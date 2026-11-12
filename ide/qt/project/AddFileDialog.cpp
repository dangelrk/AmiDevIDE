#include "AddFileDialog.h"
#include "aide/aide_project.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString defaultExtension(AddFileDialog::FileCategory category)
{
    using Category = AddFileDialog::FileCategory;
    switch (category) {
    case Category::CHeader:    return QStringLiteral(".h");
    case Category::AsmSource:  return QStringLiteral(".s");
    case Category::BasicSource: return QStringLiteral(".b");
    case Category::AsmInclude: return QStringLiteral(".i");
    default:                   return QStringLiteral(".c");
    }
}

// ASM sources accept both ".s" and ".asm" (both are used in the wild);
// everything else has exactly one expected extension.
bool extensionMatchesCategory(AddFileDialog::FileCategory category, const QString &ext)
{
    using Category = AddFileDialog::FileCategory;
    const QString lower = ext.toLower();
    switch (category) {
    case Category::CSource:    return lower == QLatin1String(".c");
    case Category::CHeader:    return lower == QLatin1String(".h");
    case Category::AsmSource:  return lower == QLatin1String(".s") || lower == QLatin1String(".asm");
    // ACE accepts .b and .bas alike (lex.c open_files)
    case Category::BasicSource: return lower == QLatin1String(".b") || lower == QLatin1String(".bas");
    case Category::AsmInclude: return lower == QLatin1String(".i");
    }
    return false;
}

QString categoryLabel(AddFileDialog::FileCategory category)
{
    using Category = AddFileDialog::FileCategory;
    switch (category) {
    case Category::CSource:    return QObject::tr("C Source");
    case Category::CHeader:    return QObject::tr("C Header");
    case Category::AsmSource:  return QObject::tr("Assembler Source");
    case Category::AsmInclude: return QObject::tr("Assembler Include");
    }
    return QString();
}

} // namespace

AddFileDialog::AddFileDialog(FileCategory category, const QString &projectRoot, QWidget *parent)
    : QDialog(parent)
    , m_category(category)
    , m_projectRoot(projectRoot)
{
    setWindowTitle(tr("Add New %1 File").arg(categoryLabel(category)));

    auto *form = new QFormLayout;
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("e.g. utils (extension added automatically)"));
    form->addRow(tr("Name:"), m_nameEdit);

    auto *locationRow = new QWidget(this);
    auto *locationLayout = new QHBoxLayout(locationRow);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    const bool isHeaderLike = (category == FileCategory::CHeader || category == FileCategory::AsmInclude);
    m_locationEdit = new QLineEdit(isHeaderLike ? QStringLiteral("include") : QStringLiteral("src"), locationRow);
    auto *browseButton = new QPushButton(tr("Browse..."), locationRow);
    connect(browseButton, &QPushButton::clicked, this, &AddFileDialog::browseLocation);
    locationLayout->addWidget(m_locationEdit);
    locationLayout->addWidget(browseButton);
    form->addRow(tr("Location (relative to project):"), locationRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &AddFileDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void AddFileDialog::browseLocation()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Location"), QDir(m_projectRoot).absoluteFilePath(m_locationEdit->text()));
    if (dir.isEmpty())
        return;
    m_locationEdit->setText(QDir(m_projectRoot).relativeFilePath(dir));
}

void AddFileDialog::accept()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Add New File"), tr("Please enter a file name."));
        return;
    }

    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        name += defaultExtension(m_category);
    } else if (!extensionMatchesCategory(m_category, name.mid(dot))) {
        const auto choice = QMessageBox::question(
            this, tr("Add New File"),
            tr("\"%1\" doesn't match the chosen file type (%2, expected %3).\n\nAdd it anyway?")
                .arg(name, categoryLabel(m_category), defaultExtension(m_category)));
        if (choice != QMessageBox::Yes)
            return;
    }

    const QString location = m_locationEdit->text().trimmed();
    m_resultRelativePath = location.isEmpty() ? name : location + QLatin1Char('/') + name;

    // classic Amiga FFS keeps 30 characters per name (aide_project.h)
    if (!aide_project_long_names(m_projectRoot.toUtf8().constData())
        && !aide_names_fit(m_resultRelativePath.toUtf8().constData(), AIDE_AMIGA_NAME_MAX)) {
        QMessageBox::warning(this, tr("Add New File"),
                             tr("A file or folder name may have at most %1 characters here: classic "
                                "Amiga FFS keeps no more and would cut it silently. Long names can be "
                                "allowed in the project settings.").arg(AIDE_AMIGA_NAME_MAX));
        return;
    }

    QDialog::accept();
}
