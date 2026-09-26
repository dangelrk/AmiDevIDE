#pragma once

#include <QWidget>
#include <QList>

class QTreeView;
class IssueListModel;
struct Issue;

class IssuesPane : public QWidget {
    Q_OBJECT
public:
    explicit IssuesPane(QWidget *parent = nullptr);

    void setIssues(const QList<Issue> &issues);
    void appendIssue(const Issue &issue);
    void clear();
    int count() const;

    // Navigation: -1 if there are no issues.
    bool gotoNext();
    bool gotoPrevious();

signals:
    void locationActivated(const QString &file, int line, const QString &message);

private slots:
    void onActivated(const QModelIndex &index);

private:
    void activateRow(int row);

    QTreeView *m_view;
    IssueListModel *m_model;
    int m_currentRow = -1;
};
