#include "SdkEditDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
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

SdkEditDialog::SdkEditDialog(QWidget *parent)
    : QDialog(parent)
    , m_idEdit(new QLineEdit(this))
    , m_displayNameEdit(new QLineEdit(this))
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
    form->addRow(tr("Id:"), m_idEdit);
    form->addRow(tr("Display name:"), m_displayNameEdit);
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

void SdkEditDialog::setDescriptor(const SdkDescriptor &d)
{
    m_idEdit->setText(d.id);
    m_displayNameEdit->setText(d.displayName);
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
    d.id = m_idEdit->text().trimmed();
    d.displayName = m_displayNameEdit->text().trimmed();
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
