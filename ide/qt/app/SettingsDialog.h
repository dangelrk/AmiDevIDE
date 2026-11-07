#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QListWidget;

// Global (cross-project) settings: toolchain root, make program, FS-UAE
// binary/config, shared dir, editor font/tab width/theme, user-defined SDKs.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void browseToolchainRoot();
    void browseMakeProgram();
    void browseAmivcs();
    void browseAmivcsGui();
    void browseFsUaeBinary();
    void browseFsUaeConfig();
    void browseSharedDir();
    void addSdk();
    void editSdk();
    void removeSdk();
    void accept() override;

private:
    void reloadSdkList();
    void updateFontPreview();

    QLineEdit *m_toolchainRootEdit;
    QLineEdit *m_makeProgramEdit;
    QLineEdit *m_amivcsEdit = nullptr;
    QLineEdit *m_amivcsGuiEdit = nullptr;
    QLineEdit *m_fsUaeBinaryEdit;
    QLineEdit *m_fsUaeConfigEdit;
    QLineEdit *m_sharedDirEdit;
    QComboBox *m_themeCombo;
    QFontComboBox *m_editorFontFamilyCombo;
    QSpinBox *m_editorFontSizeSpin;
    QLabel *m_fontPreviewLabel;
    QSpinBox *m_editorTabWidthSpin;
    QListWidget *m_sdkList;
};
