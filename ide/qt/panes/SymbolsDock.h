#pragma once

#include "index/Symbol.h"

#include <QList>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

class SymbolsDock : public QWidget {
    Q_OBJECT
public:
    explicit SymbolsDock(QWidget *parent = nullptr);

    QTreeWidget *treeWidget() const { return m_tree; }

    void setSymbols(const QList<Symbol> &symbols);

signals:
    void symbolActivated(const QString &file, int line);

private slots:
    void onItemActivated(QTreeWidgetItem *item, int column);

private:
    QTreeWidgetItem *groupItem(const QString &title);

    QTreeWidget *m_tree;
};
