#pragma once

#include "Issue.h"

#include <QAbstractTableModel>
#include <QList>

class IssueListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { SeverityColumn = 0, LocationColumn, MessageColumn, ColumnCount };

    explicit IssueListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setIssues(const QList<Issue> &issues);
    // Inserts a single issue at the end without resetting the whole
    // model -- setIssues() rebuilds every row, which is O(n^2) over a
    // build with many diagnostics since it's called once per issue as
    // they stream in (see BuildManager::issueFound).
    void appendIssue(const Issue &issue);
    void clear();
    const Issue &issueAt(int row) const { return m_issues.at(row); }
    int count() const { return m_issues.size(); }

private:
    QList<Issue> m_issues;
};
