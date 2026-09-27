#pragma once

#include <QWidget>

class QTextBrowser;
class QLineEdit;
class QToolButton;
class QListWidget;
class QListWidgetItem;

// F1-flow display: AutodocIndex/SfdIndex/SymbolDatabase hits are shown as
// plain-text/HTML pages with title-based back/forward history; NAME/SYNOPSIS
// etc. section headers are bolded, and Word() references become internal
// autodoc:// links (plan section 9).
class HelpDock : public QWidget {
    Q_OBJECT
public:
    explicit HelpDock(QWidget *parent = nullptr);

    // Displays raw autodoc text: bolds section headers, linkifies Word()
    // occurrences as autodoc:// links, and pushes the page onto history.
    void showAutodocEntry(const QString &title, const QString &rawText);

    // Displays a simple info card (e.g. for a SymbolDatabase hit).
    void showPlainText(const QString &title, const QString &text);

    // Shows a clickable list of candidate names (wildcard search results,
    // e.g. "Open*"); clicking/activating one re-emits autodocRequested(name).
    // An empty list shows a "no matches" placeholder.
    void showCandidateList(const QStringList &names);

    // Focuses and selects the search field's text (for a global shortcut).
    void focusSearch();

    // F2 in the editor: pre-fills the search field with `word` and runs the
    // same submit path as pressing Enter (so wildcard/exact/case-insensitive
    // resolution behaves identically to a manual search).
    void prefillAndSearch(const QString &word);

signals:
    // Emitted when the user follows an autodoc:// link, types a query, or
    // picks a name from the candidate list.
    void autodocRequested(const QString &functionName);

private slots:
    void onAnchorClicked(const QUrl &url);
    void onSearchSubmitted();
    void onCandidateActivated(QListWidgetItem *item);
    void goBack();
    void goForward();
    void searchSelection();
    void showBrowserContextMenu(const QPoint &pos);

private:
    // Help query for the browser's selection, or for the word at `pos`
    // (viewport coordinates) when nothing is selected and `pos` is given.
    QString queryAt(const QPoint *pos) const;
    void searchFor(const QString &query);

    QString linkify(const QString &escapedHtml) const;
    void pushHistory(const QString &title, const QString &html);
    void renderHtml(const QString &html);

    QLineEdit *m_search;
    QListWidget *m_resultsList;
    QTextBrowser *m_browser;
    QToolButton *m_backButton;
    QToolButton *m_forwardButton;
    QToolButton *m_searchSelectionButton;

    QList<QPair<QString, QString>> m_history; // {title, html}
    int m_historyIndex = -1;
};
