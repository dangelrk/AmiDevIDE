#include "IssuesPane.h"
#include "IssueListModel.h"
#include "Issue.h"

#include <QHeaderView>
#include <QTreeView>
#include <QVBoxLayout>

IssuesPane::IssuesPane(QWidget *parent)
    : QWidget(parent)
    , m_view(new QTreeView(this))
    , m_model(new IssueListModel(this))
{
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->header()->setSectionResizeMode(IssueListModel::MessageColumn, QHeaderView::Stretch);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    connect(m_view, &QTreeView::activated, this, &IssuesPane::onActivated);
    connect(m_view, &QTreeView::doubleClicked, this, &IssuesPane::onActivated);
}

void IssuesPane::setIssues(const QList<Issue> &issues)
{
    m_model->setIssues(issues);
    m_currentRow = -1;
}

void IssuesPane::appendIssue(const Issue &issue)
{
    m_model->appendIssue(issue);
}

void IssuesPane::clear()
{
    m_model->clear();
    m_currentRow = -1;
}

int IssuesPane::count() const
{
    return m_model->count();
}

void IssuesPane::onActivated(const QModelIndex &index)
{
    activateRow(index.row());
}

void IssuesPane::activateRow(int row)
{
    if (row < 0 || row >= m_model->count())
        return;
    m_currentRow = row;
    const Issue &issue = m_model->issueAt(row);
    m_view->setCurrentIndex(m_model->index(row, 0));
    emit locationActivated(issue.file, issue.line, issue.message);
}

bool IssuesPane::gotoNext()
{
    if (m_model->count() == 0)
        return false;
    activateRow((m_currentRow + 1) % m_model->count());
    return true;
}

bool IssuesPane::gotoPrevious()
{
    if (m_model->count() == 0)
        return false;
    const int n = m_model->count();
    activateRow((m_currentRow - 1 + n) % n);
    return true;
}
