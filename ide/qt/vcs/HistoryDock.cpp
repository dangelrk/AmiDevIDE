#include "HistoryDock.h"

#include <QDateTime>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Lane graph, the same drawing as AmiVCS's amivcs-gui-qt GraphDelegate.
class GraphDelegate : public QStyledItemDelegate {
public:
    GraphDelegate(const HistoryDock *dock, QObject *parent)
        : QStyledItemDelegate(parent), m_dock(dock) {}

    static const int kLane = 14;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->fillRect(option.rect, (option.state & QStyle::State_Selected) ? option.palette.highlight()
                                                                               : option.palette.base());
        const AideVcsGraphRow *r = m_dock->graphRow(index.row());
        if (!r) { painter->restore(); return; }
        const int left = option.rect.left();
        const int top = option.rect.top();
        const int mid = top + option.rect.height() / 2;
        const int bottom = option.rect.bottom() + 1;
        painter->setPen(QPen(option.palette.color(QPalette::Mid), 2));
        for (int i = 0; i < r->nActiveLanes; ++i) {
            if (r->activeLanes[i] == r->lane) continue;
            const int x = left + r->activeLanes[i] * kLane + kLane / 2;
            painter->drawLine(x, top, x, bottom);
        }
        const int x0 = left + r->lane * kLane + kLane / 2;
        if (const AideVcsGraphRow *prev = m_dock->graphRow(index.row() - 1)) {
            for (int i = 0; i < prev->nActiveLanes; ++i)
                if (prev->activeLanes[i] == r->lane) { painter->drawLine(x0, top, x0, mid); break; }
        }
        for (int i = 0; i < r->nParentLanes; ++i)
            painter->drawLine(x0, mid, left + r->parentLanes[i] * kLane + kLane / 2, bottom);
        painter->setPen(Qt::NoPen);
        painter->setBrush(option.palette.color(QPalette::Highlight));
        painter->drawEllipse(QPoint(x0, mid), 4, 4);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(kLane * std::max(m_dock->laneCount(), 1) + 8, 22);
    }

private:
    const HistoryDock *m_dock;
};

} // namespace

HistoryDock::HistoryDock(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({ tr("Graph"), tr("Commit"), tr("Author"), tr("Date (UTC)"), tr("Message") });
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setItemDelegateForColumn(0, new GraphDelegate(this, m_table));
    layout->addWidget(m_table);
}

HistoryDock::~HistoryDock()
{
    clear();
}

void HistoryDock::clear()
{
    if (m_rows && m_log)
        aide_vcs_graph_free(m_rows, m_log->r.commitCount);
    m_rows = nullptr;
    m_maxLanes = 0;
    m_log.reset();
    m_table->setRowCount(0);
}

const AideVcsGraphRow *HistoryDock::graphRow(int row) const
{
    if (!m_rows || !m_log || row < 0 || static_cast<size_t>(row) >= m_log->r.commitCount)
        return nullptr;
    return &m_rows[row];
}

void HistoryDock::showLog(VcsResultPtr log)
{
    clear();
    m_log = std::move(log);
    if (!aide_vcs_graph(&m_log->r, &m_rows, &m_maxLanes)) {
        m_rows = nullptr;
        m_maxLanes = 0;
    }
    const int n = static_cast<int>(m_log->r.commitCount);
    m_table->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        const AideVcsCommit &c = m_log->r.commits[i];
        QString message = QString::fromUtf8(c.message).section(QLatin1Char('\n'), 0, 0);
        const QString refs = QString::fromUtf8(c.refs);
        if (!refs.isEmpty())
            message = QStringLiteral("[%1] %2").arg(refs.split(QLatin1Char(',')).join(QStringLiteral(", ")), message);
        // the Amiga has no time zone: commit times are shown as UTC
        const QString date = QDateTime::fromSecsSinceEpoch(c.mtime, QTimeZone::UTC)
                                 .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        const QStringList cells{ QString(), QString::fromUtf8(c.oid).left(8), QString::fromUtf8(c.author), date, message };
        for (int col = 0; col < cells.size(); ++col) {
            auto *item = new QTableWidgetItem(cells[col]);
            if (col == 4) item->setToolTip(QString::fromUtf8(c.message));
            m_table->setItem(i, col, item);
        }
    }
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(0, GraphDelegate::kLane * std::max(m_maxLanes, 1) + 8);
}
