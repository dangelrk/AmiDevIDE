#include "SymbolsDock.h"

#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString kindGroupTitle(SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Function:
    case SymbolKind::Prototype:
        return QObject::tr("Functions");
    case SymbolKind::Define:
    case SymbolKind::EquConstant:
        return QObject::tr("Defines / Constants");
    case SymbolKind::Variable:
        return QObject::tr("Variables");
    case SymbolKind::Typedef:
    case SymbolKind::Struct:
    case SymbolKind::Union:
    case SymbolKind::Enum:
        return QObject::tr("Types");
    case SymbolKind::Label:
    case SymbolKind::LocalLabel:
        return QObject::tr("Labels");
    case SymbolKind::AsmMacro:
        return QObject::tr("Macros");
    case SymbolKind::StructMember:
        // Not expected here -- MainWindow::refreshSymbolsDock() filters
        // these out before calling setSymbols() (there can be dozens per
        // struct; they're reached via Shift+F1 instead). Handled anyway so
        // the switch stays exhaustive.
        return QObject::tr("Other");
    }
    return QObject::tr("Other");
}

QString symbolDisplayText(const Symbol &s)
{
    if (s.kind == SymbolKind::LocalLabel && !s.scope.isEmpty())
        return QStringLiteral("%1 (%2)").arg(s.name, s.scope);
    return s.name;
}
}

SymbolsDock::SymbolsDock(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
{
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemActivated, this, &SymbolsDock::onItemActivated);
}

QTreeWidgetItem *SymbolsDock::groupItem(const QString &title)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(0) == title)
            return m_tree->topLevelItem(i);
    }
    auto *item = new QTreeWidgetItem(m_tree, { title });
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    return item;
}

void SymbolsDock::setSymbols(const QList<Symbol> &symbols)
{
    m_tree->clear();

    QList<Symbol> sorted = symbols;
    std::sort(sorted.begin(), sorted.end(), [](const Symbol &a, const Symbol &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    for (const Symbol &s : sorted) {
        auto *parentItem = groupItem(kindGroupTitle(s.kind));
        auto *item = new QTreeWidgetItem(parentItem, { symbolDisplayText(s) });
        item->setData(0, Qt::UserRole, s.file);
        item->setData(0, Qt::UserRole + 1, s.line);
    }

    m_tree->expandAll();
}

void SymbolsDock::onItemActivated(QTreeWidgetItem *item, int /*column*/)
{
    const QString file = item->data(0, Qt::UserRole).toString();
    if (file.isEmpty())
        return;
    emit symbolActivated(file, item->data(0, Qt::UserRole + 1).toInt());
}
