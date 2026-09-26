#pragma once

#include "SdkDescriptor.h"

#include <QDialog>

class QLineEdit;
class QPlainTextEdit;
class QComboBox;

// Add/edit a user-defined SDK: name, kind, root dir, and newline-separated
// path lists for include/asm-include/lib/autodoc/sfd directories.
class SdkEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit SdkEditDialog(QWidget *parent = nullptr);

    void setDescriptor(const SdkDescriptor &descriptor);
    SdkDescriptor descriptor() const;

private slots:
    void browseRootDir();

private:
    QLineEdit *m_idEdit;
    QLineEdit *m_displayNameEdit;
    QComboBox *m_kindCombo;
    QLineEdit *m_rootDirEdit;
    QPlainTextEdit *m_includeDirsEdit;
    QPlainTextEdit *m_asmIncludeDirsEdit;
    QPlainTextEdit *m_libDirsEdit;
    QPlainTextEdit *m_autodocDirsEdit;
    QPlainTextEdit *m_sfdDirsEdit;
};
