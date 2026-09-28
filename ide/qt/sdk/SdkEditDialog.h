#pragma once

#include "SdkDescriptor.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QComboBox;

// Add/edit a user-defined SDK: name, version, kind, root dir, and
// newline-separated path lists for include/asm-include/lib/autodoc/sfd
// directories. The id is not entered: it is derived from name and version
// (aide_sdk_make_id), so a project names the SDK the same way on every
// machine and in the MUI IDE.
class SdkEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit SdkEditDialog(QWidget *parent = nullptr);

    void setDescriptor(const SdkDescriptor &descriptor);
    SdkDescriptor descriptor() const;

    void accept() override;

private slots:
    void browseRootDir();
    void updateIdPreview();

private:
    QString currentId() const;

    QString m_originalId; // of the SDK being edited, empty when adding
    QLineEdit *m_nameEdit;
    QLineEdit *m_versionEdit;
    QLabel *m_idLabel;
    QComboBox *m_kindCombo;
    QLineEdit *m_rootDirEdit;
    QPlainTextEdit *m_includeDirsEdit;
    QPlainTextEdit *m_asmIncludeDirsEdit;
    QPlainTextEdit *m_libDirsEdit;
    QPlainTextEdit *m_autodocDirsEdit;
    QPlainTextEdit *m_sfdDirsEdit;
};
