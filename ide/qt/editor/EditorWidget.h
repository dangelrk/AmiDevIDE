#pragma once

#include "EditorTheme.h"

#include <QPlainTextEdit>
#include <QString>
#include <QStringList>

class QSyntaxHighlighter;
class QCompleter;
class LineNumberArea;

// Canonical Qt line-number-area editor (QPlainTextEdit + child LineNumberArea +
// setViewportMargins), extended with current-line highlight, a fixed-width
// font, and Amiga-IDE navigation signals (goto-definition / hothelp).
class EditorWidget : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit EditorWidget(QWidget *parent = nullptr);

    bool loadFile(const QString &path);
    bool saveFile(const QString &path = QString());

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

    // 1-based line the cursor is currently on (for asm local-label scoping).
    int currentLine() const;

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth() const;

    void setTabWidthChars(int chars);

    // Re-reads AppSettings (font family/size, tab width, theme) and applies
    // them to this editor. Called on construction and again whenever global
    // settings change; HighlighterFactory::attach() must be re-run alongside
    // this to re-color the syntax highlighter's rules for the new theme.
    void applyEditorSettings();

    // Takes ownership; deletes any previously attached highlighter.
    void setHighlighter(QSyntaxHighlighter *highlighter);

    // Replaces the completer's word list (keywords + SymbolDatabase names +
    // SFD function names, per plan section 4). Triggered with Ctrl+Space.
    void setCompletionWords(const QStringList &words);

    // Shows a floating call-tip (SFD/prototype signature) anchored below the
    // cursor. Triggered by typing '(' after a function name, or manually via
    // Ctrl+Shift+Space.
    void showCallTip(const QString &text);

signals:
    void gotoDefinitionRequested(const QString &symbol, int fromLine);
    void helpRequested(const QString &word);
    // F2: pre-fill and run the Autodoc search dock's search field with the
    // word under the cursor (as opposed to F1's silent single lookup).
    void autodocSearchRequested(const QString &word);
    // Shift+F3: project-wide "find all references" for the word under the
    // cursor (as opposed to F3's "jump to the one definition").
    void findReferencesRequested(const QString &word);
    // '(' typed after an identifier, or Ctrl+Shift+Space inside an existing
    // call: asks for a call-tip (SFD/prototype signature) for `word`.
    void signatureHelpRequested(const QString &word);
    // Shift+F1: struct/union member layout (name + byte offsets) for `word`,
    // whether it's the struct's own name or one of its members.
    void structInfoRequested(const QString &word);
    void filePathChanged(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();
    void insertCompletion(const QString &completion);

private:
    QString wordUnderCursor() const;
    QString textUnderCursorPrefix() const;
    // Scans left from the cursor for the nearest unmatched '(' and returns
    // the identifier immediately preceding it (e.g. cursor anywhere inside
    // "OpenLibrary(name, |" -> "OpenLibrary"). Empty if none found.
    QString enclosingCallIdentifier() const;

    LineNumberArea *m_lineNumberArea;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QString m_filePath;
    QCompleter *m_completer = nullptr;
    EditorTheme m_theme = EditorTheme::resolve(QStringLiteral("system"));
};
