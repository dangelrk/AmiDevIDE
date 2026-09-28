#include "SdkEditDialog.h"
#include "SdkRegistry.h"

#include "aide/aide_sdk.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdlib>

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

SdkEditDialog::SdkEditDialog(QWidget *parent)
    : QDialog(parent)
    , m_nameEdit(new QLineEdit(this))
    , m_versionEdit(new QLineEdit(this))
    , m_idLabel(new QLabel(this))
    , m_kindCombo(new QComboBox(this))
    , m_rootDirEdit(new QLineEdit(this))
    , m_includeDirsEdit(new QPlainTextEdit(this))
    , m_asmIncludeDirsEdit(new QPlainTextEdit(this))
    , m_libDirsEdit(new QPlainTextEdit(this))
    , m_autodocDirsEdit(new QPlainTextEdit(this))
    , m_sfdDirsEdit(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Add / Edit SDK"));

    m_kindCombo->addItem(tr("Other"), QStringLiteral("other"));
    m_kindCombo->addItem(tr("NDK"), QStringLiteral("ndk"));
    m_kindCombo->addItem(tr("MUI"), QStringLiteral("mui"));

    auto *rootRow = new QWidget(this);
    auto *rootRowLayout = new QHBoxLayout(rootRow);
    rootRowLayout->setContentsMargins(0, 0, 0, 0);
    rootRowLayout->addWidget(m_rootDirEdit);
    auto *browseButton = new QPushButton(tr("Browse..."), rootRow);
    rootRowLayout->addWidget(browseButton);
    connect(browseButton, &QPushButton::clicked, this, &SdkEditDialog::browseRootDir);

    for (QPlainTextEdit *edit : { m_includeDirsEdit, m_asmIncludeDirsEdit, m_libDirsEdit,
                                  m_autodocDirsEdit, m_sfdDirsEdit }) {
        edit->setPlaceholderText(tr("One absolute path per line"));
        edit->setFixedHeight(60);
    }

    auto *form = new QFormLayout;
    m_nameEdit->setPlaceholderText(tr("e.g. AmiSSL"));
    m_versionEdit->setPlaceholderText(tr("e.g. 5.18"));
    m_idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_idLabel->setToolTip(tr("What the project stores. Derived from name and version; "
                             "the MUI IDE finds the SDK by the same id."));
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SdkEditDialog::updateIdPreview);
    connect(m_versionEdit, &QLineEdit::textChanged, this, &SdkEditDialog::updateIdPreview);
    updateIdPreview();

    form->addRow(tr("Name:"), m_nameEdit);
    form->addRow(tr("Version:"), m_versionEdit);
    form->addRow(tr("Id:"), m_idLabel);
    form->addRow(tr("Kind:"), m_kindCombo);
    form->addRow(tr("Root directory:"), rootRow);
    form->addRow(tr("Include dirs:"), m_includeDirsEdit);
    form->addRow(tr("Asm include dirs:"), m_asmIncludeDirsEdit);
    form->addRow(tr("Lib dirs:"), m_libDirsEdit);
    form->addRow(tr("Autodoc dirs:"), m_autodocDirsEdit);
    form->addRow(tr("SFD dirs:"), m_sfdDirsEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    resize(480, 520);
}

void SdkEditDialog::browseRootDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select SDK root directory"));
    if (!dir.isEmpty())
        m_rootDirEdit->setText(dir);
}

QString SdkEditDialog::currentId() const
{
    const QByteArray name = m_nameEdit->text().trimmed().toUtf8();
    const QByteArray version = m_versionEdit->text().trimmed().toUtf8();
    char *id = aide_sdk_make_id(name.constData(), version.constData());
    const QString result = QString::fromUtf8(id);
    free(id);
    return result;
}

void SdkEditDialog::updateIdPreview()
{
    const QString id = currentId();
    m_idLabel->setText(id.isEmpty() ? tr("(enter a name and a version)") : id);
}

void SdkEditDialog::accept()
{
    if (m_nameEdit->text().trimmed().isEmpty() || m_versionEdit->text().trimmed().isEmpty()
        || currentId().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("An SDK needs a name and a version."));
        return;
    }
    const QString id = currentId();
    if (id.compare(m_originalId, Qt::CaseInsensitive) != 0) {
        if (const SdkDescriptor *other = SdkRegistry::instance().find(id)) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("There already is an SDK \"%1\" (id %2).").arg(other->label(), other->id));
            return;
        }
    }
    QDialog::accept();
}

void SdkEditDialog::setDescriptor(const SdkDescriptor &d)
{
    m_originalId = d.id;
    m_nameEdit->setText(d.displayName);
    m_versionEdit->setText(d.version);
    const int kindIndex = m_kindCombo->findData(
        d.kind == SdkKind::Ndk ? QStringLiteral("ndk") : d.kind == SdkKind::Mui ? QStringLiteral("mui")
                                                                                 : QStringLiteral("other"));
    m_kindCombo->setCurrentIndex(qMax(0, kindIndex));
    m_rootDirEdit->setText(d.rootDir);
    m_includeDirsEdit->setPlainText(joinLines(d.includeDirs));
    m_asmIncludeDirsEdit->setPlainText(joinLines(d.asmIncludeDirs));
    m_libDirsEdit->setPlainText(joinLines(d.libDirs));
    m_autodocDirsEdit->setPlainText(joinLines(d.autodocDirs));
    m_sfdDirsEdit->setPlainText(joinLines(d.sfdDirs));
}

SdkDescriptor SdkEditDialog::descriptor() const
{
    SdkDescriptor d;
    d.id = currentId();
    d.displayName = m_nameEdit->text().trimmed();
    d.version = m_versionEdit->text().trimmed();
    const QString kindStr = m_kindCombo->currentData().toString();
    d.kind = kindStr == QLatin1String("ndk") ? SdkKind::Ndk
             : kindStr == QLatin1String("mui") ? SdkKind::Mui
                                                : SdkKind::Other;
    d.rootDir = m_rootDirEdit->text().trimmed();
    d.includeDirs = splitLines(m_includeDirsEdit->toPlainText());
    d.asmIncludeDirs = splitLines(m_asmIncludeDirsEdit->toPlainText());
    d.libDirs = splitLines(m_libDirsEdit->toPlainText());
    d.autodocDirs = splitLines(m_autodocDirsEdit->toPlainText());
    d.sfdDirs = splitLines(m_sfdDirsEdit->toPlainText());
    d.userDefined = true;
    return d;
}
