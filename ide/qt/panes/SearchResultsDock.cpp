#include "SearchResultsDock.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kFileRole = Qt::UserRole;
constexpr int kLineRole = Qt::UserRole + 1;
constexpr int kColumnRole = Qt::UserRole + 2;
}

SearchResultsDock::SearchResultsDock(QWidget *parent)
    : QWidget(parent)
    , m_search(new QLineEdit(this))
    , m_caseSensitive(new QCheckBox(tr("Case"), this))
    , m_wholeWord(new QCheckBox(tr("Word"), this))
    , m_regex(new QCheckBox(tr("Regex"), this))
    , m_summaryLabel(new QLabel(this))
    , m_results(new QTreeWidget(this))
{
    m_search->setPlaceholderText(tr("Find in project files..."));
    connect(m_search, &QLineEdit::returnPressed, this, &SearchResultsDock::onSearchSubmitted);

    auto *toolRow = new QWidget(this);
    auto *toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->addWidget(m_search);
    toolLayout->addWidget(m_caseSensitive);
    toolLayout->addWidget(m_wholeWord);
    toolLayout->addWidget(m_regex);

    m_results->setColumnCount(2);
    m_results->setHeaderLabels({ tr("Location"), tr("Text") });
    m_results->setRootIsDecorated(false);
    m_results->setAlternatingRowColors(true);
    m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(m_results, &QTreeWidget::itemActivated, this, &SearchResultsDock::onResultActivated);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, &SearchResultsDock::onResultActivated);

    m_summaryLabel->setEnabled(false); // muted "status line" styling, matches disabled-item convention elsewhere

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(toolRow);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_results);
}

void SearchResultsDock::setProjectFiles(const QStringList &files)
{
    m_projectFiles = files;
}

void SearchResultsDock::focusSearchField()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void SearchResultsDock::showResults(const QList<ProjectSearch::SearchMatch> &matches, const QString &headerLabel)
{
    m_results->clear();

    for (const ProjectSearch::SearchMatch &match : matches) {
        auto *item = new QTreeWidgetItem(m_results);
        const QString location = QStringLiteral("%1:%2").arg(QFileInfo(match.file).fileName()).arg(match.line);
        item->setText(0, location);
        item->setText(1, match.lineText.trimmed());
        item->setToolTip(0, match.file);
        item->setData(0, kFileRole, match.file);
        item->setData(0, kLineRole, match.line);
        item->setData(0, kColumnRole, match.column);

        if (match.isDefinition) {
            QFont bold = item->font(0);
            bold.setBold(true);
            item->setFont(0, bold);
            item->setFont(1, bold);
        }
    }

    m_summaryLabel->setText(headerLabel);
    setWindowTitle(headerLabel);
}

void SearchResultsDock::onSearchSubmitted()
{
    const QString pattern = m_search->text();
    if (pattern.isEmpty())
        return;

    const auto matches = ProjectSearch::search(m_projectFiles, pattern, m_caseSensitive->isChecked(),
                                               m_wholeWord->isChecked(), m_regex->isChecked());
    showResults(matches, tr("%1 match(es) for \"%2\"").arg(matches.size()).arg(pattern));
}

void SearchResultsDock::onResultActivated(QTreeWidgetItem *item, int /*column*/)
{
    activateItem(item);
}

void SearchResultsDock::activateItem(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QString file = item->data(0, kFileRole).toString();
    const int line = item->data(0, kLineRole).toInt();
    const int column = item->data(0, kColumnRole).toInt();
    emit locationActivated(file, line, column);
}
