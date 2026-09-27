#include "ProjectSettingsDialog.h"
#include "sdk/SdkRegistry.h"
#include "sdk/SdkEditDialog.h"
#include "app/AppSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QStringList splitLines(const QString &text)
{
    QStringList list = text.split('\n', Qt::SkipEmptyParts);
    for (QString &s : list)
        s = s.trimmed();
    list.removeAll(QString());
    return list;
}

QString joinLines(const QStringList &list)
{
    return list.join('\n');
}
}

ProjectSettingsDialog::ProjectSettingsDialog(const Project &project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(tr("Project Settings"));
    resize(640, 560);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(createGeneralTab(), tr("General"));
    tabs->addTab(createCompilerTab(), tr("Compiler"));
    tabs->addTab(createAssemblerTab(), tr("Assembler"));
    tabs->addTab(createLinkerTab(), tr("Linker"));
    tabs->addTab(createSdksTab(), tr("SDKs"));
    tabs->addTab(createRunTab(), tr("Run"));

    updateLanguageDependentState();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

QWidget *ProjectSettingsDialog::createGeneralTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    // First row: everything else on this dialog depends on it. Switching
    // language changes which toolchain builds the project, so the tabs
    // that only apply to one of them are enabled from here.
    m_languageCombo = new QComboBox(page);
    m_languageCombo->addItem(tr("C"), int(ProjectLanguage::C));
    m_languageCombo->addItem(tr("ACE BASIC"), int(ProjectLanguage::Basic));
    m_languageCombo->addItem(tr("Assembler"), int(ProjectLanguage::Asm));
    m_languageCombo->setCurrentIndex(int(m_project.language));
    connect(m_languageCombo, &QComboBox::currentIndexChanged,
            this, &ProjectSettingsDialog::updateLanguageDependentState);
    form->addRow(tr("Language:"), m_languageCombo);

    // Executable or link library. A library is not linked at all: the
    // object files are concatenated, which is what an Amiga link library
    // is (vc.lib and amiga.lib both start with HUNK_UNIT).
    m_outputKindCombo = new QComboBox(page);
    m_outputKindCombo->addItem(tr("Executable"), int(OutputKind::Executable));
    m_outputKindCombo->addItem(tr("Link library"), int(OutputKind::Library));
    m_outputKindCombo->addItem(tr("Shared library (.library)"), int(OutputKind::SharedLibrary));
    m_outputKindCombo->setCurrentIndex(int(m_project.output.kind));
    connect(m_outputKindCombo, &QComboBox::currentIndexChanged,
            this, &ProjectSettingsDialog::updateLanguageDependentState);
    form->addRow(tr("Output:"), m_outputKindCombo);

    m_nameEdit = new QLineEdit(m_project.name, page);
    form->addRow(tr("Name:"), m_nameEdit);

    m_binaryEdit = new QLineEdit(m_project.output.binary, page);
    form->addRow(tr("Binary:"), m_binaryEdit);

    m_targetDirEdit = new QLineEdit(m_project.output.targetDir, page);
    form->addRow(tr("Target dir:"), m_targetDirEdit);

    m_objDirEdit = new QLineEdit(m_project.output.objDir, page);
    form->addRow(tr("Object dir:"), m_objDirEdit);

    auto *typeRow = new QWidget(page);
    auto *typeLayout = new QHBoxLayout(typeRow);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    m_cliRadio = new QRadioButton(tr("CLI"), typeRow);
    m_wbRadio = new QRadioButton(tr("Workbench"), typeRow);
    (m_project.program.type == ProgramConfig::Type::Wb ? m_wbRadio : m_cliRadio)->setChecked(true);
    typeLayout->addWidget(m_cliRadio);
    typeLayout->addWidget(m_wbRadio);
    typeLayout->addStretch();
    form->addRow(tr("Program type:"), typeRow);

    m_stackSizeSpin = new QSpinBox(page);
    m_stackSizeSpin->setRange(1024, 1024 * 1024);
    m_stackSizeSpin->setSingleStep(1024);
    m_stackSizeSpin->setValue(m_project.program.stackSize);
    form->addRow(tr("Stack size:"), m_stackSizeSpin);

    auto *iconGroup = new QGroupBox(tr("Workbench icon"), page);
    auto *iconLayout = new QVBoxLayout(iconGroup);
    m_iconGenerateRadio = new QRadioButton(tr("Generate default tool icon"), iconGroup);
    m_iconFileRadio = new QRadioButton(tr("Use existing .info file"), iconGroup);
    (m_project.program.iconMode == ProgramConfig::IconMode::File ? m_iconFileRadio : m_iconGenerateRadio)->setChecked(true);
    auto *iconPathRow = new QWidget(iconGroup);
    auto *iconPathLayout = new QHBoxLayout(iconPathRow);
    iconPathLayout->setContentsMargins(0, 0, 0, 0);
    m_iconPathEdit = new QLineEdit(m_project.program.iconPath, iconPathRow);
    auto *iconBrowseButton = new QPushButton(tr("Browse..."), iconPathRow);
    connect(iconBrowseButton, &QPushButton::clicked, this, &ProjectSettingsDialog::browseIconPath);
    iconPathLayout->addWidget(m_iconPathEdit);
    iconPathLayout->addWidget(iconBrowseButton);
    iconLayout->addWidget(m_iconGenerateRadio);
    iconLayout->addWidget(m_iconFileRadio);
    iconLayout->addWidget(iconPathRow);
    form->addRow(iconGroup);

    return page;
}

QWidget *ProjectSettingsDialog::createCompilerTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_cpuCombo = new QComboBox(page);
    m_cpuCombo->addItems({ "68000", "68010", "68020", "68030", "68040", "68060" });
    m_cpuCombo->setCurrentText(m_project.compiler.cpu);
    form->addRow(tr("CPU:"), m_cpuCombo);

    m_fpuCombo = new QComboBox(page);
    m_fpuCombo->addItems({ "none", "68881", "68882", "68040", "68060", "softfloat" });
    m_fpuCombo->setCurrentText(m_project.compiler.fpu);
    form->addRow(tr("FPU:"), m_fpuCombo);

    m_optLevelSpin = new QSpinBox(page);
    m_optLevelSpin->setRange(0, 3);
    m_optLevelSpin->setValue(m_project.compiler.optLevel);
    form->addRow(tr("Optimization:"), m_optLevelSpin);

    m_smallDataCheck = new QCheckBox(tr("Small data (-sd)"), page);
    m_smallDataCheck->setChecked(m_project.compiler.smallData);
    form->addRow(m_smallDataCheck);

    m_debugSymbolsCheck = new QCheckBox(tr("Debug symbols (-g)"), page);
    m_debugSymbolsCheck->setChecked(m_project.compiler.debugSymbols);
    form->addRow(m_debugSymbolsCheck);

    m_includePathsEdit = new QPlainTextEdit(joinLines(m_project.includePaths), page);
    m_includePathsEdit->setPlaceholderText(tr("One include path per line"));
    m_includePathsEdit->setFixedHeight(70);
    form->addRow(tr("Include paths:"), m_includePathsEdit);

    m_definesEdit = new QPlainTextEdit(joinLines(m_project.defines), page);
    m_definesEdit->setPlaceholderText(tr("One define per line, e.g. VERSION=1"));
    m_definesEdit->setFixedHeight(70);
    form->addRow(tr("Defines:"), m_definesEdit);

    m_extraCFlagsEdit = new QPlainTextEdit(joinLines(m_project.compiler.extraCFlags), page);
    m_extraCFlagsEdit->setFixedHeight(50);
    form->addRow(tr("Extra vc flags:"), m_extraCFlagsEdit);

    return page;
}

QWidget *ProjectSettingsDialog::createAssemblerTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_kick1hunksCheck = new QCheckBox(tr("-kick1hunks"), page);
    m_kick1hunksCheck->setChecked(m_project.compiler.kick1hunks);
    form->addRow(m_kick1hunksCheck);

    m_asmNoSymCheck = new QCheckBox(tr("-nosym"), page);
    m_asmNoSymCheck->setChecked(m_project.compiler.asmNoSym);
    form->addRow(m_asmNoSymCheck);

    m_extraAsmFlagsEdit = new QPlainTextEdit(joinLines(m_project.compiler.extraAsmFlags), page);
    m_extraAsmFlagsEdit->setFixedHeight(70);
    form->addRow(tr("Extra vasm flags:"), m_extraAsmFlagsEdit);

    return page;
}

QWidget *ProjectSettingsDialog::createLinkerTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *startupGroup = new QGroupBox(tr("Startup"), page);
    auto *startupLayout = new QVBoxLayout(startupGroup);
    m_startupStdRadio = new QRadioButton(tr("startup.o (standard)"), startupGroup);
    m_startupMinStartRadio = new QRadioButton(tr("minstart.o"), startupGroup);
    m_startupMinResRadio = new QRadioButton(tr("minres.o (resident, uses -lvcs)"), startupGroup);
    m_startupCustomRadio = new QRadioButton(tr("Custom:"), startupGroup);
    m_customStartupEdit = new QLineEdit(m_project.linker.customStartupPath, startupGroup);
    auto *customRow = new QWidget(startupGroup);
    auto *customLayout = new QHBoxLayout(customRow);
    customLayout->setContentsMargins(0, 0, 0, 0);
    auto *customBrowseButton = new QPushButton(tr("Browse..."), customRow);
    connect(customBrowseButton, &QPushButton::clicked, this, &ProjectSettingsDialog::browseCustomStartup);
    customLayout->addWidget(m_startupCustomRadio);
    customLayout->addWidget(m_customStartupEdit);
    customLayout->addWidget(customBrowseButton);

    switch (m_project.linker.startup) {
    case LinkerConfig::Startup::MinStart: m_startupMinStartRadio->setChecked(true); break;
    case LinkerConfig::Startup::MinRes: m_startupMinResRadio->setChecked(true); break;
    case LinkerConfig::Startup::Custom: m_startupCustomRadio->setChecked(true); break;
    default: m_startupStdRadio->setChecked(true); break;
    }

    startupLayout->addWidget(m_startupStdRadio);
    startupLayout->addWidget(m_startupMinStartRadio);
    startupLayout->addWidget(m_startupMinResRadio);
    startupLayout->addWidget(customRow);
    layout->addWidget(startupGroup);

    auto *libsGroup = new QGroupBox(tr("Link libraries (ordered)"), page);
    auto *libsLayout = new QHBoxLayout(libsGroup);
    m_librariesList = new QListWidget(libsGroup);
    m_librariesList->addItems(m_project.linker.libraries);
    libsLayout->addWidget(m_librariesList);
    auto *libsButtonsLayout = new QVBoxLayout;
    auto *addLibButton = new QPushButton(tr("Add..."), libsGroup);
    auto *removeLibButton = new QPushButton(tr("Remove"), libsGroup);
    auto *upButton = new QPushButton(tr("Up"), libsGroup);
    auto *downButton = new QPushButton(tr("Down"), libsGroup);
    connect(addLibButton, &QPushButton::clicked, this, &ProjectSettingsDialog::addLibrary);
    connect(removeLibButton, &QPushButton::clicked, this, &ProjectSettingsDialog::removeLibrary);
    connect(upButton, &QPushButton::clicked, this, &ProjectSettingsDialog::moveLibraryUp);
    connect(downButton, &QPushButton::clicked, this, &ProjectSettingsDialog::moveLibraryDown);
    libsButtonsLayout->addWidget(addLibButton);
    libsButtonsLayout->addWidget(removeLibButton);
    libsButtonsLayout->addWidget(upButton);
    libsButtonsLayout->addWidget(downButton);
    libsButtonsLayout->addStretch();
    libsLayout->addLayout(libsButtonsLayout);
    layout->addWidget(libsGroup);

    auto *form = new QFormLayout;
    m_extraLibDirsEdit = new QPlainTextEdit(joinLines(m_project.linker.extraLibDirs), page);
    m_extraLibDirsEdit->setFixedHeight(50);
    form->addRow(tr("Extra lib dirs:"), m_extraLibDirsEdit);

    m_stripCheck = new QCheckBox(tr("Strip (-s -Rshort)"), page);
    m_stripCheck->setChecked(m_project.linker.strip);
    form->addRow(m_stripCheck);

    m_extraLdFlagsEdit = new QPlainTextEdit(joinLines(m_project.linker.extraLdFlags), page);
    m_extraLdFlagsEdit->setFixedHeight(50);
    form->addRow(tr("Extra vlink flags:"), m_extraLdFlagsEdit);

    layout->addLayout(form);
    return page;
}

QWidget *ProjectSettingsDialog::createSdksTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    // One list for everything. The NDK and MUI used to have dedicated
    // selectors from when there were two of each to choose between; there is
    // one of each now and neither is compulsory -- a program that drives the
    // hardware directly needs no system headers at all.
    auto *sdkGroup = new QGroupBox(tr("SDKs"), page);
    auto *sdkLayout = new QVBoxLayout(sdkGroup);

    auto *hint = new QLabel(tr("Include and library paths are searched in this order."),
                            sdkGroup);
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    hint->setFont(hintFont);
    sdkLayout->addWidget(hint);

    m_extraSdksContainer = new QWidget(sdkGroup);
    auto *checksLayout = new QVBoxLayout(m_extraSdksContainer);
    checksLayout->setContentsMargins(0, 0, 0, 0);

    for (const SdkDescriptor &d : SdkRegistry::instance().allSdks()) {
        auto *check = new QCheckBox(d.displayName, m_extraSdksContainer);
        check->setChecked(m_project.sdks.selected.contains(d.id));
        if (d.id == QStringLiteral("ndk-3.2"))
            check->setToolTip(tr("The Amiga system headers."));
        checksLayout->addWidget(check);
        m_extraSdkChecks << check;
        m_extraSdkIds << d.id;
    }
    sdkLayout->addWidget(m_extraSdksContainer);

    auto *addSdkButton = new QPushButton(tr("Add SDK..."), sdkGroup);
    connect(addSdkButton, &QPushButton::clicked, this, &ProjectSettingsDialog::addUserSdk);
    sdkLayout->addWidget(addSdkButton);

    layout->addWidget(sdkGroup);
    layout->addStretch();
    return page;
}

QWidget *ProjectSettingsDialog::createRunTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_copyToSharedCheck = new QCheckBox(tr("Copy binary to shared/ before running"), page);
    m_copyToSharedCheck->setChecked(m_project.run.copyToShared);
    form->addRow(m_copyToSharedCheck);

    m_sharedSubdirEdit = new QLineEdit(m_project.run.sharedSubdir, page);
    form->addRow(tr("Shared subdir:"), m_sharedSubdirEdit);

    auto *configRow = new QWidget(page);
    auto *configLayout = new QHBoxLayout(configRow);
    configLayout->setContentsMargins(0, 0, 0, 0);
    m_emulatorConfigOverrideEdit = new QLineEdit(m_project.run.emulatorConfigOverride, configRow);
    auto *configBrowseButton = new QPushButton(tr("Browse..."), configRow);
    connect(configBrowseButton, &QPushButton::clicked, this, &ProjectSettingsDialog::browseEmulatorConfig);
    configLayout->addWidget(m_emulatorConfigOverrideEdit);
    configLayout->addWidget(configBrowseButton);
    form->addRow(tr("FS-UAE config override:"), configRow);

    m_extraFsUaeArgsEdit = new QPlainTextEdit(joinLines(m_project.run.extraFsUaeArgs), page);
    m_extraFsUaeArgsEdit->setFixedHeight(60);
    form->addRow(tr("Extra FS-UAE args:"), m_extraFsUaeArgsEdit);

    return page;
}

void ProjectSettingsDialog::browseIconPath()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select .info Icon"), QString(), tr("Icon files (*.info)"));
    if (!path.isEmpty())
        m_iconPathEdit->setText(path);
}

void ProjectSettingsDialog::browseCustomStartup()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select Startup Object"), QString(), tr("Object files (*.o)"));
    if (!path.isEmpty())
        m_customStartupEdit->setText(path);
}

void ProjectSettingsDialog::browseEmulatorConfig()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select FS-UAE Config"), QString(), tr("FS-UAE config (*.fs-uae)"));
    if (!path.isEmpty())
        m_emulatorConfigOverrideEdit->setText(path);
}

void ProjectSettingsDialog::addLibrary()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add Library"), tr("Library name (without -l):"), QLineEdit::Normal, QString(), &ok);
    if (ok && !name.trimmed().isEmpty())
        m_librariesList->addItem(name.trimmed());
}

void ProjectSettingsDialog::removeLibrary()
{
    qDeleteAll(m_librariesList->selectedItems());
}

void ProjectSettingsDialog::moveLibraryUp()
{
    const int row = m_librariesList->currentRow();
    if (row <= 0)
        return;
    auto *item = m_librariesList->takeItem(row);
    m_librariesList->insertItem(row - 1, item);
    m_librariesList->setCurrentRow(row - 1);
}

void ProjectSettingsDialog::moveLibraryDown()
{
    const int row = m_librariesList->currentRow();
    if (row < 0 || row >= m_librariesList->count() - 1)
        return;
    auto *item = m_librariesList->takeItem(row);
    m_librariesList->insertItem(row + 1, item);
    m_librariesList->setCurrentRow(row + 1);
}

void ProjectSettingsDialog::addUserSdk()
{
    SdkEditDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const SdkDescriptor d = dialog.descriptor();
    if (d.id.isEmpty())
        return;

    SdkRegistry::instance().addUserSdk(d);

    auto *check = new QCheckBox(d.displayName.isEmpty() ? d.id : d.displayName, m_extraSdksContainer);
    check->setChecked(true);
    m_extraSdksContainer->layout()->addWidget(check);
    m_extraSdkChecks << check;
    m_extraSdkIds << d.id;
}

void ProjectSettingsDialog::accept()
{
    // A project with no name or no binary name can't be saved
    // meaningfully (the "New Project" flow in particular needs a real
    // name to even know what .aproj file to write) -- reject and leave
    // the dialog open instead of closing.
    if (m_nameEdit->text().trimmed().isEmpty() || m_binaryEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Project Settings"), tr("Name and Binary name must not be empty."));
        return;
    }
    QDialog::accept();
}

Project ProjectSettingsDialog::resultProject() const
{
    Project p = m_project;

    p.language = ProjectLanguage(m_languageCombo->currentData().toInt());
    p.output.kind = OutputKind(m_outputKindCombo->currentData().toInt());
    p.name = m_nameEdit->text();
    p.output.binary = m_binaryEdit->text();
    p.output.targetDir = m_targetDirEdit->text();
    p.output.objDir = m_objDirEdit->text();
    p.program.type = m_wbRadio->isChecked() ? ProgramConfig::Type::Wb : ProgramConfig::Type::Cli;
    p.program.stackSize = m_stackSizeSpin->value();
    p.program.iconMode = m_iconFileRadio->isChecked() ? ProgramConfig::IconMode::File : ProgramConfig::IconMode::Generate;
    p.program.iconPath = m_iconPathEdit->text();

    p.compiler.cpu = m_cpuCombo->currentText();
    p.compiler.fpu = m_fpuCombo->currentText();
    p.compiler.optLevel = m_optLevelSpin->value();
    p.compiler.smallData = m_smallDataCheck->isChecked();
    p.compiler.debugSymbols = m_debugSymbolsCheck->isChecked();
    p.includePaths = splitLines(m_includePathsEdit->toPlainText());
    p.defines = splitLines(m_definesEdit->toPlainText());
    p.compiler.extraCFlags = splitLines(m_extraCFlagsEdit->toPlainText());

    p.compiler.kick1hunks = m_kick1hunksCheck->isChecked();
    p.compiler.asmNoSym = m_asmNoSymCheck->isChecked();
    p.compiler.extraAsmFlags = splitLines(m_extraAsmFlagsEdit->toPlainText());

    if (m_startupMinStartRadio->isChecked())
        p.linker.startup = LinkerConfig::Startup::MinStart;
    else if (m_startupMinResRadio->isChecked())
        p.linker.startup = LinkerConfig::Startup::MinRes;
    else if (m_startupCustomRadio->isChecked())
        p.linker.startup = LinkerConfig::Startup::Custom;
    else
        p.linker.startup = LinkerConfig::Startup::Std;
    p.linker.customStartupPath = m_customStartupEdit->text();

    QStringList libs;
    for (int i = 0; i < m_librariesList->count(); ++i)
        libs << m_librariesList->item(i)->text();
    p.linker.libraries = libs;
    p.linker.extraLibDirs = splitLines(m_extraLibDirsEdit->toPlainText());
    p.linker.strip = m_stripCheck->isChecked();
    p.linker.extraLdFlags = splitLines(m_extraLdFlagsEdit->toPlainText());

    // Written in registry order, which is the order the checkboxes were built
    // in -- so the emitted -I/-L sequence is stable across edits.
    QStringList selected;
    for (int i = 0; i < m_extraSdkChecks.size(); ++i) {
        if (m_extraSdkChecks[i]->isChecked())
            selected << m_extraSdkIds[i];
    }
    p.sdks.selected = selected;

    p.run.copyToShared = m_copyToSharedCheck->isChecked();
    p.run.sharedSubdir = m_sharedSubdirEdit->text();
    p.run.emulatorConfigOverride = m_emulatorConfigOverrideEdit->text();
    p.run.extraFsUaeArgs = splitLines(m_extraFsUaeArgsEdit->toPlainText());

    return p;
}

void ProjectSettingsDialog::updateLanguageDependentState()
{
    const bool basic = m_languageCombo->currentData().toInt() == int(ProjectLanguage::Basic);
    const bool library = m_outputKindCombo
                      && m_outputKindCombo->currentData().toInt() == int(OutputKind::Library);
    // A shared library is linked, so libraries and flags still apply -- it
    // just brings no startup object of its own.
    const bool sharedLib = m_outputKindCombo
                        && m_outputKindCombo->currentData().toInt() == int(OutputKind::SharedLibrary);

    // The link stage is vlink either way, so libraries, library paths,
    // extra flags and strip all still apply -- only the STARTUP choice
    // does not: an ACE program always uses ACE's own startup.lib, picked
    // by the generator from the language and CPU, and vbcc's
    // minstart.o/minres.o would produce a binary that never starts.
    // A library is concatenated, never linked, so nothing on the linker
    // side applies to it at all.
    const bool startupApplies = !basic && !library && !sharedLib;
    if (m_startupStdRadio)      m_startupStdRadio->setEnabled(startupApplies);
    if (m_startupMinStartRadio) m_startupMinStartRadio->setEnabled(startupApplies);
    if (m_startupMinResRadio)   m_startupMinResRadio->setEnabled(startupApplies);
    if (m_startupCustomRadio)   m_startupCustomRadio->setEnabled(startupApplies);
    if (m_customStartupEdit)    m_customStartupEdit->setEnabled(startupApplies);
    if (m_librariesList)        m_librariesList->setEnabled(!library);
    if (m_extraLibDirsEdit)     m_extraLibDirsEdit->setEnabled(!library);
    if (m_extraLdFlagsEdit)     m_extraLdFlagsEdit->setEnabled(!library);
    if (m_stripCheck)           m_stripCheck->setEnabled(!library);

    // FPU/small-data/debug are vbcc code-generation switches; ace has none
    // of them. Optimisation maps onto ace's -O, and CPU onto -0/-2.
    if (m_fpuCombo)          m_fpuCombo->setEnabled(!basic);
    if (m_smallDataCheck)    m_smallDataCheck->setEnabled(!basic);
    if (m_debugSymbolsCheck) m_debugSymbolsCheck->setEnabled(!basic);

    // ACE's startup manages its own stack; __stack is a vc.lib convention.
    if (m_stackSizeSpin)     m_stackSizeSpin->setEnabled(!basic);
}
