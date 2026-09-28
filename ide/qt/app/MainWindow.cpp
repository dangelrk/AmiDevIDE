#include "MainWindow.h"

#include "editor/EditorTabWidget.h"
#include "editor/EditorWidget.h"
#include "editor/CompletionModel.h"
#include "project/ProjectTreeDock.h"
#include "project/ProjectSettingsDialog.h"
#include "project/ProjectTemplates.h"
#include "project/AddFileDialog.h"
#include "panes/SymbolsDock.h"
#include "panes/OutputPane.h"
#include "panes/IssuesPane.h"
#include "panes/Issue.h"
#include "panes/SearchResultsDock.h"
#include "panes/MemoryMapDock.h"
#include "help/HelpDock.h"
#include "AppSettings.h"
#include "SettingsDialog.h"
#include "sdk/SdkRegistry.h"
#include "build/BuildManager.h"
#include "build/EmulatorRunner.h"
#include "aide/aide_reindent.h"

#include <cstdlib>
#include "index/SymbolIndexer.h"
#include "build/MakefileGenerator.h"
#include "disasm/DisassemblyDialog.h"
#include "search/ProjectSearch.h"
#include "Version.h"

#include <QAction>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_editorTabs(new EditorTabWidget(this))
    , m_projectTree(new ProjectTreeDock(this))
    , m_symbolsDock(new SymbolsDock(this))
    , m_outputPane(new OutputPane(this))
    , m_issuesPane(new IssuesPane(this))
    , m_helpDock(new HelpDock(this))
    , m_searchResultsDock(new SearchResultsDock(this))
    , m_memoryMapDock(new MemoryMapDock(this))
    , m_buildManager(new BuildManager(this))
    , m_emulatorRunner(new EmulatorRunner(this))
    , m_indexer(new SymbolIndexer)
{
    m_indexer->moveToThread(&m_indexerThread);
    connect(&m_indexerThread, &QThread::finished, m_indexer, &QObject::deleteLater);
    connect(m_indexer, &SymbolIndexer::fileIndexed, this, &MainWindow::onFileIndexed);
    connect(this, &MainWindow::requestSetIndexerContext, m_indexer, &SymbolIndexer::setProjectContext);
    connect(this, &MainWindow::requestIndexFile, m_indexer, &SymbolIndexer::indexFile);
    m_indexerThread.start();

    connect(m_buildManager, &BuildManager::started, this, &MainWindow::onBuildStarted);
    connect(m_buildManager, &BuildManager::outputLine, this, &MainWindow::onBuildOutputLine);
    connect(m_buildManager, &BuildManager::issueFound, this, &MainWindow::onBuildIssueFound);
    connect(m_buildManager, &BuildManager::finished, this, &MainWindow::onBuildFinished);

    connect(m_emulatorRunner, &EmulatorRunner::statusMessage, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 5000);
    });
    connect(m_emulatorRunner, &EmulatorRunner::errorOccurred, this, [this](const QString &msg) {
        statusBar()->showMessage(msg, 5000);
        m_outputPane->appendLine(msg);
    });

    // Repair settings left behind by an earlier installation before anything
    // reads a path out of them. Doing this after SdkRegistry::reload() would
    // load the SDK registry from a directory that no longer exists.
    m_settingsMigrations = AppSettings::instance().migrateStaleSettings();

    SdkRegistry::instance().reload(AppSettings::instance().toolchainRoot());

    resize(1280, 860);

    setCentralWidget(m_editorTabs);

    createDocks();
    createActions();
    updateWindowTitle();

    connect(m_editorTabs, &EditorTabWidget::gotoDefinitionRequested, this, &MainWindow::onGotoDefinitionRequested);
    connect(m_editorTabs, &EditorTabWidget::helpRequested, this, &MainWindow::showHelpForWord);
    connect(m_editorTabs, &EditorTabWidget::autodocSearchRequested, this, &MainWindow::onAutodocSearchRequested);
    connect(m_editorTabs, &EditorTabWidget::findReferencesRequested, this, &MainWindow::onFindReferencesRequested);
    connect(m_editorTabs, &EditorTabWidget::signatureHelpRequested, this, &MainWindow::onSignatureHelpRequested);
    connect(m_editorTabs, &EditorTabWidget::structInfoRequested, this, &MainWindow::onStructInfoRequested);

    connect(m_projectTree, &ProjectTreeDock::fileActivated, this, [this](const QString &path) {
        openFileAt(path);
    });
    connect(m_projectTree, &ProjectTreeDock::removeFileRequested, this, &MainWindow::removeFileFromProject);

    connect(m_symbolsDock, &SymbolsDock::symbolActivated, this, &MainWindow::onSymbolActivated);
    connect(m_editorTabs, &EditorTabWidget::fileContentsPersisted, this, &MainWindow::reindexOpenFile);
    connect(m_editorTabs, &EditorTabWidget::currentFileChanged, m_projectTree, &ProjectTreeDock::setActiveFile);
    connect(m_editorTabs, &EditorTabWidget::fileModifiedChanged, m_projectTree, &ProjectTreeDock::setFileModified);
    connect(m_helpDock, &HelpDock::autodocRequested, this, &MainWindow::onAutodocRequested);

    connect(m_outputPane, &OutputPane::locationActivated, this, [this](const QString &file, int line) {
        gotoLocation(file, line);
    });
    connect(m_issuesPane, &IssuesPane::locationActivated, this,
            [this](const QString &file, int line, const QString &message) {
                gotoLocation(file, line);
                statusBar()->showMessage(message, 5000);
            });
    connect(m_searchResultsDock, &SearchResultsDock::locationActivated, this,
            [this](const QString &file, int line, int column) {
                gotoLocation(file, line, column);
            });

    reportToolchainState();

    // Session restore: reopen the most recently used project, if it still exists.
    const QStringList recents = AppSettings::instance().recentProjects();
    if (!recents.isEmpty() && QFile::exists(recents.first()))
        openProjectAt(recents.first());
}

// Say where the toolchain is, and say so loudly when there isn't one. Without
// this the first sign of a missing or moved toolchain is `make` reporting
// "No such file or directory" for vc, several steps removed from the cause.
void MainWindow::reportToolchainState()
{
    for (const QString &line : std::as_const(m_settingsMigrations))
        m_outputPane->appendLine(line);

    const QString root = AppSettings::instance().toolchainRoot();
    if (AppSettings::looksLikeToolchain(root)) {
        m_outputPane->appendLine(tr("Toolchain: %1").arg(root));
        statusBar()->showMessage(m_settingsMigrations.isEmpty()
                                     ? tr("Ready")
                                     : m_settingsMigrations.first(),
                                 m_settingsMigrations.isEmpty() ? 0 : 8000);
        return;
    }

    const QString message =
        tr("No toolchain at %1 — builds will fail. "
           "Set it under Options ▸ Settings, or run: "
           "cmake --build build --target acd-toolchain").arg(root);
    m_outputPane->appendLine(message);
    statusBar()->showMessage(message);
}

void MainWindow::createDocks()
{
    m_projectTreeDockWidget = new QDockWidget(tr("Project"), this);
    m_projectTreeDockWidget->setObjectName(QStringLiteral("ProjectTreeDock"));
    m_projectTreeDockWidget->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectTreeDockWidget);

    m_symbolsDockWidget = new QDockWidget(tr("Symbols"), this);
    m_symbolsDockWidget->setObjectName(QStringLiteral("SymbolsDock"));
    m_symbolsDockWidget->setWidget(m_symbolsDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_symbolsDockWidget);
    tabifyDockWidget(m_projectTreeDockWidget, m_symbolsDockWidget);
    m_projectTreeDockWidget->raise();

    m_bottomDockWidget = new QDockWidget(tr("Output"), this);
    m_bottomDockWidget->setObjectName(QStringLiteral("OutputDock"));
    m_bottomDockWidget->setWidget(m_outputPane);
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDockWidget);

    auto *issuesDockWidget = new QDockWidget(tr("Issues"), this);
    issuesDockWidget->setObjectName(QStringLiteral("IssuesDock"));
    issuesDockWidget->setWidget(m_issuesPane);
    addDockWidget(Qt::BottomDockWidgetArea, issuesDockWidget);
    tabifyDockWidget(m_bottomDockWidget, issuesDockWidget);

    m_helpDockWidget = new QDockWidget(tr("Help"), this);
    m_helpDockWidget->setObjectName(QStringLiteral("HelpDock"));
    m_helpDockWidget->setWidget(m_helpDock);
    addDockWidget(Qt::RightDockWidgetArea, m_helpDockWidget);

    m_searchResultsDockWidget = new QDockWidget(tr("Search Results"), this);
    m_searchResultsDockWidget->setObjectName(QStringLiteral("SearchResultsDock"));
    m_searchResultsDockWidget->setWidget(m_searchResultsDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_searchResultsDockWidget);
    tabifyDockWidget(m_bottomDockWidget, m_searchResultsDockWidget);

    m_memoryMapDockWidget = new QDockWidget(tr("Memory Map"), this);
    m_memoryMapDockWidget->setObjectName(QStringLiteral("MemoryMapDock"));
    m_memoryMapDockWidget->setWidget(m_memoryMapDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_memoryMapDockWidget);
    tabifyDockWidget(m_bottomDockWidget, m_memoryMapDockWidget);

    // tabifyDockWidget() brings the newest tab to the front, so raise
    // Output only once every bottom dock has been added.
    m_bottomDockWidget->raise();
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *newAct = fileMenu->addAction(tr("&New File"), this, &MainWindow::newFile);
    newAct->setShortcut(QKeySequence::New);

    QAction *openAct = fileMenu->addAction(tr("&Open File..."), this, &MainWindow::openFile);
    openAct->setShortcut(QKeySequence::Open);

    QAction *saveAct = fileMenu->addAction(tr("&Save"), m_editorTabs, &EditorTabWidget::saveCurrent);
    saveAct->setShortcut(QKeySequence::Save);

    QAction *saveAsAct = fileMenu->addAction(tr("Save &As..."), m_editorTabs, &EditorTabWidget::saveCurrentAs);
    saveAsAct->setShortcut(QKeySequence::SaveAs);

    fileMenu->addSeparator();

    QAction *closeTabAct = fileMenu->addAction(tr("&Close Tab"), m_editorTabs, &EditorTabWidget::closeCurrentTab);
    closeTabAct->setShortcut(QKeySequence::Close);

    fileMenu->addSeparator();
    QAction *quitAct = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAct->setShortcut(QKeySequence::Quit);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *findInProjectAct = editMenu->addAction(tr("&Find in Project..."), this, &MainWindow::focusProjectSearch);
    findInProjectAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    editMenu->addSeparator();
    QAction *formatAct = editMenu->addAction(tr("&Format Document"), this, &MainWindow::formatCurrentFile);
    formatAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));

    QMenu *projectMenu = menuBar()->addMenu(tr("&Project"));
    projectMenu->addAction(tr("&New Project..."), this, &MainWindow::newProject);
    projectMenu->addAction(tr("&Open Project..."), this, &MainWindow::openProject);
    projectMenu->addSeparator();
    projectMenu->addAction(tr("Add &Existing File to Project..."), this, &MainWindow::addExistingFileToProject);
    QMenu *addNewFileMenu = projectMenu->addMenu(tr("Add &New File to Project"));
    using Category = AddFileDialog::FileCategory;
    addNewFileMenu->addAction(tr("C Source..."), this, [this] { addNewFileToProject(Category::CSource); });
    addNewFileMenu->addAction(tr("C Header..."), this, [this] { addNewFileToProject(Category::CHeader); });
    addNewFileMenu->addAction(tr("Assembler Source..."), this, [this] { addNewFileToProject(Category::AsmSource); });
    addNewFileMenu->addAction(tr("BASIC Source..."), this, [this] { addNewFileToProject(Category::BasicSource); });
    addNewFileMenu->addAction(tr("Assembler Include..."), this, [this] { addNewFileToProject(Category::AsmInclude); });
    m_recentProjectsMenu = projectMenu->addMenu(tr("Open &Recent"));
    loadRecentProjectsMenu();
    projectMenu->addSeparator();
    QAction *saveProjectAct = projectMenu->addAction(tr("&Save Project"), this, &MainWindow::saveProject);
    // Not Cmd+Shift+S: that's QKeySequence::SaveAs (File > Save As...), already bound above.
    saveProjectAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    projectMenu->addSeparator();
    projectMenu->addAction(tr("Project &Settings..."), this, &MainWindow::openProjectSettings);

    QMenu *optionsMenu = menuBar()->addMenu(tr("&Options"));
    optionsMenu->addAction(tr("&Global Settings..."), this, &MainWindow::openGlobalSettings);

    QMenu *buildMenu = menuBar()->addMenu(tr("&Build"));
    m_buildAct = buildMenu->addAction(tr("&Build"), this, &MainWindow::buildProject);
    m_buildAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));

    m_runAct = buildMenu->addAction(tr("&Run"), this, &MainWindow::runProject);
    m_runAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));

    m_cleanAct = buildMenu->addAction(tr("&Clean"), this, &MainWindow::cleanProject);

    m_stopBuildAct = buildMenu->addAction(tr("&Stop Build"), this, &MainWindow::stopBuild);
    m_stopBuildAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    m_stopBuildAct->setEnabled(false);

    buildMenu->addSeparator();
    buildMenu->addAction(tr("&Disassemble Binary..."), this, &MainWindow::disassembleBinary);

    buildMenu->addSeparator();
    QAction *nextIssueAct = buildMenu->addAction(tr("&Next Issue"), this, [this] { m_issuesPane->gotoNext(); });
    nextIssueAct->setShortcut(QKeySequence(Qt::Key_F4));

    QAction *prevIssueAct = buildMenu->addAction(tr("&Previous Issue"), this, [this] { m_issuesPane->gotoPrevious(); });
    prevIssueAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F4));

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_projectTreeDockWidget->toggleViewAction());
    viewMenu->addAction(m_symbolsDockWidget->toggleViewAction());
    viewMenu->addAction(m_bottomDockWidget->toggleViewAction());
    viewMenu->addAction(m_helpDockWidget->toggleViewAction());
    viewMenu->addAction(m_searchResultsDockWidget->toggleViewAction());
    viewMenu->addAction(m_memoryMapDockWidget->toggleViewAction());

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *searchAutodocsAct = helpMenu->addAction(tr("&Search Autodocs..."), this, &MainWindow::focusAutodocSearch);
    // Was Cmd+Shift+F; freed up for Edit > Find in Project... (F2 in the
    // editor already covers "search autodocs for the word under the cursor",
    // making this menu's own shortcut less essential).
    searchAutodocsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));
    helpMenu->addSeparator();
    QAction *aboutAct = helpMenu->addAction(tr("&About AmigaIDE..."), this, &MainWindow::showAboutDialog);
    aboutAct->setMenuRole(QAction::AboutRole);
}

void MainWindow::focusAutodocSearch()
{
    m_helpDockWidget->show();
    m_helpDockWidget->raise();
    m_helpDock->focusSearch();
}

void MainWindow::focusProjectSearch()
{
    m_searchResultsDock->setProjectFiles(m_project ? m_project->allProjectFiles() : QStringList());
    m_searchResultsDockWidget->show();
    m_searchResultsDockWidget->raise();
    m_searchResultsDock->focusSearchField();
}

void MainWindow::formatCurrentFile()
{
    EditorWidget *editor = m_editorTabs->currentEditor();
    if (!editor) {
        statusBar()->showMessage(tr("No file open."), 3000);
        return;
    }

    const QByteArray original = editor->toPlainText().toUtf8();
    char *reindented = aide_reindent_braces(original.constData(), "    ");

    // A single select-all + insertText edit (not setPlainText(), which
    // would reset the undo stack and modified-state tracking) -- this
    // way the reformat is one ordinary, undoable edit like any other.
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(QString::fromUtf8(reindented));
    free(reindented);

    statusBar()->showMessage(tr("Formatted -- not yet saved."), 3000);
}

void MainWindow::addExistingFileToProject()
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Existing Files to Project"), m_project->projectRoot(),
        tr("Amiga sources (*.c *.h *.s *.asm *.i);;All Files (*)"));
    if (paths.isEmpty())
        return;

    const QDir projectDir(m_project->projectRoot());
    for (const QString &path : paths) {
        const QString relPath = projectDir.relativeFilePath(path);
        const QString suffix = QFileInfo(path).suffix().toLower();

        if (suffix == QLatin1String("h") || suffix == QLatin1String("i")) {
            if (!m_project->headers.contains(relPath))
                m_project->headers << relPath;
        } else {
            bool exists = false;
            for (const SourceFile &s : m_project->sources) {
                if (s.path == relPath) { exists = true; break; }
            }
            if (!exists)
                m_project->sources << SourceFile{ relPath };
        }
        openFileAt(path);
    }

    m_project->save();
    m_projectTree->setProject(m_project.get());
    reindexAllProjectFiles();
    statusBar()->showMessage(tr("Added %1 file(s) to project.").arg(paths.size()), 3000);
}

void MainWindow::addNewFileToProject(AddFileDialog::FileCategory category)
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }

    AddFileDialog dialog(category, m_project->projectRoot(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString relPath = dialog.resultRelativePath();
    const QString fullPath = m_project->absolutePath(relPath);

    if (QFile::exists(fullPath)) {
        QMessageBox::warning(this, tr("Add New File"), tr("File already exists:\n%1").arg(fullPath));
        return;
    }

    if (!QDir().mkpath(QFileInfo(fullPath).absolutePath())) {
        QMessageBox::warning(this, tr("Add New File"), tr("Could not create directory for the new file."));
        return;
    }

    QFile file(fullPath);
    if (!file.open(QFile::WriteOnly)) {
        QMessageBox::warning(this, tr("Add New File"), tr("Could not create file:\n%1").arg(fullPath));
        return;
    }
    file.close();

    using Category = AddFileDialog::FileCategory;
    if (category == Category::CHeader || category == Category::AsmInclude)
        m_project->headers << relPath;
    else
        m_project->sources << SourceFile{ relPath };

    m_project->save();
    m_projectTree->setProject(m_project.get());
    reindexAllProjectFiles();
    openFileAt(fullPath);
    statusBar()->showMessage(tr("Added \"%1\" to project.").arg(relPath), 3000);
}

void MainWindow::removeFileFromProject(const QString &absPath)
{
    if (!m_project)
        return;

    QMessageBox box(this);
    box.setWindowTitle(tr("Remove File from Project"));
    box.setText(tr("Remove \"%1\" from the project?").arg(QFileInfo(absPath).fileName()));
    box.setInformativeText(tr("The file can stay on disk, untracked by the project, or be deleted permanently."));
    QPushButton *removeOnlyButton = box.addButton(tr("Remove from Project"), QMessageBox::AcceptRole);
    QPushButton *removeDeleteButton = box.addButton(tr("Remove && Delete File"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(removeOnlyButton);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked != removeOnlyButton && clicked != removeDeleteButton)
        return; // cancelled

    const bool alsoDelete = clicked == removeDeleteButton;

    bool removedAny = false;
    for (int i = m_project->sources.size() - 1; i >= 0; --i) {
        if (m_project->absolutePath(m_project->sources[i].path) == absPath) {
            m_project->sources.removeAt(i);
            removedAny = true;
        }
    }
    for (int i = m_project->headers.size() - 1; i >= 0; --i) {
        if (m_project->absolutePath(m_project->headers[i]) == absPath) {
            m_project->headers.removeAt(i);
            removedAny = true;
        }
    }

    if (!removedAny)
        return;

    if (alsoDelete) {
        m_editorTabs->closeFileIfOpen(absPath, /*discardChanges=*/true);
        if (!QFile::remove(absPath))
            QMessageBox::warning(this, tr("Remove File"), tr("Removed from project, but could not delete the file from disk:\n%1").arg(absPath));
    }

    m_symbolDatabase.removeFile(absPath);
    m_includeGraph.removeFile(absPath);

    m_project->save();
    m_projectTree->setProject(m_project.get());
    refreshSymbolsDock();
    refreshCompletionWords();
    statusBar()->showMessage(
        alsoDelete ? tr("Removed and deleted \"%1\".").arg(QFileInfo(absPath).fileName())
                   : tr("Removed \"%1\" from project.").arg(QFileInfo(absPath).fileName()),
        3000);
}

void MainWindow::disassembleBinary()
{
    QString path;
    if (m_project) {
        const QString target = MakefileGenerator::targetBinaryPath(*m_project);
        if (QFile::exists(target))
            path = target;
    }

    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, tr("Select Binary or Object File to Disassemble"));
        if (path.isEmpty())
            return;
    }

    auto *dialog = new DisassemblyDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (dialog->disassemble(path))
        dialog->show();
    else
        dialog->deleteLater();
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(
        this, tr("About AmigaIDE"),
        tr("<h3>AmigaIDE</h3>"
           "<p>Version %1</p>"
           "<p>A native Qt6 IDE for the vbcc/vasm/vlink AmigaOS m68k cross-compile toolchain.</p>"
           "<p>%2</p>")
            .arg(QStringLiteral(AMIGAIDE_VERSION_FULL), QStringLiteral(AMIGAIDE_COPYRIGHT_STRING)));
}

void MainWindow::newFile()
{
    m_editorTabs->newFile();
}

void MainWindow::openFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open File"), QString(),
        tr("Amiga sources (*.c *.h *.s *.asm *.i);;All Files (*)"));
    if (!path.isEmpty())
        openFileAt(path);
}

void MainWindow::openFileAt(const QString &path, int line, int column)
{
    EditorWidget *editor = m_editorTabs->openFile(path);
    if (!editor)
        return;

    editor->setCompletionWords(m_completionWords);

    if (line >= 0) {
        QTextCursor cursor(editor->document()->findBlockByNumber(qMax(0, line - 1)));
        cursor.movePosition(QTextCursor::StartOfBlock);
        for (int i = 0; i < column; ++i)
            cursor.movePosition(QTextCursor::Right);
        editor->setTextCursor(cursor);
        editor->centerCursor();
    }
    editor->setFocus();
}

void MainWindow::gotoLocation(const QString &file, int line, int column)
{
    openFileAt(file, line, column);
}

void MainWindow::showHelpForWord(const QString &word)
{
    m_helpDockWidget->show();
    m_helpDockWidget->raise();
    onAutodocRequested(word);
}

void MainWindow::onAutodocSearchRequested(const QString &word)
{
    m_helpDockWidget->show();
    m_helpDockWidget->raise();
    m_helpDock->prefillAndSearch(word);
}

void MainWindow::onFindReferencesRequested(const QString &word)
{
    const QStringList files = m_project ? m_project->allProjectFiles() : QStringList();
    auto matches = ProjectSearch::search(files, word, /*caseSensitive=*/true, /*wholeWord=*/true, /*regex=*/false);

    // Cross-check against the symbol index so definition sites are flagged
    // (and rendered bold in the results dock) rather than looking like any
    // other occurrence of the word.
    const QList<Symbol> definitions = m_symbolDatabase.findByName(word);
    for (ProjectSearch::SearchMatch &match : matches) {
        for (const Symbol &def : definitions) {
            if (def.file == match.file && def.line == match.line) {
                match.isDefinition = true;
                break;
            }
        }
    }

    m_searchResultsDockWidget->show();
    m_searchResultsDockWidget->raise();
    m_searchResultsDock->showResults(matches, tr("%1 reference(s) to \"%2\"").arg(matches.size()).arg(word));
}

void MainWindow::onSignatureHelpRequested(const QString &word)
{
    QString text;

    if (const SfdFunction *fn = m_sfdIndex.find(word)) {
        text = fn->rawSignature;
        if (!fn->registers.isEmpty())
            text += tr("\nRegisters: %1").arg(fn->registers);
    } else {
        // Fall back to a project-local prototype/definition with a captured
        // declaration line (Symbol::signature), same source the F1 hothelp
        // panel uses for a SymbolDatabase hit.
        for (const Symbol &s : m_symbolDatabase.findByName(word)) {
            if ((s.kind == SymbolKind::Function || s.kind == SymbolKind::Prototype) && !s.signature.isEmpty()) {
                text = s.signature;
                break;
            }
        }
    }

    if (text.isEmpty())
        return;
    if (EditorWidget *editor = m_editorTabs->currentEditor())
        editor->showCallTip(text);
}

void MainWindow::onStructInfoRequested(const QString &word)
{
    // Is `word` itself a struct/union (or a typedef alias for an anonymous
    // one)? membersOfStruct() is the ground truth for "this name owns
    // members" -- Struct/Union/Typedef symbols alone don't distinguish a
    // struct-like typedef from e.g. "typedef LONG MyLong;".
    QString ownerName;
    QString highlightMember;

    if (!m_symbolDatabase.membersOfStruct(word).isEmpty()) {
        ownerName = word;
    } else {
        // Otherwise, is `word` a member of some struct?
        for (const Symbol &s : m_symbolDatabase.allSymbols()) {
            if (s.kind == SymbolKind::StructMember && s.name == word) {
                ownerName = s.scope;
                highlightMember = word;
                break;
            }
        }
    }

    if (ownerName.isEmpty())
        return; // nothing struct-related known for this word

    QList<Symbol> members = m_symbolDatabase.membersOfStruct(ownerName);
    std::sort(members.begin(), members.end(),
              [](const Symbol &a, const Symbol &b) { return a.offset < b.offset; });

    int totalSize = -1;
    for (const Symbol &s : m_symbolDatabase.findByName(ownerName)) {
        if ((s.kind == SymbolKind::Struct || s.kind == SymbolKind::Union || s.kind == SymbolKind::Typedef)
                && s.offset >= 0) {
            totalSize = s.offset;
            break;
        }
    }

    QString text;
    if (!highlightMember.isEmpty()) {
        for (const Symbol &m : members) {
            if (m.name == highlightMember) {
                text += tr("%1.%2 : %3  @ offset %4 (0x%5)\n\n")
                        .arg(ownerName, m.name, m.signature)
                        .arg(m.offset)
                        .arg(m.offset, 0, 16);
                break;
            }
        }
    }

    text += tr("struct %1").arg(ownerName);
    if (totalSize >= 0)
        text += tr(" -- %1 bytes").arg(totalSize);
    text += QLatin1Char('\n');
    for (const Symbol &m : members) {
        text += QStringLiteral("  +0x%1  %2  %3\n")
                .arg(m.offset, 2, 16, QLatin1Char('0'))
                .arg(m.signature, -20)
                .arg(m.name);
    }

    m_helpDockWidget->show();
    m_helpDockWidget->raise();
    m_helpDock->showPlainText(tr("struct %1").arg(ownerName), text);
}

void MainWindow::onAutodocRequested(const QString &functionName)
{
    m_helpDockWidget->show();
    m_helpDockWidget->raise();

    if (functionName.contains(QLatin1Char('*')) || functionName.contains(QLatin1Char('?'))) {
        const QRegularExpression wildcardRe(QRegularExpression::wildcardToRegularExpression(functionName),
                                             QRegularExpression::CaseInsensitiveOption);
        QStringList matches = m_autodocIndex.findWildcardNames(functionName);
        for (const QString &fn : m_sfdIndex.allFunctionNames()) {
            if (wildcardRe.match(fn).hasMatch())
                matches << fn;
        }
        matches.removeDuplicates();
        matches.sort(Qt::CaseInsensitive);

        if (matches.size() == 1) {
            onAutodocRequested(matches.first());
            return;
        }
        m_helpDock->showCandidateList(matches);
        return;
    }

    QList<AutodocEntry> entries = m_autodocIndex.find(functionName);
    if (!entries.isEmpty()) {
        if (entries.size() > 1) {
            if (const SfdFunction *fn = m_sfdIndex.find(functionName)) {
                for (const AutodocEntry &e : entries) {
                    if (e.library == fn->library) {
                        entries = { e };
                        break;
                    }
                }
            }
        }
        const AutodocEntry &entry = entries.first();
        m_helpDock->showAutodocEntry(QStringLiteral("%1/%2").arg(entry.library, entry.function),
                                      m_autodocIndex.readEntryText(entry));
        return;
    }

    // ACE BASIC keywords, from the SDK's own quick reference. Checked
    // before the symbol database so PRINT gives the language's help rather
    // than an unrelated identifier that happens to share the name.
    if (m_project && m_project->language == ProjectLanguage::Basic) {
        m_aceQuickRef.load(AppSettings::instance().toolchainRoot() + QStringLiteral("/sdk/ACE"));
        const QString help = m_aceQuickRef.lookup(functionName);
        if (!help.isEmpty()) {
            m_helpDock->showPlainText(functionName.toUpper(), help);
            return;
        }
    }

    const QList<Symbol> candidates = m_symbolDatabase.findByName(functionName);
    if (!candidates.isEmpty()) {
        const Symbol &s = candidates.first();
        const QString info = tr("%1\n\nFile: %2\nLine: %3\n\n(Enter to go to definition)")
                                  .arg(s.signature.isEmpty() ? s.name : s.signature)
                                  .arg(QFileInfo(s.file).fileName())
                                  .arg(s.line);
        m_helpDock->showPlainText(s.name, info);
        return;
    }

    statusBar()->showMessage(tr("No help found for \"%1\".").arg(functionName), 3000);
}

void MainWindow::saveEditorsBeforeBuild()
{
    // Build must always compile what's actually in the editor, not
    // whatever was last written to disk -- otherwise editing a file and
    // hitting Build without an explicit Save first silently builds the
    // stale (possibly still-empty) on-disk version. Mirrors the native
    // Amiga build's editor_save_current() call in do_build().
    m_editorTabs->saveAllModified();
}

void MainWindow::setBuildActionsEnabled(bool enabled)
{
    if (m_buildAct) m_buildAct->setEnabled(enabled);
    if (m_cleanAct) m_cleanAct->setEnabled(enabled);
    // A link library has nothing to run: the build produces a .lib for
    // other projects to link against, not an executable.
    const bool runnable = !m_project || m_project->output.kind == OutputKind::Executable;
    if (m_runAct) m_runAct->setEnabled(enabled && runnable);
    if (m_stopBuildAct) m_stopBuildAct->setEnabled(!enabled);
}

void MainWindow::buildProject()
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }
    if (m_buildManager->isRunning()) {
        statusBar()->showMessage(tr("Build already in progress."), 3000);
        return;
    }
    saveEditorsBeforeBuild();
    m_buildManager->build(*m_project);
}

void MainWindow::cleanProject()
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }
    if (m_buildManager->isRunning()) {
        statusBar()->showMessage(tr("Build already in progress."), 3000);
        return;
    }
    m_buildManager->clean(*m_project);
}

void MainWindow::runProject()
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }
    if (m_buildManager->isRunning()) {
        statusBar()->showMessage(tr("Build already in progress."), 3000);
        return;
    }
    saveEditorsBeforeBuild();
    m_runAfterBuild = true;
    m_buildManager->build(*m_project);
}

void MainWindow::stopBuild()
{
    m_buildManager->stop();
}

void MainWindow::onBuildStarted()
{
    m_outputPane->clearOutput();
    if (m_project) {
        for (const QString &id : SdkRegistry::instance().missingIds(m_project->sdks.selected))
            m_outputPane->appendLine(tr("Warning: SDK \"%1\" is not installed; its paths are left out.").arg(id));
    }
    m_issuesPane->clear();
    m_currentIssues.clear();
    m_building = true;
    updateWindowTitle();
    setBuildActionsEnabled(false);
    statusBar()->showMessage(tr("Building..."));
}

void MainWindow::onBuildOutputLine(const QString &line)
{
    m_outputPane->appendLine(line);
}

void MainWindow::onBuildIssueFound(const Issue &issue)
{
    m_currentIssues << issue;
    m_issuesPane->appendIssue(issue);
}

void MainWindow::onBuildFinished(bool success, bool stopped)
{
    m_building = false;
    updateWindowTitle();
    setBuildActionsEnabled(true);

    if (stopped) {
        m_runAfterBuild = false;
        statusBar()->showMessage(tr("Build stopped."), 5000);
        return;
    }

    int errorCount = 0, warningCount = 0;
    for (const Issue &issue : m_currentIssues) {
        if (issue.severity == IssueSeverity::Error)
            ++errorCount;
        else if (issue.severity == IssueSeverity::Warning)
            ++warningCount;
    }

    statusBar()->showMessage(
        success ? tr("Build succeeded (%1 warning(s)).").arg(warningCount)
                : tr("Build failed: %1 error(s), %2 warning(s).").arg(errorCount).arg(warningCount),
        5000);

    if (!success && !m_currentIssues.isEmpty())
        m_issuesPane->gotoNext();

    if (success && m_project) {
        const QString target = MakefileGenerator::targetBinaryPath(*m_project);
        if (QFile::exists(target))
            m_memoryMapDock->showForBinary(target);
    }

    if (m_runAfterBuild) {
        m_runAfterBuild = false;
        if (success && m_project)
            m_emulatorRunner->run(*m_project);
    }
}

void MainWindow::newProject()
{
    // Directory first, then Project Settings directly -- no separate
    // name/template dialog: Name/Binary are already fields on the
    // Settings dialog's General tab (matching the native Amiga MUI app).
    // Cancel there creates nothing at all on disk; OK validates (its own
    // accept() override refuses an empty name/binary) and creates+saves.
    const QString dir = QFileDialog::getExistingDirectory(this, tr("New Project: Choose Directory"));
    if (dir.isEmpty())
        return;

    // A genuinely empty project -- no auto-created template source file
    // and no C-vs-Assembler question, since neither is asked anymore.
    // Files come from "Add New File to Project"/"Add Existing File to
    // Project" instead. "vc" is vbcc's own C runtime support library
    // (___stack, __math_init, ___main, ___v0printf, ...), needed by
    // virtually any vbcc-linked Amiga program regardless of what it's
    // actually made of -- see the Amiga app's do_new_project() for the
    // same reasoning.
    Project project;

    ProjectSettingsDialog dialog(project, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    project = dialog.resultProject();

    // "vc" is vbcc's own C runtime support library (___stack, __math_init,
    // ___main, ___v0printf, ...), needed by virtually any vbcc-linked Amiga
    // program -- but not by a BASIC one, which links ACE's runtime instead.
    // Added here rather than before the dialog because the language is only
    // known once the user has answered.
    if (project.language == ProjectLanguage::C)
        project.linker.libraries << QStringLiteral("vc");

    // A shared library needs its romtag and vector table in place before
    // anything links, so this is the one kind that gets starter files.
    const QString path0 = QDir(dir).absoluteFilePath(project.name + QStringLiteral(".aproj"));
    project.setFilePathForTemplates(path0);
    const QStringList created = ProjectTemplates::apply(&project);

    const QString path = path0;
    if (!project.save(path)) {
        QMessageBox::warning(this, tr("New Project"), tr("Could not create project file:\n%1").arg(path));
        return;
    }

    openProjectAt(path);
    if (!created.isEmpty())
        statusBar()->showMessage(tr("Created %1 from the shared-library template.")
                                     .arg(created.join(QStringLiteral(", "))), 5000);
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), QString(), tr("AmigaIDE projects (*.aproj)"));
    if (!path.isEmpty())
        openProjectAt(path);
}

void MainWindow::openProjectAt(const QString &path)
{
    if (!m_editorTabs->closeAllTabsWithPrompt())
        return;

    auto project = std::make_unique<Project>();
    if (!project->load(path)) {
        QMessageBox::warning(this, tr("Open Project"), tr("Could not open project file:\n%1").arg(path));
        return;
    }

    m_project = std::move(project);
    AppSettings::instance().addRecentProject(path);
    loadRecentProjectsMenu();

    m_projectTree->setProject(m_project.get());
    m_editorTabs->setDefaultSaveDirectory(m_project->projectRoot());
    m_editorTabs->setProjectLanguage(m_project->language);
    setBuildActionsEnabled(true);
    updateWindowTitle();
    reindexAllProjectFiles();

    for (const QString &relPath : m_project->editor.openFiles)
        openFileAt(m_project->absolutePath(relPath));

    if (!m_project->editor.activeFile.isEmpty()) {
        if (EditorWidget *editor = m_editorTabs->editorForPath(m_project->absolutePath(m_project->editor.activeFile)))
            m_editorTabs->setCurrentWidget(editor);
    }

    statusBar()->showMessage(tr("Opened project \"%1\"").arg(m_project->name), 3000);

    const QStringList missing = SdkRegistry::instance().missingIds(m_project->sdks.selected);
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, tr("Missing SDKs"),
                             tr("This project uses SDKs that are not installed here:\n\n%1\n\n"
                                "The build will not find their headers and libraries. Install an SDK "
                                "with the same name, in that version or newer (Settings > SDKs, or an AmiDevSDK.json "
                                "under %2/sdk).")
                                 .arg(missing.join(QLatin1Char('\n')), AppSettings::instance().toolchainRoot()));
    }

    const QStringList upgrades = SdkRegistry::instance().majorUpgradeIds(m_project->sdks.selected);
    if (!upgrades.isEmpty()) {
        QStringList lines;
        for (const QString &id : upgrades)
            lines << tr("%1  ->  %2").arg(id, SdkRegistry::instance().resolve(id)->label());
        QMessageBox::warning(this, tr("Newer SDK versions"),
                             tr("This project was written for older major versions of these SDKs; "
                                "it builds against the installed ones:\n\n%1\n\n"
                                "Their interfaces may have changed. The project file keeps the "
                                "versions it asks for.")
                                 .arg(lines.join(QLatin1Char('\n'))));
    }
}

void MainWindow::saveProject()
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }

    QStringList openFiles;
    for (int i = 0; i < m_editorTabs->count(); ++i) {
        if (auto *editor = qobject_cast<EditorWidget *>(m_editorTabs->widget(i)))
            openFiles << editor->filePath();
    }
    m_project->editor.openFiles = openFiles;
    if (auto *current = m_editorTabs->currentEditor())
        m_project->editor.activeFile = current->filePath();

    if (m_project->save())
        statusBar()->showMessage(tr("Project saved."), 3000);
    else
        QMessageBox::warning(this, tr("Save Project"), tr("Could not save project file."));
}

void MainWindow::openProjectSettings()
{
    if (!m_project) {
        statusBar()->showMessage(tr("No project open."), 3000);
        return;
    }

    ProjectSettingsDialog dialog(*m_project, this);
    if (dialog.exec() == QDialog::Accepted) {
        const QString path = m_project->filePath();
        *m_project = dialog.resultProject();
        m_project->save(path);
        m_projectTree->setProject(m_project.get());
        updateWindowTitle();
        reindexAllProjectFiles();
    }
}

void MainWindow::openGlobalSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        SdkRegistry::instance().reload(AppSettings::instance().toolchainRoot());
        m_editorTabs->applyEditorSettingsToAllTabs();
    }
}

void MainWindow::updateWindowTitle()
{
    const QString base = m_project ? tr("%1 - AmigaIDE").arg(m_project->name) : tr("AmigaIDE");
    // Visible even when a build produces no output for a while (nothing
    // to look at in the status bar or the Ausgabe pane either) --
    // whoever's cmd-tabbed to another window still sees it's still
    // running.
    setWindowTitle(m_building ? tr("%1 [Building...]").arg(base) : base);
}

void MainWindow::loadRecentProjectsMenu()
{
    if (!m_recentProjectsMenu)
        return;

    m_recentProjectsMenu->clear();
    const QStringList recents = AppSettings::instance().recentProjects();
    if (recents.isEmpty()) {
        QAction *emptyAct = m_recentProjectsMenu->addAction(tr("(none)"));
        emptyAct->setEnabled(false);
        return;
    }

    for (const QString &path : recents) {
        m_recentProjectsMenu->addAction(path, this, [this, path] { openProjectAt(path); });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_editorTabs->closeAllTabsWithPrompt())
        event->accept();
    else
        event->ignore();
}

MainWindow::~MainWindow()
{
    m_indexerThread.quit();
    m_indexerThread.wait();
}

void MainWindow::reindexAllProjectFiles()
{
    if (!m_project)
        return;

    const QStringList includePaths = allIncludeSearchPaths();

    QStringList sfdDirs;
    QList<SdkDescriptor> activeSdks;
    for (const QString &id : m_project->sdks.selected) {
        if (const SdkDescriptor *sdk = SdkRegistry::instance().resolve(id)) {
            sfdDirs << sdk->sfdDirs;
            activeSdks << *sdk;
        }
    }
    m_sfdIndex.reload(sfdDirs);
    m_autodocIndex.reload(activeSdks);

    emit requestSetIndexerContext(m_project->projectRoot(), includePaths, m_project->language);

    for (const SourceFile &src : m_project->sources)
        emit requestIndexFile(m_project->absolutePath(src.path));
    for (const QString &header : m_project->headers)
        emit requestIndexFile(m_project->absolutePath(header));
}

void MainWindow::reindexOpenFile(const QString &path)
{
    if (!m_project || path.isEmpty())
        return;
    // Only re-parse files that belong to the current project; skip files
    // opened standalone (no project loaded, or outside the project tree).
    emit requestIndexFile(path);
}

void MainWindow::onFileIndexed(const QString &path, QList<Symbol> symbols, QStringList resolvedIncludes)
{
    m_symbolDatabase.setFileSymbols(path, symbols);
    m_includeGraph.setDirectIncludes(path, resolvedIncludes);
    refreshSymbolsDock();
    refreshCompletionWords();
}

void MainWindow::refreshCompletionWords()
{
    QStringList words = CompletionModel::staticKeywords();
    for (const Symbol &s : m_symbolDatabase.allSymbols())
        words << s.name;
    for (const QString &fn : m_sfdIndex.allFunctionNames())
        words << fn;
    words.removeDuplicates();
    m_completionWords = words;

    for (int i = 0; i < m_editorTabs->count(); ++i) {
        if (auto *editor = qobject_cast<EditorWidget *>(m_editorTabs->widget(i)))
            editor->setCompletionWords(words);
    }
}

void MainWindow::refreshSymbolsDock()
{
    // StructMember entries are deliberately excluded here -- there can be
    // dozens per struct, which would swamp the general symbol tree. They're
    // reached instead via the dedicated Shift+F1 struct-info lookup.
    QList<Symbol> symbols = m_symbolDatabase.allSymbols();
    symbols.removeIf([](const Symbol &s) { return s.kind == SymbolKind::StructMember; });
    m_symbolsDock->setSymbols(symbols);
}

void MainWindow::onSymbolActivated(const QString &file, int line)
{
    gotoLocation(file, line);
}

int MainWindow::scanEnclosingAsmLabel(const QString &filePath, int fromLine) const
{
    EditorWidget *editor = m_editorTabs->editorForPath(filePath);
    if (!editor)
        return -1;

    for (int line = fromLine; line >= 1; --line) {
        const QString text = editor->document()->findBlockByNumber(line - 1).text();
        if (text.isEmpty() || text.at(0).isSpace())
            continue;
        static const QRegularExpression labelRe(QStringLiteral(R"re(^([A-Za-z_][\w$]*))re"));
        if (const auto m = labelRe.match(text); m.hasMatch())
            return line;
    }
    return -1;
}

QStringList MainWindow::allIncludeSearchPaths() const
{
    QStringList paths;
    if (m_project) {
        for (const QString &path : m_project->includePaths)
            paths << m_project->absolutePath(path);
    }

    // The toolchain's own base C headers (stdio.h, stdlib.h, ...).
    paths << AppSettings::instance().toolchainRoot() + QStringLiteral("/targets/m68k-amigaos/include");

    if (m_project) {
        const QStringList sdkIds = m_project->sdks.selected;

        for (const QString &id : sdkIds) {
            if (const SdkDescriptor *sdk = SdkRegistry::instance().resolve(id)) {
                paths << sdk->includeDirs;
                paths << sdk->asmIncludeDirs;
            }
        }
    }

    paths.removeDuplicates();
    return paths;
}

bool MainWindow::tryOpenIncludeOnLine(EditorWidget *editor, int line)
{
    if (!editor || line < 1)
        return false;

    const QString text = editor->document()->findBlockByNumber(line - 1).text();
    const QString suffix = QFileInfo(editor->filePath()).suffix().toLower();
    const bool isAsm = suffix == QLatin1String("s") || suffix == QLatin1String("asm") || suffix == QLatin1String("i");

    QString directive;
    bool quoted = true;
    if (!isAsm) {
        static const QRegularExpression cIncludeRe(QStringLiteral(R"re(^\s*#\s*include\s*([<"])([^">]+)[">])re"));
        const auto m = cIncludeRe.match(text);
        if (!m.hasMatch())
            return false;
        quoted = m.captured(1) == QLatin1String("\"");
        directive = m.captured(2);
    } else {
        static const QRegularExpression asmIncludeRe(
            QStringLiteral(R"re(^\s*include\s+"?([^"\s;]+)"?)re"), QRegularExpression::CaseInsensitiveOption);
        const auto m = asmIncludeRe.match(text);
        if (!m.hasMatch())
            return false;
        directive = m.captured(1);
        quoted = false;
    }

    const QString fileDir = QFileInfo(editor->filePath()).absolutePath();
    const QString resolved = IncludeGraph::resolveInclude(directive, quoted, fileDir, allIncludeSearchPaths());
    if (resolved.isEmpty()) {
        statusBar()->showMessage(tr("\"%1\" not found in the project (SDK headers aren't indexed).").arg(directive), 3000);
        return true; // recognized as an include line either way; don't fall through to symbol lookup
    }

    openFileAt(resolved);
    return true;
}

void MainWindow::onGotoDefinitionRequested(const QString &symbol, int fromLine)
{
    EditorWidget *current = m_editorTabs->currentEditor();
    if (tryOpenIncludeOnLine(current, fromLine))
        return;

    const QString currentFile = current ? current->filePath() : QString();

    // Compute the enclosing asm scope at the cursor, for local-label lookups.
    QString currentScope;
    if (current) {
        const int scopeLine = scanEnclosingAsmLabel(currentFile, fromLine);
        if (scopeLine > 0) {
            const QString text = current->document()->findBlockByNumber(scopeLine - 1).text();
            static const QRegularExpression labelRe(QStringLiteral(R"re(^([A-Za-z_][\w$]*))re"));
            if (const auto m = labelRe.match(text); m.hasMatch())
                currentScope = m.captured(1);
        }
    }

    QList<Symbol> candidates = m_symbolDatabase.findByName(symbol);

    // Local labels are only meaningful within their defining scope.
    QList<Symbol> scoped;
    for (const Symbol &s : candidates) {
        if (s.kind != SymbolKind::LocalLabel || s.scope == currentScope)
            scoped << s;
    }
    if (!scoped.isEmpty())
        candidates = scoped;

    if (candidates.isEmpty()) {
        if (!m_autodocIndex.find(symbol).isEmpty() || m_sfdIndex.find(symbol)) {
            onAutodocRequested(symbol);
        } else {
            statusBar()->showMessage(tr("No definition found for \"%1\".").arg(symbol), 3000);
        }
        return;
    }

    if (candidates.size() == 1) {
        gotoLocation(candidates.first().file, candidates.first().line);
        return;
    }

    // Prefer a same-file match if there is exactly one.
    QList<Symbol> sameFile;
    for (const Symbol &s : candidates) {
        if (s.file == currentFile)
            sameFile << s;
    }
    if (sameFile.size() == 1) {
        gotoLocation(sameFile.first().file, sameFile.first().line);
        return;
    }

    auto *menu = new QMenu(this);
    for (const Symbol &s : candidates) {
        QAction *act = menu->addAction(tr("%1:%2").arg(QFileInfo(s.file).fileName()).arg(s.line));
        connect(act, &QAction::triggered, this, [this, s] { gotoLocation(s.file, s.line); });
    }
    menu->popup(QCursor::pos());
}
