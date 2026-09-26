#include "index/BasicSymbolParser.h"

#include <QTest>

// ACE BASIC outline parsing. The declaration forms below are taken from
// real sources in the ACE repo (submods/list/list.b) and from docs/ref.txt,
// not invented: STRUCT ... END STRUCT defines a type, while
// "DECLARE STRUCT Type [*] name" declares variables of it (ref.txt:587).
class TstBasicSymbolParser : public QObject {
    Q_OBJECT
private slots:
    void subsAndFunctions();
    void declarationsArePrototypes();
    void structDefinitionVersusStructVariable();
    void constsVariablesAndLabels();
    void commentsAndStringsDoNotConfuseIt();
    void includes();

private:
    static Symbol find(const BasicSymbolParser::Result &r, const QString &name);
};

Symbol TstBasicSymbolParser::find(const BasicSymbolParser::Result &r, const QString &name)
{
    for (const Symbol &s : r.symbols) {
        if (s.name == name)
            return s;
    }
    return Symbol{};
}

void TstBasicSymbolParser::subsAndFunctions()
{
    const auto r = BasicSymbolParser::parse(QStringLiteral(
        "SUB Greet\n"
        "END SUB\n"
        "SUB ADDRESS LCons%(SHORTINT v, ADDRESS tl) EXTERNAL\n"
        "FUNCTION STRING Name$\n"
        "METHOD Draw\n"), QStringLiteral("/p/main.b"));

    QCOMPARE(find(r, QStringLiteral("Greet")).kind, SymbolKind::Function);
    QCOMPARE(find(r, QStringLiteral("Greet")).line, 1);
    // return type before the name must not be mistaken for the name
    QCOMPARE(find(r, QStringLiteral("LCons%")).kind, SymbolKind::Function);
    QCOMPARE(find(r, QStringLiteral("Name$")).kind, SymbolKind::Function);
    QCOMPARE(find(r, QStringLiteral("Draw")).kind, SymbolKind::Function);
}

void TstBasicSymbolParser::declarationsArePrototypes()
{
    const auto r = BasicSymbolParser::parse(QStringLiteral(
        "DECLARE SUB Helper\n"
        "DECLARE FUNCTION LONGINT Total&\n"), QStringLiteral("/p/main.b"));

    QCOMPARE(find(r, QStringLiteral("Helper")).kind, SymbolKind::Prototype);
    QCOMPARE(find(r, QStringLiteral("Total&")).kind, SymbolKind::Prototype);
}

void TstBasicSymbolParser::structDefinitionVersusStructVariable()
{
    const auto r = BasicSymbolParser::parse(QStringLiteral(
        "STRUCT LCell\n"
        "  LONGINT car\n"
        "  ADDRESS cdr\n"
        "END STRUCT\n"
        "DECLARE STRUCT LCell *cel\n"), QStringLiteral("/p/list.b"));

    const Symbol cell = find(r, QStringLiteral("LCell"));
    QCOMPARE(cell.kind, SymbolKind::Struct);
    QCOMPARE(cell.line, 1);

    const Symbol car = find(r, QStringLiteral("car"));
    QCOMPARE(car.kind, SymbolKind::StructMember);
    QCOMPARE(car.scope, QStringLiteral("LCell"));
    QCOMPARE(car.signature, QStringLiteral("LONGINT"));

    // "DECLARE STRUCT LCell *cel" declares cel, not a type called LCell
    const Symbol cel = find(r, QStringLiteral("cel"));
    QCOMPARE(cel.kind, SymbolKind::Variable);
    QCOMPARE(cel.signature, QStringLiteral("LCell"));

    // ...and after END STRUCT, a typed line is a plain variable again
    const auto r2 = BasicSymbolParser::parse(QStringLiteral(
        "STRUCT S\n"
        "  LONGINT inside\n"
        "END STRUCT\n"
        "LONGINT outside\n"), QStringLiteral("/p/list.b"));
    QCOMPARE(find(r2, QStringLiteral("inside")).kind, SymbolKind::StructMember);
    QCOMPARE(find(r2, QStringLiteral("outside")).kind, SymbolKind::Variable);
}

void TstBasicSymbolParser::constsVariablesAndLabels()
{
    const auto r = BasicSymbolParser::parse(QStringLiteral(
        "CONST LNil = 0\n"
        "DIM arr(10)\n"
        "STRING name$ SIZE 40\n"
        "SHARED total&\n"
        "mainloop:\n"), QStringLiteral("/p/main.b"));

    QCOMPARE(find(r, QStringLiteral("LNil")).kind, SymbolKind::Define);
    QCOMPARE(find(r, QStringLiteral("arr")).kind, SymbolKind::Variable);
    QCOMPARE(find(r, QStringLiteral("name$")).kind, SymbolKind::Variable);
    QCOMPARE(find(r, QStringLiteral("total&")).kind, SymbolKind::Variable);
    QCOMPARE(find(r, QStringLiteral("mainloop")).kind, SymbolKind::Label);
    QCOMPARE(find(r, QStringLiteral("mainloop")).line, 5);
}

void TstBasicSymbolParser::commentsAndStringsDoNotConfuseIt()
{
    const auto r = BasicSymbolParser::parse(QStringLiteral(
        "REM SUB NotASub\n"
        "' SUB AlsoNotASub\n"
        "PRINT \"it's fine\"\n"
        "SUB Real\n"), QStringLiteral("/p/main.b"));

    QCOMPARE(r.symbols.size(), 1);
    QCOMPARE(r.symbols.first().name, QStringLiteral("Real"));
    QCOMPARE(r.symbols.first().line, 4);
}

void TstBasicSymbolParser::includes()
{
    const auto r = BasicSymbolParser::parse(QStringLiteral(
        "#include <ace/limits.h>\n"
        "#include \"local.h\"\n"), QStringLiteral("/p/main.b"));

    QCOMPARE(r.includes.size(), 2);
    QCOMPARE(r.includes.at(0), QStringLiteral("ace/limits.h"));
    QCOMPARE(r.includes.at(1), QStringLiteral("local.h"));
}

QTEST_APPLESS_MAIN(TstBasicSymbolParser)
#include "tst_basicsymbolparser.moc"
