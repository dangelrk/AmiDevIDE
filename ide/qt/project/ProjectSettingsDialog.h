#pragma once

#include "Project.h"

#include <QDialog>

class QComboBox;

class QLineEdit;
class QPlainTextEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QRadioButton;
class QListWidget;
class QButtonGroup;

// Tabs: General / Compiler / Assembler / Linker / SDKs / Run, per plan section 5.
class ProjectSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectSettingsDialog(const Project &project, QWidget *parent = nullptr);

    // Applies all UI state onto a copy of the original project and returns it.
    Project resultProject() const;

private slots:
    void accept() override;

private:
    QWidget *createGeneralTab();
    QWidget *createCompilerTab();
    QWidget *createAssemblerTab();
    QWidget *createLinkerTab();
    QWidget *createSdksTab();
    QWidget *createRunTab();

    void browseIconPath();
    void browseCustomStartup();
    void browseEmulatorConfig();
    void addLibrary();
    void removeLibrary();
    void moveLibraryUp();
    void moveLibraryDown();
    void addUserSdk();
    // Enables/disables the parts of this dialog that only apply to one of
    // the two toolchains. Called on construction and on every change of
    // the language combo.
    void updateLanguageDependentState();

    Project m_project;
    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_outputKindCombo = nullptr;

    // General
    QLineEdit *m_nameEdit;
    QLineEdit *m_binaryEdit;
    QLineEdit *m_targetDirEdit;
    QLineEdit *m_objDirEdit;
    QRadioButton *m_cliRadio;
    QRadioButton *m_wbRadio;
    QSpinBox *m_stackSizeSpin;
    QRadioButton *m_iconGenerateRadio;
    QRadioButton *m_iconFileRadio;
    QLineEdit *m_iconPathEdit;

    // Compiler
    QComboBox *m_cpuCombo;
    QComboBox *m_fpuCombo;
    QSpinBox *m_optLevelSpin;
    QCheckBox *m_smallDataCheck;
    QCheckBox *m_debugSymbolsCheck;
    QPlainTextEdit *m_includePathsEdit;
    QPlainTextEdit *m_definesEdit;
    QPlainTextEdit *m_extraCFlagsEdit;

    // Assembler
    QCheckBox *m_kick1hunksCheck;
    QCheckBox *m_asmNoSymCheck;
    QPlainTextEdit *m_extraAsmFlagsEdit;

    // Linker
    QRadioButton *m_startupStdRadio;
    QRadioButton *m_startupMinStartRadio;
    QRadioButton *m_startupMinResRadio;
    QRadioButton *m_startupCustomRadio;
    QLineEdit *m_customStartupEdit;
    QListWidget *m_librariesList;
    QPlainTextEdit *m_extraLibDirsEdit;
    QCheckBox *m_stripCheck;
    QPlainTextEdit *m_extraLdFlagsEdit;

    // SDKs: one checkbox per registry entry, none of them mandatory.
    QWidget *m_extraSdksContainer;
    QList<QCheckBox *> m_extraSdkChecks; // parallel to m_extraSdkIds
    QStringList m_extraSdkIds;

    // Run
    QCheckBox *m_copyToSharedCheck;
    QLineEdit *m_sharedSubdirEdit;
    QLineEdit *m_emulatorConfigOverrideEdit;
    QPlainTextEdit *m_extraFsUaeArgsEdit;
};
