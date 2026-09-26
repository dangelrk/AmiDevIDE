#include "IssueListModel.h"

#include <QFileInfo>
#include <QIcon>
#include <QStyle>
#include <QApplication>

IssueListModel::IssueListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int IssueListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_issues.size();
}

int IssueListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant IssueListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_issues.size())
        return {};

    const Issue &issue = m_issues.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case SeverityColumn:
            switch (issue.severity) {
            case IssueSeverity::Error: return QStringLiteral("Error");
            case IssueSeverity::Warning: return QStringLiteral("Warning");
            case IssueSeverity::Note: return QStringLiteral("Note");
            }
            break;
        case LocationColumn:
            return QStringLiteral("%1:%2").arg(QFileInfo(issue.file).fileName()).arg(issue.line);
        case MessageColumn:
            return issue.message;
        default:
            break;
        }
    }

    if (role == Qt::DecorationRole && index.column() == SeverityColumn) {
        QStyle *style = QApplication::style();
        switch (issue.severity) {
        case IssueSeverity::Error:
            return style->standardIcon(QStyle::SP_MessageBoxCritical);
        case IssueSeverity::Warning:
            return style->standardIcon(QStyle::SP_MessageBoxWarning);
        case IssueSeverity::Note:
            return style->standardIcon(QStyle::SP_MessageBoxInformation);
        }
    }

    return {};
}

QVariant IssueListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case SeverityColumn: return tr("Severity");
    case LocationColumn: return tr("Location");
    case MessageColumn: return tr("Message");
    default: return {};
    }
}

void IssueListModel::setIssues(const QVector<Issue> &issues)
{
    beginResetModel();
    m_issues = issues;
    endResetModel();
}

void IssueListModel::appendIssue(const Issue &issue)
{
    const int row = m_issues.size();
    beginInsertRows(QModelIndex(), row, row);
    m_issues.append(issue);
    endInsertRows();
}

void IssueListModel::clear()
{
    setIssues({});
}
