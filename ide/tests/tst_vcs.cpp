#include "aide/aide_vcs.h"
#include "aide/aide_project.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <cstdlib>
#include <cstring>

// aide_vcs.c builds amivcs jobs and reads amivcs --porcelain-out results
// (AmiVCS format 1). Golden texts below are what amivcs 0.6 writes; an
// optional run against a real amivcs is in realAmivcs().
class TstVcs : public QObject {
    Q_OBJECT
private slots:
    void parseStatus();
    void parseLogAndMessages();
    void unknownRecordsAndNewerFormat();
    void missingEnd();
    void fileState();
    void jobFiles();
    void cmdlineQuoting();
    void ignoreLines();
    void opClasses();
    void remotesAndLogin();
    void realAmivcs();
};

void TstVcs::parseStatus()
{
    AideVcsResult r;
    aide_vcs_result_init(&r);
    const char *t =
        "format\t1\n"
        "branch\tmain\n"
        "change\tS\tN\tsrc/neu.c\n"
        "change\tU\tM\tsrc/main.c\n"
        "change\tU\tN\ttab\\tname.c\n"
        "end\t0\n";
    QVERIFY(aide_vcs_parse(&r, t));
    QCOMPARE(r.format, 1);
    QVERIFY(r.ended);
    QCOMPARE(r.rc, 0);
    QVERIFY(r.haveHead);
    QVERIFY(!r.detached);
    QCOMPARE(QString::fromUtf8(r.branch), QStringLiteral("main"));
    QCOMPARE(r.changeCount, size_t(3));
    QCOMPARE(r.changes[0].staged, 'S');
    QCOMPARE(r.changes[1].kind, 'M');
    QCOMPARE(QString::fromUtf8(r.changes[2].path), QStringLiteral("tab\tname.c"));
    aide_vcs_result_free(&r);

    aide_vcs_result_init(&r);
    QVERIFY(aide_vcs_parse(&r, "format\t1\ndetached\tabcdef\nend\t0\n"));
    QVERIFY(r.detached);
    QCOMPARE(QString::fromUtf8(r.branch), QStringLiteral("abcdef"));
    aide_vcs_result_free(&r);
}

void TstVcs::parseLogAndMessages()
{
    AideVcsResult r;
    aide_vcs_result_init(&r);
    const char *t =
        "format\t1\n"
        "commit\taa\tbb,cc\t1793595912\tR\xc3\xbc" "diger\tmain,dev\tMerge\\nzweite Zeile mit \\\\ und \\t\n"
        "commit\tbb\t\t1793595000\tTester\t\tErster\n"
        "msg\twarn\tconflict: a.c\n"
        "msg\twarn\tconflict: b.c\n"
        "msg\terror\tamivcs: 2 conflict(s)\n"
        "ref\t*\tmain\n"
        "ref\t-\tdev\n"
        "progress\t3\t10\tReceiving objects\n"
        "committed\taa\n"
        "end\t1\n";
    QVERIFY(aide_vcs_parse(&r, t));
    QCOMPARE(r.commitCount, size_t(2));
    QCOMPARE(QString::fromUtf8(r.commits[0].parents), QStringLiteral("bb,cc"));
    QCOMPARE(r.commits[0].mtime, 1793595912L);
    QCOMPARE(QString::fromUtf8(r.commits[0].author), QStringLiteral("R\u00fcdiger"));
    QCOMPARE(QString::fromUtf8(r.commits[0].message), QStringLiteral("Merge\nzweite Zeile mit \\ und \t"));
    QCOMPARE(QString::fromUtf8(r.commits[1].parents), QString());
    QCOMPARE(r.messageCount, size_t(3));
    QCOMPARE(r.messages[1].level, 1);
    QCOMPARE(QString::fromUtf8(aide_vcs_result_error(&r)), QStringLiteral("amivcs: 2 conflict(s)"));
    QCOMPARE(r.refs.count, size_t(2));
    QCOMPARE(QString::fromUtf8(r.currentRef), QStringLiteral("main"));
    QCOMPARE(r.progressDone, 3L);
    QCOMPARE(QString::fromUtf8(r.committed), QStringLiteral("aa"));
    QCOMPARE(r.rc, 1);
    aide_vcs_result_free(&r);
}

void TstVcs::unknownRecordsAndNewerFormat()
{
    AideVcsResult r;
    aide_vcs_result_init(&r);
    QVERIFY(aide_vcs_parse(&r, "format\t1\nshiny\tnew\trecord\nbranch\tmain\nend\t0\n"));
    QCOMPARE(QString::fromUtf8(r.branch), QStringLiteral("main"));
    aide_vcs_result_free(&r);

    aide_vcs_result_init(&r);
    QVERIFY(!aide_vcs_parse(&r, "format\t2\nend\t0\n"));   // newer than this IDE
    aide_vcs_result_free(&r);
    aide_vcs_result_init(&r);
    QVERIFY(!aide_vcs_parse(&r, "AMIVCS v0.5\n"));          // no format: too old
    aide_vcs_result_free(&r);
}

void TstVcs::missingEnd()
{
    AideVcsResult r;
    aide_vcs_result_init(&r);
    QVERIFY(aide_vcs_parse(&r, "format\t1\nbranch\tmain\n"));
    QVERIFY(!r.ended);
    QCOMPARE(r.rc, -1);
    aide_vcs_result_free(&r);
}

void TstVcs::fileState()
{
    AideVcsResult r;
    aide_vcs_result_init(&r);
    QVERIFY(aide_vcs_parse(&r,
        "format\t1\nchange\tS\tM\tsrc/a.c\nchange\tS\tN\tsrc/b.c\nchange\tU\tM\tsrc/b.c\n"
        "change\tU\tD\tSrc/C.c\nchange\tU\tN\tsrc/x.c.theirs\nend\t0\n"));
    QCOMPARE(aide_vcs_file_state(&r, "src/a.c", 0), 'S');
    QCOMPARE(aide_vcs_file_state(&r, "src/b.c", 0), 'M');     // unstaged work left
    QCOMPARE(aide_vcs_file_state(&r, "src/c.c", 0), '\0');
    QCOMPARE(aide_vcs_file_state(&r, "src/c.c", 1), 'D');     // Amiga: case-insensitive
    QCOMPARE(aide_vcs_file_state(&r, "src/clean.c", 0), '\0');
    QCOMPARE(aide_vcs_file_state(&r, "../lib/x.c", 0), '\0');
    QVERIFY(aide_vcs_has_theirs(&r));
    aide_vcs_result_free(&r);
}

void TstVcs::jobFiles()
{
    QTemporaryDir dir;
    const QByteArray base = dir.filePath(QStringLiteral("job")).toUtf8();
    AideStrList items;
    aide_strlist_init(&items);
    aide_strlist_push(&items, "src/main.c");
    aide_strlist_push(&items, "tab\there.c");
    AideVcsJob job;
    QVERIFY(aide_vcs_job_build(&job, AIDE_VCS_ADD, "/opt/amivcs", base.constData(), &items, nullptr, 0, 0));
    QStringList argv;
    for (size_t i = 0; i < job.argv.count; i++) argv << QString::fromUtf8(job.argv.items[i]);
    QCOMPARE(argv, (QStringList{ "/opt/amivcs", "--porcelain-out", base + ".pc", "--args-from", base + ".args", "add" }));
    QFile args(QString::fromUtf8(job.argsPath));
    QVERIFY(args.open(QFile::ReadOnly));
    QCOMPARE(args.readAll(), QByteArray("src/main.c\ntab\\there.c\n"));
    args.close();
    aide_vcs_job_free(&job);
    QVERIFY(!QFile::exists(QString::fromUtf8(base + ".args")));

    QVERIFY(aide_vcs_job_build(&job, AIDE_VCS_COMMIT, "amivcs", base.constData(), nullptr,
                               "Gr\xc3\xbc\xc3\x9f" "e\n\"zwei\" ; $x", 1, 0));
    argv.clear();
    for (size_t i = 0; i < job.argv.count; i++) argv << QString::fromUtf8(job.argv.items[i]);
    QCOMPARE(argv.mid(3), (QStringList{ "commit", "-F", base + ".msg", "-a" }));
    QFile msg(QString::fromUtf8(job.messagePath));
    QVERIFY(msg.open(QFile::ReadOnly));
    QCOMPARE(msg.readAll(), QByteArray("Gr\xc3\xbc\xc3\x9f" "e\n\"zwei\" ; $x"));
    aide_vcs_job_free(&job);

    QVERIFY(aide_vcs_job_build(&job, AIDE_VCS_LOG, "amivcs", base.constData(), nullptr, nullptr, 0, 200));
    argv.clear();
    for (size_t i = 0; i < job.argv.count; i++) argv << QString::fromUtf8(job.argv.items[i]);
    QCOMPARE(argv.mid(3), (QStringList{ "log", "--all", "-n", "200" }));
    aide_vcs_job_free(&job);
    aide_strlist_free(&items);
}

void TstVcs::cmdlineQuoting()
{
    AideVcsJob job;
    QVERIFY(aide_vcs_job_build(&job, AIDE_VCS_STATUS, "Work:Tools 2/amivcs", "T:aide-vcs", nullptr, nullptr, 0, 0));
    char *line = aide_vcs_job_cmdline(&job);
    QCOMPARE(QString::fromUtf8(line),
             QStringLiteral("\"Work:Tools 2/amivcs\" \"--porcelain-out\" \"T:aide-vcs.pc\" \"status\""));
    free(line);
    aide_vcs_job_free(&job);
}

void TstVcs::ignoreLines()
{
    AideProject p;
    aide_project_init(&p);
    aide_project_create_minimal_c(&p, "demo");
    AideStrList out;
    aide_strlist_init(&out);
    aide_vcs_ignore_missing(&p, "# AMIVCS\nbin/\n#?.o\n", &out);
    QStringList got;
    for (size_t i = 0; i < out.count; i++) got << QString::fromUtf8(out.items[i]);
    QCOMPARE(got, (QStringList{ "build/", "*.aproj.user" }));   // bin/ is already there
    aide_strlist_free(&out);

    QTemporaryDir dir;
    const QByteArray root = dir.path().toUtf8();
    aide_strlist_init(&out);
    aide_vcs_ignore_missing(&p, nullptr, &out);
    QVERIFY(aide_vcs_ignore_append(root.constData(), &out));
    QFile f(dir.filePath(QStringLiteral(".amivcsignore")));
    QVERIFY(f.open(QFile::ReadOnly));
    const QByteArray text = f.readAll();
    QVERIFY(text.contains("build/\n"));
    QVERIFY(text.contains("bin/\n"));
    QVERIFY(text.contains("*.aproj.user\n"));
    AideStrList again;
    aide_strlist_init(&again);
    aide_vcs_ignore_missing(&p, text.constData(), &again);
    QCOMPARE(again.count, size_t(0));                        // nothing missing any more
    aide_strlist_free(&again);
    aide_strlist_free(&out);
    aide_project_free(&p);
}

void TstVcs::opClasses()
{
    QVERIFY(!aide_vcs_op_writes(AIDE_VCS_STATUS));
    QVERIFY(!aide_vcs_op_writes(AIDE_VCS_LOG));
    QVERIFY(aide_vcs_op_writes(AIDE_VCS_ADD));
    QVERIFY(aide_vcs_op_writes(AIDE_VCS_PULL));
    QVERIFY(aide_vcs_op_touches_files(AIDE_VCS_SWITCH));
    QVERIFY(!aide_vcs_op_touches_files(AIDE_VCS_COMMIT));
    QVERIFY(aide_vcs_op_network(AIDE_VCS_PUSH));
    QVERIFY(!aide_vcs_op_network(AIDE_VCS_MERGE));
}

// Remote URLs (one per repository, AmiVCS 0.10), the auth-required
// record and the login items that go into --args-from.
void TstVcs::remotesAndLogin()
{
    QVERIFY(aide_vcs_is_url("https://amivcs.example.com/proj"));
    QVERIFY(aide_vcs_is_url("https://host/sub/dir/proj/"));
    QVERIFY(!aide_vcs_is_url("https://amivcs.example.com"));
    QVERIFY(!aide_vcs_is_url("https://amivcs.example.com/"));
    QVERIFY(!aide_vcs_is_url("proj"));
    QVERIFY(!aide_vcs_is_url("://host/proj"));
    char *n = aide_vcs_url_name("https://host/sub/proj/");
    QCOMPARE(QByteArray(n), QByteArray("proj"));
    free(n);
    n = aide_vcs_url_name("https://host");
    QCOMPARE(QByteArray(n), QByteArray(""));
    free(n);

    AideVcsResult r;
    aide_vcs_result_init(&r);
    QVERIFY(aide_vcs_parse(&r, "format\t1\nauth-required\thttps://h/p\tbob\n"
                               "msg\terror\tamivcs: https://h/p needs a user and password\nend\t1\n"));
    QVERIFY(r.authRequired);
    QCOMPARE(QByteArray(r.authUrl), QByteArray("https://h/p"));
    QCOMPARE(QByteArray(r.authUser), QByteArray("bob"));
    QVERIFY(!aide_vcs_needs_remote_url(&r));
    aide_vcs_result_free(&r);
    aide_vcs_result_init(&r);
    QVERIFY(aide_vcs_parse(&r, "format\t1\nmsg\terror\tamivcs: no remote URL set -- give one\nend\t1\n"));
    QVERIFY(!r.authRequired);
    QVERIFY(aide_vcs_needs_remote_url(&r));
    aide_vcs_result_free(&r);

    AideStrList items, out;
    aide_strlist_init(&items);
    aide_strlist_push(&items, "https://h/p");
    aide_vcs_items_with_login(&items, "bob", "ge\theim", 1, &out);
    QStringList got;
    for (size_t i = 0; i < out.count; i++) got << QString::fromUtf8(out.items[i]);
    QCOMPARE(got, (QStringList{ "--user", "bob", "--password", "ge\theim", "--remember-password", "https://h/p" }));
    aide_strlist_free(&out);
    aide_vcs_items_with_login(&items, "", nullptr, 0, &out);
    QCOMPARE(out.count, size_t(1));
    aide_strlist_free(&out);

    // a push with login items: --args-from carries everything, escaped
    QTemporaryDir dir;
    const QByteArray base = dir.filePath(QStringLiteral("job")).toUtf8();
    aide_vcs_items_with_login(&items, "bob", "ge\theim", 0, &out);
    AideVcsJob job;
    QVERIFY(aide_vcs_job_build(&job, AIDE_VCS_PUSH, "amivcs", base.constData(), &out, nullptr, 0, 0));
    QFile args(QString::fromUtf8(job.argsPath));
    QVERIFY(args.open(QFile::ReadOnly));
    QCOMPARE(args.readAll(), QByteArray("--user\nbob\n--password\nge\\theim\nhttps://h/p\n"));
    args.close();
    aide_vcs_job_free(&job);
    aide_strlist_free(&out);
    aide_strlist_free(&items);
}

// Runs the jobs against a real amivcs if AIDE_TEST_AMIVCS names one
// (skipped otherwise): init, add, commit, status, log, and a push that
// has no login yet.
void TstVcs::realAmivcs()
{
    const QByteArray prog = qgetenv("AIDE_TEST_AMIVCS");
    if (prog.isEmpty() || !QFile::exists(QString::fromUtf8(prog)))
        QSKIP("AIDE_TEST_AMIVCS not set");
    QTemporaryDir dir;
    QVERIFY(QDir::setCurrent(dir.path()));
    // commits need a name (~/.amivcsconfig); keep the real one out of it
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
    QFile cfg(QDir(home.path()).filePath(QStringLiteral(".amivcsconfig")));
    QVERIFY(cfg.open(QFile::WriteOnly));
    cfg.write("name=Test Person\nemail=test@example.org\n");
    cfg.close();
    QFile src(QStringLiteral("main.c"));
    QVERIFY(src.open(QFile::WriteOnly));
    src.write("int main(void){return 0;}\n");
    src.close();
    const QByteArray base = dir.filePath(QStringLiteral("job")).toUtf8();
    auto run = [&](AideVcsOp op, const QStringList &items, const char *msg, AideVcsResult *r) {
        AideStrList l;
        aide_strlist_init(&l);
        for (const QString &s : items) aide_strlist_push(&l, s.toUtf8().constData());
        AideVcsJob job;
        if (!aide_vcs_job_build(&job, op, prog.constData(), base.constData(), &l, msg, 0, 50)) return false;
        QStringList args;
        for (size_t i = 1; i < job.argv.count; i++) args << QString::fromUtf8(job.argv.items[i]);
        QProcess p;
        p.start(QString::fromUtf8(job.argv.items[0]), args);
        p.waitForFinished(30000);
        aide_vcs_result_init(r);
        const bool ok = aide_vcs_parse_file(r, job.resultPath);
        aide_vcs_job_free(&job);
        aide_strlist_free(&l);
        return ok;
    };
    AideVcsResult r;
    QVERIFY(run(AIDE_VCS_INIT, {}, nullptr, &r));
    QCOMPARE(r.rc, 0);
    aide_vcs_result_free(&r);
    QVERIFY(run(AIDE_VCS_ADD, { "main.c" }, nullptr, &r));
    QCOMPARE(aide_vcs_file_state(&r, "main.c", 0), 'S');
    aide_vcs_result_free(&r);
    QVERIFY(run(AIDE_VCS_COMMIT, {}, "Erster Stand\nmit zwei Zeilen", &r));
    QVERIFY(r.committed);
    aide_vcs_result_free(&r);
    QVERIFY(run(AIDE_VCS_LOG, {}, nullptr, &r));
    QCOMPARE(r.commitCount, size_t(1));
    QCOMPARE(QString::fromUtf8(r.commits[0].message), QStringLiteral("Erster Stand\nmit zwei Zeilen"));
    QCOMPARE(QByteArray(r.commits[0].author), QByteArray("Test Person <test@example.org>"));
    aide_vcs_result_free(&r);
    // no remote yet
    QVERIFY(run(AIDE_VCS_PUSH, {}, nullptr, &r));
    QVERIFY(r.rc != 0);
    QVERIFY(aide_vcs_needs_remote_url(&r));
    aide_vcs_result_free(&r);
    // a URL but no login: amivcs asks the program instead of the network
    QVERIFY(run(AIDE_VCS_PUSH, { "https://amivcs.invalid/proj" }, nullptr, &r));
    QVERIFY(r.rc != 0);
    QVERIFY(r.authRequired);
    QCOMPARE(QByteArray(r.authUrl), QByteArray("https://amivcs.invalid/proj"));
    aide_vcs_result_free(&r);
}

QTEST_MAIN(TstVcs)
#include "tst_vcs.moc"
