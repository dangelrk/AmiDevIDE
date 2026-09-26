#pragma once

#include <QDialog>

class QLineEdit;

// "Add New File to Project": the category (C source / Assembler source /
// C header / Assembler include) is picked from the "Add New File to
// Project" submenu before this dialog even opens (see MainWindow.cpp) --
// this just asks for a name and a location (relative to the project
// root). Category determines the default extension and whether the
// result should end up in the project's `sources` or `headers` list.
class AddFileDialog : public QDialog {
    Q_OBJECT
public:
    enum class FileCategory { CSource, AsmSource, BasicSource, CHeader, AsmInclude };

    AddFileDialog(FileCategory category, const QString &projectRoot, QWidget *parent = nullptr);

    // Valid only after exec() == QDialog::Accepted.
    QString resultRelativePath() const { return m_resultRelativePath; }
    FileCategory category() const { return m_category; }

private slots:
    void browseLocation();
    void accept() override;

private:
    FileCategory m_category;
    QString m_projectRoot;
    QLineEdit *m_nameEdit;
    QLineEdit *m_locationEdit;

    QString m_resultRelativePath;
};
