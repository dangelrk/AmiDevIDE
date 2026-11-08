#include "app/AppSettings.h"
#include "app/MainWindow.h"
#include "editor/EditorTabWidget.h"
#include "editor/EditorWidget.h"
#include "project/Project.h"
#include "vcs/HistoryDock.h"
#include "vcs/VcsDialogs.h"
#include "vcs/VcsDock.h"

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>

// The Qt IDE's version control, end to end in the real MainWindow
// (offscreen): create repository, ignore lines, stage all, commit, new
// branch, switch with an unsaved editor (saved first, refused while the
// change is uncommitted, then switched and reloaded), merge. With
// AIDE_TEST_AMIVCS (an amivcs) and AIDE_TEST_AMIVCS_NET=1 also push,
// clone and pull against the AmiVCS server AIDE_TEST_AMIVCS_URL (its base
// URL) as AIDE_TEST_AMIVCS_USER / AIDE_TEST_AMIVCS_PASS -- the URL and
// login dialogs are answered like a user would.
class TstVcsGui : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void workflow();

private:
    // answers every modal dialog that comes up: the next queued answer
    void answerDialogs();
    void queue(std::function<bool(QWidget *)> a) { m_answers.append(a); }
    bool waitIdle(int ms = 20000);
    QPushButton *button(const QString &text) const;
    QAction *action(const QString &text) const;
    QListWidget *list(int i) const;
    QString branchText() const;

    MainWindow *m_win = nullptr;
    QList<std::function<bool(QWidget *)>> m_answers;
    QStringList m_seen;   // texts of answered dialogs, for the checks
    QStringList m_unexpected;
    QString m_amivcs;
    QTemporaryDir m_home;
};

static bool yes(QWidget *w)
{
    auto *box = qobject_cast<QMessageBox *>(w);
    if (!box) return false;
    QAbstractButton *b = box->button(QMessageBox::Yes);
    if (!b) b = box->button(QMessageBox::Ok);
    if (!b) return false;
    b->click();
    return true;
}

// the AmiVCS login dialog: user, password, remember
static std::function<bool(QWidget *)> login(const QString &user, const QString &pass)
{
    return [user, pass](QWidget *w) {
        auto *d = qobject_cast<VcsLoginDialog *>(w);
        if (!d) return false;
        const QList<QLineEdit *> edits = d->findChildren<QLineEdit *>();
        if (edits.size() < 2) return false;
        edits[0]->setText(user);
        edits[1]->setText(pass);
        d->findChild<QCheckBox *>()->setChecked(true);
        d->accept();
        return true;
    };
}

static std::function<bool(QWidget *)> text(const QString &value)
{
    return [value](QWidget *w) {
        auto *d = qobject_cast<QInputDialog *>(w);
        if (!d) return false;
        d->setTextValue(value);
        d->accept();
        return true;
    };
}

void TstVcsGui::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("AmigaIDE-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_vcsgui"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings().clear();
    m_amivcs = QString::fromUtf8(qgetenv("AIDE_TEST_AMIVCS"));
    // commits need an author (AmiVCS 0.10): global defaults in a
    // HOME of our own, never the real ~/.amivcsconfig
    QVERIFY(m_home.isValid());
    qputenv("HOME", m_home.path().toUtf8());
    qputenv("USERPROFILE", m_home.path().toUtf8());
    QFile cfg(QDir(m_home.path()).filePath(QStringLiteral(".amivcsconfig")));
    QVERIFY(cfg.open(QFile::WriteOnly));
    cfg.write("name=Test Person\nemail=test@example.org\nverify_ssl=0\n");
    cfg.close();
}

void TstVcsGui::answerDialogs()
{
    QWidget *w = QApplication::activeModalWidget();
    if (!w) return;
    if (m_answers.isEmpty()) {
        // not part of the script (e.g. "Missing SDKs" in the test
        // environment): close it, remember it
        if (auto *box = qobject_cast<QMessageBox *>(w)) {
            m_unexpected << box->text().left(80);
            if (QAbstractButton *b = box->button(QMessageBox::Ok)) b->click();
            else if (QAbstractButton *b2 = box->button(QMessageBox::No)) b2->click();
            else box->reject();
        } else if (auto *d = qobject_cast<QDialog *>(w)) {
            m_unexpected << d->windowTitle();   // a dialog of ours: never hang on it
            d->reject();
        }
        return;
    }
    if (auto *box = qobject_cast<QMessageBox *>(w)) m_seen << box->text();
    if (auto *d = qobject_cast<QInputDialog *>(w)) m_seen << d->labelText();
    if (m_answers.first()(w)) m_answers.removeFirst();
}

bool TstVcsGui::waitIdle(int ms)
{
    // the runner is idle and no answer is pending
    auto *runner = m_win->findChild<VcsRunner *>();
    QElapsedTimer t;
    t.start();
    QTest::qWait(150);
    while (t.elapsed() < ms) {
        if (!runner->busy() && m_answers.isEmpty() && !QApplication::activeModalWidget()) {
            QTest::qWait(150);
            if (!runner->busy() && !QApplication::activeModalWidget()) return true;
        }
        QTest::qWait(50);
    }
    return false;
}

QPushButton *TstVcsGui::button(const QString &t) const
{
    for (QPushButton *b : m_win->findChild<VcsDock *>()->findChildren<QPushButton *>())
        if (b->text() == t) return b;
    return nullptr;
}

QAction *TstVcsGui::action(const QString &t) const
{
    for (QAction *a : m_win->findChildren<QAction *>())
        if (a->text() == t) return a;
    return nullptr;
}

QListWidget *TstVcsGui::list(int i) const
{
    return m_win->findChild<VcsDock *>()->findChildren<QListWidget *>().value(i);
}

QString TstVcsGui::branchText() const
{
    for (QLabel *l : m_win->findChild<VcsDock *>()->findChildren<QLabel *>())
        if (l->text().startsWith(QStringLiteral("Branch:"))) return l->text();
    return {};
}

void TstVcsGui::workflow()
{
    if (m_amivcs.isEmpty() || !QFile::exists(m_amivcs))
        QSKIP("AIDE_TEST_AMIVCS not set");
    AppSettings::instance().setAmivcsProgram(m_amivcs);

    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("demo"));
    QDir().mkpath(root + QStringLiteral("/src"));
    {
        QFile f(root + QStringLiteral("/src/main.c"));
        QVERIFY(f.open(QFile::WriteOnly));
        f.write("int main(void){return 0;}\n");
    }
    Project p = Project::createMinimalC(QStringLiteral("demo"));
    const QString aproj = root + QStringLiteral("/demo.aproj");
    QVERIFY(p.save(aproj));

    QTimer pump;
    connect(&pump, &QTimer::timeout, this, &TstVcsGui::answerDialogs);
    pump.start(50);

    MainWindow win;
    m_win = &win;
    win.show();
    QVERIFY(QMetaObject::invokeMethod(&win, "openProjectAt", Q_ARG(QString, aproj)));
    QVERIFY(waitIdle());

    // ---- create the repository; the ignore lines are offered
    QPushButton *init = button(QStringLiteral("Create Repository Here"));
    QVERIFY(init && !init->isHidden() && init->isEnabled());
    queue(yes);                                   // add .amivcsignore lines?
    init->click();
    QVERIFY(waitIdle());
    QVERIFY(QFile::exists(root + QStringLiteral("/.amivcs")));
    QFile ign(root + QStringLiteral("/.amivcsignore"));
    QVERIFY(ign.open(QFile::ReadOnly));
    const QByteArray ignText = ign.readAll();
    QVERIFY(ignText.contains("build/") && ignText.contains("*.aproj.user"));
    QCOMPARE(branchText(), QStringLiteral("Branch: main"));
    QCOMPARE(list(0)->count(), 3);                // .amivcsignore, demo.aproj, src/main.c
    for (const QString &u : m_unexpected)
        QVERIFY2(u.contains(QStringLiteral("SDK")), qPrintable(u));
    m_unexpected.clear();

    // ---- stage all, commit
    button(QStringLiteral("Stage All"))->click();
    QVERIFY(waitIdle());
    QCOMPARE(list(0)->count(), 0);
    QCOMPARE(list(1)->count(), 3);
    win.findChild<VcsDock *>()->findChild<QPlainTextEdit *>()->setPlainText(
        QStringLiteral("Erster Stand mit Größe"));
    button(QStringLiteral("Commit"))->click();
    QVERIFY(waitIdle());
    QCOMPARE(list(1)->count(), 0);
    auto *history = win.findChild<HistoryDock *>()->findChild<QTableWidget *>();
    QCOMPARE(history->rowCount(), 1);
    QVERIFY(history->item(0, 4)->text().contains(QStringLiteral("Größe")));

    // ---- new branch
    queue(text(QStringLiteral("dev")));
    action(QStringLiteral("&New Branch..."))->trigger();
    QVERIFY(waitIdle());
    QVERIFY(history->item(0, 4)->text().contains(QStringLiteral("dev")));

    // ---- unsaved edit, switch: saved first, refused while uncommitted
    auto *tabs = win.findChild<EditorTabWidget *>();
    EditorWidget *ed = tabs->openFile(root + QStringLiteral("/src/main.c"));
    QVERIFY(ed);
    ed->moveCursor(QTextCursor::End);
    ed->insertPlainText(QStringLiteral("/* geändert */\n"));
    queue(text(QStringLiteral("dev")));            // which branch
    queue(yes);                                    // save and continue?
    queue(yes);                                    // the refusal (OK)
    action(QStringLiteral("&Switch Branch..."))->trigger();
    QVERIFY(waitIdle());
    {
        QFile f(root + QStringLiteral("/src/main.c"));
        QVERIFY(f.open(QFile::ReadOnly));
        QVERIFY(f.readAll().contains("ge\xc3\xa4ndert"));   // saved before switching
    }
    QVERIFY(m_seen.last().contains(QStringLiteral("uncommitted")));
    QCOMPARE(branchText(), QStringLiteral("Branch: main"));

    // ---- commit everything, then switch: the editor is reloaded
    win.findChild<VcsDock *>()->findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("Kommentar"));
    win.findChild<VcsDock *>()->findChild<QCheckBox *>()->setChecked(true);
    button(QStringLiteral("Commit"))->click();
    QVERIFY(waitIdle());
    queue(text(QStringLiteral("dev")));
    queue(yes);
    action(QStringLiteral("&Switch Branch..."))->trigger();
    QVERIFY(waitIdle(30000));
    QCOMPARE(branchText(), QStringLiteral("Branch: dev"));
    ed = tabs->editorForPath(root + QStringLiteral("/src/main.c"));
    QVERIFY(ed);
    QVERIFY(!ed->toPlainText().contains(QStringLiteral("geändert")));

    // ---- merge main into dev (fast-forward), reloaded again
    queue(text(QStringLiteral("main")));
    queue(yes);
    action(QStringLiteral("&Merge Branch..."))->trigger();
    QVERIFY(waitIdle(30000));
    ed = tabs->editorForPath(root + QStringLiteral("/src/main.c"));
    QVERIFY(ed && ed->toPlainText().contains(QStringLiteral("geändert")));

    // ---- network: push (asks for the URL, then the login), clone, pull
    if (qgetenv("AIDE_TEST_AMIVCS_NET") != "1")
        return;
    const QString base = QString::fromUtf8(qgetenv("AIDE_TEST_AMIVCS_URL"));
    const QString user = QString::fromUtf8(qgetenv("AIDE_TEST_AMIVCS_USER"));
    const QString pass = QString::fromUtf8(qgetenv("AIDE_TEST_AMIVCS_PASS"));
    QVERIFY2(!base.isEmpty() && !user.isEmpty(), "AIDE_TEST_AMIVCS_URL/_USER/_PASS");
    const QString remote = QStringLiteral("%1/ide-gui-%2").arg(base).arg(QDateTime::currentSecsSinceEpoch());
    queue(text(remote));                          // the remote URL
    queue(login(user, QStringLiteral("falsch"))); // refused ...
    queue(login(user, pass));                     // ... asked again
    action(QStringLiteral("&Push"))->trigger();
    QVERIFY(waitIdle(60000));
    QVERIFY2(m_answers.isEmpty(), qPrintable(m_seen.join(QStringLiteral(" | ")) + QStringLiteral(" || ")
                                             + m_unexpected.join(QStringLiteral(" | "))));
    QVERIFY(m_seen.join(QLatin1Char(' ')).contains(QStringLiteral("has no remote yet")));
    {
        QFile c(root + QStringLiteral("/.amivcs/config"));
        QVERIFY(c.open(QFile::ReadOnly));
        const QByteArray cfgText = c.readAll();
        QVERIFY2(cfgText.contains("remote=" + remote.toUtf8()) && cfgText.contains("pass="), cfgText.constData());
    }

    // clone into a second directory; opens its project
    const QString parent = dir.filePath(QStringLiteral("clones"));
    QDir().mkpath(parent);
    // the directory dialog is native-free in offscreen tests: answer it too
    queue(text(remote));                          // the remote URL
    queue([parent](QWidget *w) {                  // directory
        auto *d = qobject_cast<QFileDialog *>(w);
        if (!d) return false;
        // the Qt dialog accepts its current directory when the name field
        // is empty (a name there would only be entered)
        d->setDirectory(parent);
        for (QLineEdit *e : d->findChildren<QLineEdit *>()) e->clear();
        QMetaObject::invokeMethod(d, "accept");
        return true;
    });
    queue(text(QStringLiteral("klon")));          // folder name
    queue(login(user, pass));                     // a clone has no login yet
    action(QStringLiteral("C&lone..."))->trigger();
    QVERIFY(waitIdle(60000));
    {
        QString diag = m_seen.join(QStringLiteral(" | ")) + QStringLiteral(" || ") + m_unexpected.join(QStringLiteral(" | "))
                       + QStringLiteral(" || answers left %1").arg(m_answers.size())
                       + QStringLiteral(" || in %1: ").arg(parent) + QDir(parent).entryList(QDir::AllEntries | QDir::Hidden).join(QLatin1Char(','))
                       + QStringLiteral(" / ") + QDir(parent + QStringLiteral("/klon")).entryList(QDir::AllEntries | QDir::Hidden).join(QLatin1Char(','))
                       + QStringLiteral(" || log: ") + win.findChildren<QPlainTextEdit *>().last()->toPlainText().right(400);
        diag.replace(QLatin1Char('\n'), QLatin1Char(' '));
        QVERIFY2(QFile::exists(parent + QStringLiteral("/klon/demo.aproj")), qPrintable(diag));
    }
    m_answers.clear();

    // a change pushed from the clone arrives in the first copy by pull
    {
        QFile f(parent + QStringLiteral("/klon/src/main.c"));
        QVERIFY(f.open(QFile::Append));
        f.write("/* aus dem Klon */\n");
    }
    win.findChild<VcsDock *>()->findChild<QPlainTextEdit *>()->setPlainText(QStringLiteral("aus dem Klon"));
    win.findChild<VcsDock *>()->findChild<QCheckBox *>()->setChecked(true);
    button(QStringLiteral("Commit"))->click();
    QVERIFY(waitIdle());
    action(QStringLiteral("&Push"))->trigger();
    QVERIFY(waitIdle(60000));

    // back in the first copy (on dev, which was pushed): pull
    QVERIFY(QMetaObject::invokeMethod(&win, "openProjectAt", Q_ARG(QString, aproj)));
    QVERIFY(waitIdle());
    QVERIFY(action(QStringLiteral("P&ull"))->isEnabled());
    queue(yes);                                   // save and continue?
    action(QStringLiteral("P&ull"))->trigger();
    QVERIFY(waitIdle(60000));
    QFile f(root + QStringLiteral("/src/main.c"));
    QVERIFY(f.open(QFile::ReadOnly));
    QString log = win.findChildren<QPlainTextEdit *>().last()->toPlainText().right(700);
    log += QStringLiteral(" || ") + m_unexpected.join(QStringLiteral(" | ")) + QStringLiteral(" || root ") + win.findChild<VcsRunner *>()->projectRoot();
    log.replace(QLatin1Char('\n'), QLatin1Char(' '));
    QVERIFY2(f.readAll().contains("aus dem Klon"), qPrintable(log));
}

QTEST_MAIN(TstVcsGui)
#include "tst_vcsgui.moc"
