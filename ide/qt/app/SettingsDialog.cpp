#include "SettingsDialog.h"
#include "AppSettings.h"
#include "editor/EditorTheme.h"
#include "sdk/SdkRegistry.h"
#include "sdk/SdkEditDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QWidget *pathRow(QWidget *parent, QLineEdit *&edit, const QString &initial, QObject *receiver, const char *slot)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit(initial, row);
    layout->addWidget(edit);
    auto *button = new QPushButton(QObject::tr("Browse..."), row);
    QObject::connect(button, SIGNAL(clicked()), receiver, slot);
    layout->addWidget(button);
    return row;
}
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Global Settings"));
    resize(560, 560);

    AppSettings &settings = AppSettings::instance();
    auto *outerLayout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    outerLayout->addWidget(tabs);
    auto *generalPage = new QWidget(tabs);
    auto *layout = new QVBoxLayout(generalPage);
    tabs->addTab(generalPage, tr("General"));
    auto *sdkPage = new QWidget(tabs);
    auto *sdkPageLayout = new QVBoxLayout(sdkPage);
    tabs->addTab(sdkPage, tr("SDKs"));

    auto *toolchainGroup = new QGroupBox(tr("Toolchain"), this);
    auto *toolchainForm = new QFormLayout(toolchainGroup);
    toolchainForm->addRow(tr("Toolchain root:"),
                           pathRow(toolchainGroup, m_toolchainRootEdit, settings.toolchainRoot(),
                                   this, SLOT(browseToolchainRoot())));
    toolchainForm->addRow(tr("make program:"),
                           pathRow(toolchainGroup, m_makeProgramEdit, settings.makeProgram(),
                                   this, SLOT(browseMakeProgram())));
    toolchainForm->addRow(tr("amivcs program:"),
                           pathRow(toolchainGroup, m_amivcsEdit, settings.amivcsProgram(),
                                   this, SLOT(browseAmivcs())));
    toolchainForm->addRow(tr("amivcs-gui program:"),
                           pathRow(toolchainGroup, m_amivcsGuiEdit, settings.amivcsGuiProgram(),
                                   this, SLOT(browseAmivcsGui())));
    layout->addWidget(toolchainGroup);

    auto *runGroup = new QGroupBox(tr("Run (FS-UAE)"), this);
    auto *runForm = new QFormLayout(runGroup);
    runForm->addRow(tr("FS-UAE binary:"),
                     pathRow(runGroup, m_fsUaeBinaryEdit, settings.fsUaeBinary(),
                             this, SLOT(browseFsUaeBinary())));
    runForm->addRow(tr("FS-UAE config:"),
                     pathRow(runGroup, m_fsUaeConfigEdit, settings.fsUaeConfig(),
                             this, SLOT(browseFsUaeConfig())));
    runForm->addRow(tr("Shared directory:"),
                     pathRow(runGroup, m_sharedDirEdit, settings.sharedDir(),
                             this, SLOT(browseSharedDir())));
    layout->addWidget(runGroup);

    auto *editorGroup = new QGroupBox(tr("Editor"), this);
    auto *editorForm = new QFormLayout(editorGroup);
    m_themeCombo = new QComboBox(editorGroup);
    m_themeCombo->addItem(tr("System"), QStringLiteral("system"));
    for (const EditorTheme &theme : EditorTheme::allThemes())
        m_themeCombo->addItem(theme.displayName, theme.id);
    m_themeCombo->setCurrentIndex(qMax(0, m_themeCombo->findData(settings.theme())));
    editorForm->addRow(tr("Theme:"), m_themeCombo);

    m_editorFontFamilyCombo = new QFontComboBox(editorGroup);
    m_editorFontFamilyCombo->setFontFilters(QFontComboBox::MonospacedFonts);
    const QString currentFamily = settings.editorFontFamily().isEmpty()
                                       ? QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
                                       : settings.editorFontFamily();
    m_editorFontFamilyCombo->setCurrentFont(QFont(currentFamily));
    editorForm->addRow(tr("Font family:"), m_editorFontFamilyCombo);

    m_editorFontSizeSpin = new QSpinBox(editorGroup);
    m_editorFontSizeSpin->setRange(6, 48);
    m_editorFontSizeSpin->setValue(settings.editorFontPointSize());
    editorForm->addRow(tr("Font size:"), m_editorFontSizeSpin);

    m_fontPreviewLabel = new QLabel(QStringLiteral("AaBbCc 0123 m68k"), editorGroup);
    m_fontPreviewLabel->setFrameShape(QFrame::StyledPanel);
    m_fontPreviewLabel->setMargin(6);
    editorForm->addRow(tr("Preview:"), m_fontPreviewLabel);

    connect(m_editorFontFamilyCombo, &QFontComboBox::currentFontChanged,
            this, &SettingsDialog::updateFontPreview);
    connect(m_editorFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::updateFontPreview);
    updateFontPreview();

    m_editorTabWidthSpin = new QSpinBox(editorGroup);
    m_editorTabWidthSpin->setRange(1, 16);
    m_editorTabWidthSpin->setValue(settings.editorTabWidth());
    editorForm->addRow(tr("Tab width:"), m_editorTabWidthSpin);

    layout->addWidget(editorGroup);
    layout->addStretch();

    // Where each built-in SDK lives. Optional: an empty field keeps the
    // default under <toolchain>/sdk, so nobody has to fill this in. A
    // directory named differently ("NDK32" instead of "NDK_3.2") is chosen
    // here once and then used for the build, the index and the help.
    auto *locationGroup = new QGroupBox(tr("SDK locations"), sdkPage);
    auto *locationLayout = new QVBoxLayout(locationGroup);
    auto *locationHint = new QLabel(tr("Base directory of each SDK. Leave empty to use the default "
                                       "shown in grey; the include, lib and autodoc directories "
                                       "below it keep their usual names."),
                                    locationGroup);
    locationHint->setWordWrap(true);
    locationLayout->addWidget(locationHint);
    auto *locationScroll = new QScrollArea(locationGroup);
    locationScroll->setWidgetResizable(true);
    locationScroll->setFrameShape(QFrame::NoFrame);
    auto *locationRows = new QWidget(locationScroll);
    auto *locationForm = new QFormLayout(locationRows);
    const QMap<QString, QString> overrides = settings.sdkRootOverrides();
    for (const SdkDescriptor &d : SdkRegistry::instance().defaultBuiltinSdks()) {
        auto *row = new QWidget(locationRows);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(overrides.value(d.id), row);
        edit->setPlaceholderText(d.rootDir);
        edit->setClearButtonEnabled(true);
        edit->setToolTip(tr("Default: %1").arg(d.rootDir));
        rowLayout->addWidget(edit);
        auto *button = new QPushButton(tr("Browse..."), row);
        connect(button, &QPushButton::clicked, this, [this, edit, d]() {
            const QString start = edit->text().trimmed().isEmpty() ? d.rootDir : edit->text().trimmed();
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Select %1 Directory").arg(d.displayName),
                                                                  start);
            if (!dir.isEmpty())
                edit->setText(dir);
        });
        rowLayout->addWidget(button);
        locationForm->addRow(d.displayName + QLatin1Char(':'), row);
        m_sdkRootEdits.insert(d.id, edit);
    }
    locationScroll->setWidget(locationRows);
    locationLayout->addWidget(locationScroll);
    sdkPageLayout->addWidget(locationGroup, 2);

    auto *sdkGroup = new QGroupBox(tr("User-defined SDKs"), sdkPage);
    auto *sdkLayout = new QHBoxLayout(sdkGroup);
    m_sdkList = new QListWidget(sdkGroup);
    sdkLayout->addWidget(m_sdkList);
    auto *sdkButtonsLayout = new QVBoxLayout;
    auto *addButton = new QPushButton(tr("Add..."), sdkGroup);
    auto *editButton = new QPushButton(tr("Edit..."), sdkGroup);
    auto *removeButton = new QPushButton(tr("Remove"), sdkGroup);
    connect(addButton, &QPushButton::clicked, this, &SettingsDialog::addSdk);
    connect(editButton, &QPushButton::clicked, this, &SettingsDialog::editSdk);
    connect(removeButton, &QPushButton::clicked, this, &SettingsDialog::removeSdk);
    sdkButtonsLayout->addWidget(addButton);
    sdkButtonsLayout->addWidget(editButton);
    sdkButtonsLayout->addWidget(removeButton);
    sdkButtonsLayout->addStretch();
    sdkLayout->addLayout(sdkButtonsLayout);
    sdkPageLayout->addWidget(sdkGroup, 1);

    reloadSdkList();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outerLayout->addWidget(buttons);
}

void SettingsDialog::updateFontPreview()
{
    QFont f = m_editorFontFamilyCombo->currentFont();
    f.setPointSize(m_editorFontSizeSpin->value());
    m_fontPreviewLabel->setFont(f);
}

void SettingsDialog::reloadSdkList()
{
    m_sdkList->clear();
    for (const SdkDescriptor &d : SdkRegistry::instance().userSdks()) {
        auto *item = new QListWidgetItem(d.fromManifest ? tr("%1 (AmiDevSDK.json)").arg(d.label()) : d.label(),
                                         m_sdkList);
        item->setData(Qt::UserRole, d.id);
        if (d.fromManifest)
            item->setToolTip(tr("Described by %1/AmiDevSDK.json -- edit that file to change it.").arg(d.rootDir));
    }
}

void SettingsDialog::browseToolchainRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Toolchain Root"), m_toolchainRootEdit->text());
    if (!dir.isEmpty())
        m_toolchainRootEdit->setText(dir);
}

void SettingsDialog::browseAmivcs()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select amivcs Program"), m_amivcsEdit->text());
    if (!path.isEmpty())
        m_amivcsEdit->setText(path);
}

void SettingsDialog::browseAmivcsGui()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select amivcs-gui Program"), m_amivcsGuiEdit->text());
    if (!path.isEmpty())
        m_amivcsGuiEdit->setText(path);
}

void SettingsDialog::browseMakeProgram()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select make Program"), m_makeProgramEdit->text());
    if (!path.isEmpty())
        m_makeProgramEdit->setText(path);
}

void SettingsDialog::browseFsUaeBinary()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select FS-UAE Binary"), m_fsUaeBinaryEdit->text());
    if (!path.isEmpty())
        m_fsUaeBinaryEdit->setText(path);
}

void SettingsDialog::browseFsUaeConfig()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select FS-UAE Config"), m_fsUaeConfigEdit->text(), tr("FS-UAE config (*.fs-uae)"));
    if (!path.isEmpty())
        m_fsUaeConfigEdit->setText(path);
}

void SettingsDialog::browseSharedDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Shared Directory"), m_sharedDirEdit->text());
    if (!dir.isEmpty())
        m_sharedDirEdit->setText(dir);
}

void SettingsDialog::addSdk()
{
    SdkEditDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    SdkRegistry::instance().addUserSdk(dialog.descriptor());
    reloadSdkList();
}

void SettingsDialog::editSdk()
{
    auto *item = m_sdkList->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    const SdkDescriptor *existing = SdkRegistry::instance().find(id);
    if (!existing)
        return;
    if (existing->fromManifest) {
        QMessageBox::information(this, tr("Edit SDK"),
                                 tr("This SDK is described by\n%1/AmiDevSDK.json\n\nEdit that file to change it.")
                                     .arg(existing->rootDir));
        return;
    }

    SdkEditDialog dialog(this);
    dialog.setDescriptor(*existing);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // A new name or version is a new id: the old entry goes.
    const SdkDescriptor edited = dialog.descriptor();
    if (edited.id.compare(id, Qt::CaseInsensitive) != 0)
        SdkRegistry::instance().removeUserSdk(id);
    SdkRegistry::instance().addUserSdk(edited);
    reloadSdkList();
}

void SettingsDialog::removeSdk()
{
    auto *item = m_sdkList->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    const SdkDescriptor *existing = SdkRegistry::instance().find(id);
    if (existing && existing->fromManifest) {
        QMessageBox::information(this, tr("Remove SDK"),
                                 tr("This SDK is described by\n%1/AmiDevSDK.json\n\nRemove that file or its "
                                    "directory to remove the SDK.").arg(existing->rootDir));
        return;
    }
    SdkRegistry::instance().removeUserSdk(id);
    reloadSdkList();
}

void SettingsDialog::accept()
{
    AppSettings &settings = AppSettings::instance();
    settings.setToolchainRoot(m_toolchainRootEdit->text());
    settings.setMakeProgram(m_makeProgramEdit->text());
    settings.setAmivcsProgram(m_amivcsEdit->text().trimmed());
    settings.setAmivcsGuiProgram(m_amivcsGuiEdit->text().trimmed());
    settings.setFsUaeBinary(m_fsUaeBinaryEdit->text());
    settings.setFsUaeConfig(m_fsUaeConfigEdit->text());
    settings.setSharedDir(m_sharedDirEdit->text());
    settings.setTheme(m_themeCombo->currentData().toString());
    settings.setEditorFontFamily(m_editorFontFamilyCombo->currentFont().family());
    settings.setEditorFontPointSize(m_editorFontSizeSpin->value());
    settings.setEditorTabWidth(m_editorTabWidthSpin->value());
    for (auto it = m_sdkRootEdits.cbegin(); it != m_sdkRootEdits.cend(); ++it)
        settings.setSdkRootOverride(it.key(), it.value()->text());

    QDialog::accept();
}
