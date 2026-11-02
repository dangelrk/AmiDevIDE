#include "ProjectTreeDock.h"
#include "Project.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

ProjectTreeDock::ProjectTreeDock(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
{
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemActivated, this, &ProjectTreeDock::onItemActivated);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ProjectTreeDock::onContextMenuRequested);
}

void ProjectTreeDock::onContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item)
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) // a group header or folder node, not a file
        return;

    QMenu menu(this);
    QAction *removeAction = menu.addAction(tr("Remove from Project..."));
    connect(removeAction, &QAction::triggered, this, [this, path] { emit removeFileRequested(path); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void ProjectTreeDock::onItemActivated(QTreeWidgetItem *item, int /*column*/)
{
    const QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty())
        emit fileActivated(path);
}

QTreeWidgetItem *ProjectTreeDock::groupItem(const QString &title)
{
    auto *item = new QTreeWidgetItem(m_tree, { title });
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    return item;
}

QTreeWidgetItem *ProjectTreeDock::folderItem(QTreeWidgetItem *groupRoot, QHash<QString, QTreeWidgetItem *> &cache,
                                              const QString &dirPath)
{
    if (dirPath.isEmpty())
        return groupRoot;
    if (QTreeWidgetItem *existing = cache.value(dirPath))
        return existing;

    const int slash = dirPath.lastIndexOf(QLatin1Char('/'));
    QTreeWidgetItem *parent = slash < 0 ? groupRoot : folderItem(groupRoot, cache, dirPath.left(slash));
    const QString name = slash < 0 ? dirPath : dirPath.mid(slash + 1);

    auto *item = new QTreeWidgetItem(parent, { name });
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    cache.insert(dirPath, item);
    return item;
}

void ProjectTreeDock::addLeaf(QTreeWidgetItem *groupRoot, QHash<QString, QTreeWidgetItem *> &cache,
                               const QString &relPath, const QString &absPath)
{
    const QFileInfo info(relPath);
    // QFileInfo::path() returns "." for a name with no directory part --
    // that must map to the group root, not a literal "." folder node.
    const QString dir = info.path() == QLatin1String(".") ? QString() : info.path();
    QTreeWidgetItem *parent = folderItem(groupRoot, cache, dir);
    auto *item = new QTreeWidgetItem(parent, { info.fileName() });
    item->setData(0, Qt::UserRole, absPath);
    item->setData(0, Qt::UserRole + 1, info.fileName());
    applyItemStyle(item);
}

QTreeWidgetItem *ProjectTreeDock::findItemByPath(QTreeWidgetItem *parent, const QString &absPath) const
{
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem *child = parent->child(i);
        if (child->data(0, Qt::UserRole).toString() == absPath)
            return child;
        if (QTreeWidgetItem *found = findItemByPath(child, absPath))
            return found;
    }
    return nullptr;
}

void ProjectTreeDock::applyItemStyle(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) // a group/folder node, not a file
        return;
    QFont font = item->font(0);
    font.setBold(path == m_activeFile);
    font.setItalic(m_modifiedFiles.contains(path));
    item->setFont(0, font);

    const QString name = item->data(0, Qt::UserRole + 1).toString();
    const QChar state = m_vcsStates.value(path);
    if (state.isNull()) {
        item->setText(0, name);
        item->setToolTip(0, QString());
    } else {
        item->setText(0, name + QStringLiteral("  ") + state);
        item->setToolTip(0, state == QLatin1Char('S') ? tr("Staged for commit")
                          : state == QLatin1Char('N') ? tr("New, not under version control yet")
                          : state == QLatin1Char('D') ? tr("Deleted")
                                                      : tr("Modified since the last commit"));
    }
}

void ProjectTreeDock::setActiveFile(const QString &absPath)
{
    if (m_activeFile == absPath)
        return;
    const QString previous = m_activeFile;
    m_activeFile = absPath;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        if (!previous.isEmpty())
            applyItemStyle(findItemByPath(group, previous));
        applyItemStyle(findItemByPath(group, absPath));
    }
}

void ProjectTreeDock::setFileModified(const QString &absPath, bool modified)
{
    if (modified == m_modifiedFiles.contains(absPath))
        return;
    if (modified)
        m_modifiedFiles.insert(absPath);
    else
        m_modifiedFiles.remove(absPath);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        applyItemStyle(findItemByPath(m_tree->topLevelItem(i), absPath));
}

void ProjectTreeDock::setVcsStates(const QHash<QString, QChar> &states)
{
    const QHash<QString, QChar> old = m_vcsStates;
    m_vcsStates = states;
    QSet<QString> touched;
    for (auto it = old.begin(); it != old.end(); ++it) touched.insert(it.key());
    for (auto it = states.begin(); it != states.end(); ++it) touched.insert(it.key());
    for (const QString &p : touched)
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            applyItemStyle(findItemByPath(m_tree->topLevelItem(i), p));
}

void ProjectTreeDock::clearProject()
{
    m_tree->clear();
}

void ProjectTreeDock::setProject(const Project *project)
{
    m_tree->clear();
    if (!project)
        return;

    // ACE BASIC uses ".h" for its headers just like C does, and the
    // preprocessor-only ones (include/ace/limits.h, include/funcs/*.h) are
    // textually indistinguishable from C. Guessing from content would get
    // those wrong -- but it is not needed: a project is either C or ACE
    // BASIC (the two runtimes cannot be linked together), so the project's
    // language settles it. Only the group actually in use is shown.
    const bool basic = project->language == ProjectLanguage::Basic;
    const bool asmOnly = project->language == ProjectLanguage::Asm;

    // A pure assembler project has no second source group to show, and its
    // headers are .i includes, which already have their own group.
    auto *mainSourcesGroup = asmOnly ? nullptr
                          : groupItem(basic ? tr("BASIC Sources") : tr("C Sources"));
    auto *asmSourcesGroup = groupItem(tr("Assembler Sources"));
    auto *mainHeadersGroup = asmOnly ? nullptr
                           : groupItem(basic ? tr("BASIC Headers") : tr("C Headers"));
    auto *asmIncludesGroup = groupItem(tr("Assembler Includes"));

    QHash<QString, QTreeWidgetItem *> mainSourcesFolders;
    QHash<QString, QTreeWidgetItem *> asmSourcesFolders;
    QHash<QString, QTreeWidgetItem *> mainHeadersFolders;
    QHash<QString, QTreeWidgetItem *> asmIncludesFolders;

    for (const SourceFile &src : project->sources) {
        const QString suffix = QFileInfo(src.path).suffix().toLower();
        // A BASIC project can still carry hand-written assembler: ACE
        // supports EXTERNAL functions in .s files (see examples/externfunc).
        const bool isAsm = suffix == QLatin1String("s") || suffix == QLatin1String("asm");
        if (isAsm || !mainSourcesGroup)
            addLeaf(asmSourcesGroup, asmSourcesFolders, src.path, project->absolutePath(src.path));
        else
            addLeaf(mainSourcesGroup, mainSourcesFolders, src.path, project->absolutePath(src.path));
    }

    for (const QString &header : project->headers) {
        const QString suffix = QFileInfo(header).suffix().toLower();
        if (suffix == QLatin1String("i") || !mainHeadersGroup)
            addLeaf(asmIncludesGroup, asmIncludesFolders, header, project->absolutePath(header));
        else
            addLeaf(mainHeadersGroup, mainHeadersFolders, header, project->absolutePath(header));
    }

    m_tree->expandAll();
}
