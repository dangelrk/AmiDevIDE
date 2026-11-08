#include "VcsDock.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

static QString kindLabel(char kind)
{
    switch (kind) {
    case 'N': return QObject::tr("new");
    case 'D': return QObject::tr("deleted");
    default:  return QObject::tr("modified");
    }
}

VcsDock::VcsDock(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    // page 0: a message (no amivcs, no project, no repository)
    auto *infoPage = new QWidget(m_stack);
    auto *infoLayout = new QVBoxLayout(infoPage);
    m_info = new QLabel(infoPage);
    m_info->setWordWrap(true);
    m_initButton = new QPushButton(tr("Create Repository Here"), infoPage);
    infoLayout->addWidget(m_info);
    infoLayout->addWidget(m_initButton);
    infoLayout->addStretch(1);
    connect(m_initButton, &QPushButton::clicked, this, &VcsDock::initRequested);
    m_stack->addWidget(infoPage);

    // page 1: the working view
    auto *page = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout;
    m_branch = new QLabel(page);
    m_refresh = new QPushButton(tr("Refresh"), page);
    top->addWidget(m_branch, 1);
    top->addWidget(m_refresh);
    layout->addLayout(top);

    // a merge that stopped on conflicts
    m_mergeBox = new QFrame(page);
    m_mergeBox->setFrameShape(QFrame::StyledPanel);
    m_mergeBox->setStyleSheet(QStringLiteral("QFrame { background: rgba(230, 160, 0, 60); }"));
    auto *mergeLayout = new QVBoxLayout(m_mergeBox);
    m_mergeLabel = new QLabel(m_mergeBox);
    m_mergeLabel->setWordWrap(true);
    mergeLayout->addWidget(m_mergeLabel);
    m_conflicts = new QListWidget(m_mergeBox);
    m_conflicts->setObjectName(QStringLiteral("conflicts"));
    m_conflicts->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_conflicts->setToolTip(tr("Double-click opens the file: fix the <<<<<<< ======= >>>>>>> blocks, "
                               "save, then Mark Resolved"));
    mergeLayout->addWidget(m_conflicts);
    auto *mergeRow = new QHBoxLayout;
    m_takeOurs = new QPushButton(tr("Take Ours"), m_mergeBox);
    m_takeTheirs = new QPushButton(tr("Take Theirs"), m_mergeBox);
    m_resolved = new QPushButton(tr("Mark Resolved"), m_mergeBox);
    m_takeOurs->setToolTip(tr("Keep this branch's version of the whole file"));
    m_takeTheirs->setToolTip(tr("Use the merged branch's version of the whole file"));
    m_resolved->setToolTip(tr("The file is fixed -- stage it as it is now"));
    mergeRow->addWidget(m_takeOurs);
    mergeRow->addWidget(m_takeTheirs);
    mergeRow->addWidget(m_resolved);
    mergeLayout->addLayout(mergeRow);
    m_abortMerge = new QPushButton(tr("Abort Merge"), m_mergeBox);
    m_abortMerge->setToolTip(tr("Give up the merge: every file goes back to how it was before"));
    mergeLayout->addWidget(m_abortMerge);
    m_mergeBox->hide();
    layout->addWidget(m_mergeBox, 2);

    layout->addWidget(new QLabel(tr("Changes"), page));
    m_unstaged = new QListWidget(page);
    m_unstaged->setObjectName(QStringLiteral("changes"));
    m_unstaged->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_unstaged, 2);
    auto *row1 = new QHBoxLayout;
    m_stage = new QPushButton(tr("Stage"), page);
    m_stageAll = new QPushButton(tr("Stage All"), page);
    m_diff = new QPushButton(tr("Diff"), page);
    m_diff->setToolTip(tr("Show what changed in the selected file"));
    row1->addWidget(m_stage);
    row1->addWidget(m_stageAll);
    row1->addWidget(m_diff);
    row1->addStretch(1);
    layout->addLayout(row1);

    layout->addWidget(new QLabel(tr("Staged"), page));
    m_staged = new QListWidget(page);
    m_staged->setObjectName(QStringLiteral("staged"));
    m_staged->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_staged, 1);
    auto *row2 = new QHBoxLayout;
    m_unstage = new QPushButton(tr("Unstage"), page);
    row2->addWidget(m_unstage);
    row2->addStretch(1);
    layout->addLayout(row2);

    m_message = new QPlainTextEdit(page);
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setMaximumHeight(90);
    layout->addWidget(m_message);
    auto *row3 = new QHBoxLayout;
    m_all = new QCheckBox(tr("Stage all changes first"), page);
    m_commit = new QPushButton(tr("Commit"), page);
    row3->addWidget(m_all, 1);
    row3->addWidget(m_commit);
    layout->addLayout(row3);
    m_stack->addWidget(page);

    connect(m_refresh, &QPushButton::clicked, this, &VcsDock::refreshRequested);
    connect(m_stage, &QPushButton::clicked, this, [this] {
        const QStringList p = selectedPaths(m_unstaged);
        if (!p.isEmpty()) emit stageRequested(p);
    });
    connect(m_stageAll, &QPushButton::clicked, this, &VcsDock::stageAllRequested);
    connect(m_unstage, &QPushButton::clicked, this, [this] {
        const QStringList p = selectedPaths(m_staged);
        if (!p.isEmpty()) emit unstageRequested(p);
    });
    connect(m_commit, &QPushButton::clicked, this, [this] {
        emit commitRequested(m_message->toPlainText().trimmed(), m_all->isChecked());
    });
    connect(m_diff, &QPushButton::clicked, this, [this] {
        // the selected file of either list; staged if it was picked there
        QListWidget *list = m_lastList ? m_lastList : m_unstaged;
        QListWidgetItem *item = list->currentItem();
        if (!item) item = (list == m_staged ? m_unstaged : m_staged)->currentItem();
        if (item) emit diffRequested(item->data(Qt::UserRole).toString(), item->listWidget() == m_staged);
    });
    connect(m_abortMerge, &QPushButton::clicked, this, &VcsDock::abortMergeRequested);
    connect(m_takeOurs, &QPushButton::clicked, this, [this] {
        const QStringList p = selectedPaths(m_conflicts);
        if (!p.isEmpty()) emit resolveRequested(p, false);
    });
    connect(m_takeTheirs, &QPushButton::clicked, this, [this] {
        const QStringList p = selectedPaths(m_conflicts);
        if (!p.isEmpty()) emit resolveRequested(p, true);
    });
    connect(m_resolved, &QPushButton::clicked, this, [this] {
        const QStringList p = selectedPaths(m_conflicts);
        if (!p.isEmpty()) emit markResolvedRequested(p);
    });
    connect(m_conflicts, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit fileActivated(item->data(Qt::UserRole).toString());
    });
    for (QListWidget *l : { m_unstaged, m_staged }) {
        connect(l, &QListWidget::itemPressed, this, [this, l] { m_lastList = l; });
        connect(l, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
            emit fileActivated(item->data(Qt::UserRole).toString());
        });
    }
    showNoProject();
}

void VcsDock::showUnavailable(const QString &reason)
{
    m_info->setText(reason);
    m_initButton->hide();
    m_stack->setCurrentIndex(0);
}

void VcsDock::showNoProject()
{
    m_info->setText(tr("No project open."));
    m_initButton->hide();
    m_stack->setCurrentIndex(0);
}

void VcsDock::showNoRepository()
{
    m_info->setText(tr("This project is not under version control yet."));
    m_initButton->show();
    m_stack->setCurrentIndex(0);
}

void VcsDock::showStatus(const AideVcsResult *s)
{
    m_stack->setCurrentIndex(1);
    if (s->haveHead)
        m_branch->setText(s->detached ? tr("Detached at %1").arg(QString::fromUtf8(s->branch).left(8))
                                      : tr("Branch: %1").arg(QString::fromUtf8(s->branch)));
    m_unstaged->clear();
    m_staged->clear();
    for (size_t i = 0; i < s->changeCount; ++i) {
        const AideVcsChange &c = s->changes[i];
        const QString path = QString::fromUtf8(c.path);
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(kindLabel(c.kind), -9).arg(path));
        item->setData(Qt::UserRole, path);
        (c.staged == 'S' ? m_staged : m_unstaged)->addItem(item);
    }
    m_conflicts->clear();
    m_merging = s->merging != 0;
    m_conflictsOpen = s->merging ? s->conflictsOpen : 0;
    for (size_t i = 0; i < s->conflictCount; ++i) {
        const AideVcsConflict &c = s->conflicts[i];
        const QString path = QString::fromUtf8(c.path);
        QString text = QStringLiteral("%1  %2").arg(QString::fromUtf8(c.kind), -13).arg(path);
        if (c.state != 'U') text += tr("  (resolved)");
        else if (c.theirsFile) text += tr("  (+ .theirs)");
        auto *item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, path);
        if (c.state != 'U') item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        m_conflicts->addItem(item);
    }
    if (m_merging) {
        const QString label = QString::fromUtf8(s->mergeLabel ? s->mergeLabel : "");
        m_mergeLabel->setText(m_conflictsOpen
            ? tr("Merging '%1': %2 conflict(s). Fix each file (double-click) and mark it resolved, "
                 "or take one side -- then commit.").arg(label).arg(m_conflictsOpen)
            : tr("Merging '%1': all conflicts resolved. Commit to finish the merge.").arg(label));
        if (m_message->toPlainText().trimmed().isEmpty())
            m_message->setPlainText(QStringLiteral("Merge branch '%1'").arg(label));
    }
    m_mergeBox->setVisible(m_merging);
    setBusy(m_busy);
}

void VcsDock::setBusy(bool busy)
{
    m_busy = busy;
    for (QWidget *w : std::initializer_list<QWidget *>{ m_stage, m_stageAll, m_unstage, m_commit, m_initButton,
                                                        m_abortMerge, m_takeOurs, m_takeTheirs, m_resolved })
        w->setEnabled(!busy);
    if (!busy && m_conflictsOpen > 0) m_commit->setEnabled(false);   // amivcs would refuse
}

void VcsDock::clearMessage()
{
    m_message->clear();
    m_all->setChecked(false);
}

QStringList VcsDock::selectedPaths(QListWidget *list) const
{
    QStringList out;
    for (QListWidgetItem *item : list->selectedItems())
        out << item->data(Qt::UserRole).toString();
    return out;
}
