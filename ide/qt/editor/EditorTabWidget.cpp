#include "EditorTabWidget.h"
#include "EditorWidget.h"
#include "HighlighterFactory.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTabBar>
#include <QTextDocument>

EditorTabWidget::EditorTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);

    connect(this, &QTabWidget::tabCloseRequested, this, &EditorTabWidget::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &EditorTabWidget::onCurrentTabChanged);
}

EditorWidget *EditorTabWidget::editorForPath(const QString &path) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < count(); ++i) {
        auto *editor = qobject_cast<EditorWidget *>(widget(i));
        if (editor && QFileInfo(editor->filePath()).canonicalFilePath() == canonical)
            return editor;
    }
    return nullptr;
}

EditorWidget *EditorTabWidget::openFile(const QString &path)
{
    if (EditorWidget *existing = editorForPath(path)) {
        setCurrentWidget(existing);
        return existing;
    }

    auto *editor = new EditorWidget(this);
    if (!editor->loadFile(path)) {
        delete editor;
        return nullptr;
    }
    HighlighterFactory::attach(editor, path, m_projectLanguage);

    connect(editor, &EditorWidget::gotoDefinitionRequested, this, &EditorTabWidget::gotoDefinitionRequested);
    connect(editor, &EditorWidget::helpRequested, this, &EditorTabWidget::helpRequested);
    connect(editor, &EditorWidget::autodocSearchRequested, this, &EditorTabWidget::autodocSearchRequested);
    connect(editor, &EditorWidget::findReferencesRequested, this, &EditorTabWidget::findReferencesRequested);
    connect(editor, &EditorWidget::signatureHelpRequested, this, &EditorTabWidget::signatureHelpRequested);
    connect(editor, &EditorWidget::structInfoRequested, this, &EditorTabWidget::structInfoRequested);
    connect(editor, &EditorWidget::filePathChanged, this, &EditorTabWidget::fileContentsPersisted);
    connect(editor->document(), &QTextDocument::modificationChanged, this, &EditorTabWidget::onDocumentModifiedChanged);

    const int index = addTab(editor, QFileInfo(path).fileName());
    setCurrentIndex(index);
    emit fileContentsPersisted(path); // initial load already happened before filePathChanged was connected
    return editor;
}

EditorWidget *EditorTabWidget::newFile()
{
    auto *editor = new EditorWidget(this);
    connect(editor, &EditorWidget::gotoDefinitionRequested, this, &EditorTabWidget::gotoDefinitionRequested);
    connect(editor, &EditorWidget::helpRequested, this, &EditorTabWidget::helpRequested);
    connect(editor, &EditorWidget::autodocSearchRequested, this, &EditorTabWidget::autodocSearchRequested);
    connect(editor, &EditorWidget::findReferencesRequested, this, &EditorTabWidget::findReferencesRequested);
    connect(editor, &EditorWidget::signatureHelpRequested, this, &EditorTabWidget::signatureHelpRequested);
    connect(editor, &EditorWidget::structInfoRequested, this, &EditorTabWidget::structInfoRequested);
    connect(editor, &EditorWidget::filePathChanged, this, &EditorTabWidget::fileContentsPersisted);
    connect(editor->document(), &QTextDocument::modificationChanged, this, &EditorTabWidget::onDocumentModifiedChanged);

    const int index = addTab(editor, QStringLiteral("untitled"));
    setCurrentIndex(index);
    return editor;
}

EditorWidget *EditorTabWidget::currentEditor() const
{
    return qobject_cast<EditorWidget *>(currentWidget());
}

int EditorTabWidget::indexOfEditor(EditorWidget *editor) const
{
    for (int i = 0; i < count(); ++i) {
        if (widget(i) == editor)
            return i;
    }
    return -1;
}

void EditorTabWidget::updateTabTitle(EditorWidget *editor)
{
    const int index = indexOfEditor(editor);
    if (index < 0)
        return;

    const QString base = editor->filePath().isEmpty()
                              ? QStringLiteral("untitled")
                              : QFileInfo(editor->filePath()).fileName();
    const QString title = editor->document()->isModified() ? base + QStringLiteral(" *") : base;
    setTabText(index, title);
}

void EditorTabWidget::onDocumentModifiedChanged(bool /*modified*/)
{
    for (int i = 0; i < count(); ++i) {
        if (auto *editor = qobject_cast<EditorWidget *>(widget(i))) {
            updateTabTitle(editor);
            if (!editor->filePath().isEmpty())
                emit fileModifiedChanged(editor->filePath(), editor->document()->isModified());
        }
    }
}

void EditorTabWidget::onCurrentTabChanged(int index)
{
    if (auto *editor = qobject_cast<EditorWidget *>(widget(index)))
        emit currentFileChanged(editor->filePath());
    else
        emit currentFileChanged(QString());
}

bool EditorTabWidget::promptSaveIfModified(EditorWidget *editor)
{
    if (!editor->document()->isModified())
        return true;

    const QString name = editor->filePath().isEmpty()
                              ? QStringLiteral("untitled")
                              : QFileInfo(editor->filePath()).fileName();

    const auto answer = QMessageBox::question(
        this, tr("Save changes?"),
        tr("\"%1\" has unsaved changes. Save before closing?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        setCurrentWidget(editor);
        return saveCurrent();
    }
    return true;
}

void EditorTabWidget::onTabCloseRequested(int index)
{
    auto *editor = qobject_cast<EditorWidget *>(widget(index));
    if (!editor)
        return;
    if (!promptSaveIfModified(editor))
        return;

    removeTab(index);
    editor->deleteLater();
}

void EditorTabWidget::closeCurrentTab()
{
    const int index = currentIndex();
    if (index >= 0)
        onTabCloseRequested(index);
}

bool EditorTabWidget::closeAllTabsWithPrompt()
{
    while (count() > 0) {
        auto *editor = qobject_cast<EditorWidget *>(widget(0));
        if (!editor) {
            removeTab(0);
            continue;
        }
        if (!promptSaveIfModified(editor))
            return false;
        removeTab(0);
        editor->deleteLater();
    }
    return true;
}

bool EditorTabWidget::closeFileIfOpen(const QString &path, bool discardChanges)
{
    EditorWidget *editor = editorForPath(path);
    if (!editor)
        return true; // not open -- nothing to do

    if (!discardChanges && !promptSaveIfModified(editor))
        return false;

    const int index = indexOfEditor(editor);
    if (index >= 0)
        removeTab(index);
    editor->deleteLater();
    return true;
}

void EditorTabWidget::applyEditorSettingsToAllTabs()
{
    for (int i = 0; i < count(); ++i) {
        if (auto *editor = qobject_cast<EditorWidget *>(widget(i))) {
            editor->applyEditorSettings();
            HighlighterFactory::attach(editor, editor->filePath(), m_projectLanguage);
        }
    }
}

bool EditorTabWidget::saveCurrent()
{
    EditorWidget *editor = currentEditor();
    if (!editor)
        return false;

    if (editor->filePath().isEmpty())
        return saveCurrentAs();

    const bool ok = editor->saveFile();
    if (ok)
        updateTabTitle(editor);
    return ok;
}

bool EditorTabWidget::saveAllModified()
{
    bool allOk = true;
    for (int i = 0; i < count(); ++i) {
        auto *editor = qobject_cast<EditorWidget *>(widget(i));
        if (!editor || editor->filePath().isEmpty() || !editor->document()->isModified())
            continue;
        if (editor->saveFile())
            updateTabTitle(editor);
        else
            allOk = false;
    }
    return allOk;
}

bool EditorTabWidget::saveCurrentAs()
{
    EditorWidget *editor = currentEditor();
    if (!editor)
        return false;

    const QString startPath = editor->filePath().isEmpty() ? m_defaultSaveDirectory : editor->filePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("Save File As"), startPath);
    if (path.isEmpty())
        return false;

    const bool ok = editor->saveFile(path);
    if (ok) {
        HighlighterFactory::attach(editor, path, m_projectLanguage);
        updateTabTitle(editor);
    }
    return ok;
}
