#pragma once

#include "project/Project.h"

#include <QTabWidget>
#include <QString>

class EditorWidget;

// Tab container for EditorWidgets: modified-marker in tab text, dedupes
// already-open files, prompts to save on close.
class EditorTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit EditorTabWidget(QWidget *parent = nullptr);

    EditorWidget *openFile(const QString &path);
    EditorWidget *newFile();
    EditorWidget *currentEditor() const;
    EditorWidget *editorForPath(const QString &path) const;

    // Returns false if the user cancelled a save prompt.
    bool closeAllTabsWithPrompt();

    // Saves every open tab that has a real on-disk path and unsaved
    // changes -- no prompts, no "Save As" for untitled tabs (those skip
    // silently, same as they aren't part of the project's source list
    // yet either way). Called before a build so it always compiles
    // what's actually in the editor, not a stale on-disk copy. Returns
    // false if any save failed (e.g. permissions), true otherwise
    // (including "nothing needed saving").
    bool saveAllModified();

    // Closes the tab for `path` if it's open. When discardChanges is false
    // and the tab has unsaved edits, prompts to save first (returns false if
    // the user cancels); discardChanges skips the prompt (used when the file
    // is about to be deleted from disk, so saving would be pointless).
    bool closeFileIfOpen(const QString &path, bool discardChanges = false);

    // Starting directory for "Save As" on untitled tabs (the project root
    // when a project is open, so new files don't default to some
    // unrelated last-used directory).
    void setDefaultSaveDirectory(const QString &dir) { m_defaultSaveDirectory = dir; }
    // Decides how ".h" is highlighted -- see HighlighterFactory.
    void setProjectLanguage(ProjectLanguage language) { m_projectLanguage = language; }

    // Re-applies AppSettings (font, tab width, theme) to every open editor
    // and re-attaches its highlighter so token colors pick up the new theme.
    // Called after the global Settings dialog is accepted.
    void applyEditorSettingsToAllTabs();

signals:
    void gotoDefinitionRequested(const QString &symbol, int fromLine);
    void helpRequested(const QString &word);
    void autodocSearchRequested(const QString &word);
    void findReferencesRequested(const QString &word);
    void signatureHelpRequested(const QString &word);
    void structInfoRequested(const QString &word);
    void currentFileChanged(const QString &path);
    // Fires whenever a file finishes loading or saving (i.e. its on-disk
    // contents are known-good) -- the indexer's cue to (re)parse it.
    void fileContentsPersisted(const QString &path);
    // Fires whenever any open tab's modified state changes -- the
    // project tree's cue to italicize/un-italicize that file.
    void fileModifiedChanged(const QString &path, bool modified);

public slots:
    bool saveCurrent();
    bool saveCurrentAs();
    void closeCurrentTab();

private slots:
    void onTabCloseRequested(int index);
    void onDocumentModifiedChanged(bool modified);
    void onCurrentTabChanged(int index);

private:
    void updateTabTitle(EditorWidget *editor);
    // Returns false if the user cancelled.
    bool promptSaveIfModified(EditorWidget *editor);
    int indexOfEditor(EditorWidget *editor) const;

    QString m_defaultSaveDirectory;
    ProjectLanguage m_projectLanguage = ProjectLanguage::C;
};
