#include "build/DiagnosticsParser.h"

#include <QTest>

// Lines captured verbatim from real vc/vbccm68k, vasmm68k_mot and vlink runs
// against the actual toolchain.
class TstDiagnosticsParser : public QObject {
    Q_OBJECT
private slots:
    void vcWarning();
    void vcError();
    void vasmError();
    void aceErrorWithFile();
    void aceErrorWithoutFile();
    void vlinkUndefinedSymbol();
    void contextAndSummaryLinesIgnored();
};

void TstDiagnosticsParser::vcWarning()
{
    const auto issue = DiagnosticsParser::parseLine(
        QStringLiteral(R"(warning 161 in line 2 of "bad.c": implicit declaration of function <undefined_function>)"));
    QVERIFY(issue.has_value());
    QCOMPARE(issue->severity, IssueSeverity::Warning);
    QCOMPARE(issue->file, QStringLiteral("bad.c"));
    QCOMPARE(issue->line, 2);
    QCOMPARE(issue->message, QStringLiteral("implicit declaration of function <undefined_function>"));
}

void TstDiagnosticsParser::vcError()
{
    const auto issue = DiagnosticsParser::parseLine(
        QStringLiteral(R"(error 76 in line 2 of "bad2.c": identifier expected)"));
    QVERIFY(issue.has_value());
    QCOMPARE(issue->severity, IssueSeverity::Error);
    QCOMPARE(issue->file, QStringLiteral("bad2.c"));
    QCOMPARE(issue->line, 2);
    QCOMPARE(issue->message, QStringLiteral("identifier expected"));
}

void TstDiagnosticsParser::vasmError()
{
    const auto issue = DiagnosticsParser::parseLine(
        QStringLiteral(R"(error 9 in line 1 of "badasm.s": instruction not supported on selected architecture)"));
    QVERIFY(issue.has_value());
    QCOMPARE(issue->severity, IssueSeverity::Error);
    QCOMPARE(issue->file, QStringLiteral("badasm.s"));
    QCOMPARE(issue->line, 1);
}

// ace prints no severity word and no error number, and only names a file
// once yap -g markers have been consumed (ACE repo, lex.c print_position).
void TstDiagnosticsParser::aceErrorWithFile()
{
    const auto issue = DiagnosticsParser::parseLine(
        QStringLiteral(R"(** Undefined function or array not dimensioned in line 3 of "incd/helper.b".)"));
    QVERIFY(issue.has_value());
    QCOMPARE(issue->severity, IssueSeverity::Error);
    QCOMPARE(issue->file, QStringLiteral("incd/helper.b"));
    QCOMPARE(issue->line, 3);
    QCOMPARE(issue->message, QStringLiteral("Undefined function or array not dimensioned"));
}

void TstDiagnosticsParser::aceErrorWithoutFile()
{
    const auto issue = DiagnosticsParser::parseLine(
        QStringLiteral("** Identifier expected in line 12."));
    QVERIFY(issue.has_value());
    QCOMPARE(issue->severity, IssueSeverity::Error);
    QCOMPARE(issue->file, QString());
    QCOMPARE(issue->line, 12);
    QCOMPARE(issue->message, QStringLiteral("Identifier expected"));
}

void TstDiagnosticsParser::vlinkUndefinedSymbol()
{
    const auto issue = DiagnosticsParser::parseLine(
        QStringLiteral("Error 21: bad3.o (CODE+0x2): Reference to undefined symbol _totally_undefined_symbol."));
    QVERIFY(issue.has_value());
    QCOMPARE(issue->severity, IssueSeverity::Error);
    QCOMPARE(issue->file, QStringLiteral("bad3.o"));
    QCOMPARE(issue->line, 0);
    QVERIFY(issue->message.contains(QStringLiteral("undefined symbol")));
}

void TstDiagnosticsParser::contextAndSummaryLinesIgnored()
{
    QVERIFY(!DiagnosticsParser::parseLine(QStringLiteral(">    int x = ;")).has_value());
    QVERIFY(!DiagnosticsParser::parseLine(QStringLiteral("1 error found!")).has_value());
    QVERIFY(!DiagnosticsParser::parseLine(QStringLiteral("aborting...")).has_value());
    QVERIFY(!DiagnosticsParser::parseLine(QStringLiteral("bad3.o: In \"_main\":")).has_value());
}

QTEST_APPLESS_MAIN(TstDiagnosticsParser)
#include "tst_diagnosticsparser.moc"
