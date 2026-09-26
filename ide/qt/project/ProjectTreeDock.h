#pragma once

#include <QHash>
#include <QPoint>
#include <QSet>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class Project;

// Groups project files into C Sources / Assembler Sources / C Headers /
// Assembler Includes, with a real folder hierarchy underneath each group
// (built from each relative path's directory components -- not just a
// flat list of file names). Each leaf item stores its absolute path in
// Qt::UserRole; double-click/Enter emits fileActivated.
class ProjectTreeDock : public QWidget {
    Q_OBJECT
public:
    explicit ProjectTreeDock(QWidget *parent = nullptr);

    QTreeWidget *treeWidget() const { return m_tree; }

    void setProject(const Project *project);
    void clearProject();

    // Bold = the file in the currently focused editor tab; italic = has
    // unsaved changes (independently, and possibly for several files at
    // once -- unlike the single-file-editor Amiga MUI app, this app has
    // a real tabbed multi-file editor, so "dirty" isn't limited to just
    // the active file). Both update the existing tree items directly
    // (no full setProject() rebuild), so expand/collapse state and
    // selection survive a mere save/switch-tab. Per user feedback: the
    // tree previously never reflected either state at all.
    void setActiveFile(const QString &absPath);
    void setFileModified(const QString &absPath, bool modified);

signals:
    void fileActivated(const QString &path);
    // Emitted from the right-click context menu; MainWindow decides whether
    // to also delete the file from disk.
    void removeFileRequested(const QString &path);

private slots:
    void onItemActivated(QTreeWidgetItem *item, int column);
    void onContextMenuRequested(const QPoint &pos);

private:
    QTreeWidgetItem *groupItem(const QString &title);
    // Returns (creating on demand) the folder node for `dirPath` (relative
    // to the group root, "" meaning the group root itself), splitting on
    // '/' to build/reuse intermediate folder nodes. `cache` is scoped to a
    // single group -- the same relative folder name in two different
    // groups must not resolve to the same node.
    QTreeWidgetItem *folderItem(QTreeWidgetItem *groupRoot, QHash<QString, QTreeWidgetItem *> &cache,
                                 const QString &dirPath);
    void addLeaf(QTreeWidgetItem *groupRoot, QHash<QString, QTreeWidgetItem *> &cache,
                 const QString &relPath, const QString &absPath);

    // Recursively finds the leaf item whose Qt::UserRole path == absPath.
    QTreeWidgetItem *findItemByPath(QTreeWidgetItem *parent, const QString &absPath) const;
    // Sets item's bold/italic per m_activeFile/m_modifiedFiles; no-op for
    // group/folder items (they carry no path in Qt::UserRole).
    void applyItemStyle(QTreeWidgetItem *item);

    QTreeWidget *m_tree;
    QString m_activeFile;
    QSet<QString> m_modifiedFiles;
};
