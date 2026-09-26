#pragma once

#include "search/ProjectSearch.h"

#include <QList>
#include <QWidget>

class QLineEdit;
class QCheckBox;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

// "Find in Project..." UI, and the shared results view for "Find All
// References" (MainWindow builds the match list itself for that one -- via
// ProjectSearch::search() with wholeWord=true plus a SymbolDatabase
// cross-check for isDefinition -- and calls showResults() directly,
// bypassing this dock's own search field/checkboxes).
class SearchResultsDock : public QWidget {
    Q_OBJECT
public:
    explicit SearchResultsDock(QWidget *parent = nullptr);

    // Files searched when the user submits the dock's own search field
    // (Find in Project...). Refreshed by MainWindow right before showing the
    // dock, so file-add/remove since the last search is always picked up.
    void setProjectFiles(const QStringList &files);

    // Renders `matches` under `headerLabel` (e.g. "12 references to Foo").
    // Rows whose isDefinition is set are shown in bold.
    void showResults(const QList<ProjectSearch::SearchMatch> &matches, const QString &headerLabel);

    // Focuses and selects the search field's text (for a global shortcut).
    void focusSearchField();

signals:
    void locationActivated(const QString &file, int line, int column);

private slots:
    void onSearchSubmitted();
    void onResultActivated(QTreeWidgetItem *item, int column);

private:
    void activateItem(QTreeWidgetItem *item);

    QStringList m_projectFiles;

    QLineEdit *m_search;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWord;
    QCheckBox *m_regex;
    QLabel *m_summaryLabel;
    QTreeWidget *m_results;
};
