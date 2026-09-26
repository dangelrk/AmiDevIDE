#include "aide/aide_logtail.h"

#include <QStringList>
#include <QTest>

#include <cstring>

// Host-testable in isolation (no dos.library/MUI dependency) -- guards
// the chunk-boundary line splitter used by the Amiga build's live log
// tailer (mainwindow.c's build_tick(), via aide_fs_read_range()).
class TstLogTail : public QObject {
    Q_OBJECT
private slots:
    void singleFeedMultipleLines();
    void lineSplitAcrossChunkBoundary();
    void crlfLineEndings();
    void trailingPartialLineHeldBack();
    void flushEmitsPartialLineAtEof();
    void flushOnCleanEofEmitsNothing();
    void emptyLinesPreserved();

private:
    static void collect(const char *line, void *userdata)
    {
        static_cast<QStringList *>(userdata)->append(QString::fromUtf8(line));
    }
};

void TstLogTail::singleFeedMultipleLines()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    const char *chunk = "line one\nline two\nline three\n";
    aide_logtail_feed(&t, chunk, strlen(chunk), &collect, &lines);

    QCOMPARE(lines, (QStringList{ "line one", "line two", "line three" }));
    aide_logtail_free(&t);
}

void TstLogTail::lineSplitAcrossChunkBoundary()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    // "hello world\n" arrives as two chunks, split mid-word.
    aide_logtail_feed(&t, "hello ", 6, &collect, &lines);
    QVERIFY(lines.isEmpty()); // nothing complete yet
    aide_logtail_feed(&t, "world\n", 6, &collect, &lines);

    QCOMPARE(lines, QStringList{ "hello world" });
    aide_logtail_free(&t);
}

void TstLogTail::crlfLineEndings()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    const char *chunk = "one\r\ntwo\r\n";
    aide_logtail_feed(&t, chunk, strlen(chunk), &collect, &lines);

    QCOMPARE(lines, (QStringList{ "one", "two" }));
    aide_logtail_free(&t);
}

void TstLogTail::trailingPartialLineHeldBack()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    const char *chunk = "complete\nincomplete";
    aide_logtail_feed(&t, chunk, strlen(chunk), &collect, &lines);

    QCOMPARE(lines, QStringList{ "complete" });

    // The next feed() completes it -- not duplicated, not dropped.
    aide_logtail_feed(&t, " line\n", 6, &collect, &lines);
    QCOMPARE(lines, (QStringList{ "complete", "incomplete line" }));
    aide_logtail_free(&t);
}

void TstLogTail::flushEmitsPartialLineAtEof()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    // No trailing '\n' ever arrives (e.g. the build process just exited).
    aide_logtail_feed(&t, "no newline here", 15, &collect, &lines);
    QVERIFY(lines.isEmpty());

    aide_logtail_flush(&t, &collect, &lines);
    QCOMPARE(lines, QStringList{ "no newline here" });
    aide_logtail_free(&t);
}

void TstLogTail::flushOnCleanEofEmitsNothing()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    const char *chunk = "clean line\n";
    aide_logtail_feed(&t, chunk, strlen(chunk), &collect, &lines);
    aide_logtail_flush(&t, &collect, &lines); // nothing left to flush

    QCOMPARE(lines, QStringList{ "clean line" });
    aide_logtail_free(&t);
}

void TstLogTail::emptyLinesPreserved()
{
    AideLogTail t;
    aide_logtail_init(&t);
    QStringList lines;

    const char *chunk = "a\n\nb\n";
    aide_logtail_feed(&t, chunk, strlen(chunk), &collect, &lines);

    QCOMPARE(lines, (QStringList{ "a", "", "b" }));
    aide_logtail_free(&t);
}

QTEST_APPLESS_MAIN(TstLogTail)
#include "tst_logtail.moc"
