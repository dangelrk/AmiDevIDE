#include "index/AsmSymbolParser.h"

#include <QTest>

class TstAsmSymbolParser : public QObject {
    Q_OBJECT
private slots:
    void findsGlobalAndLocalLabels();
    void findsEquAndMacro();
    void findsIncludeAndIncdir();
};

void TstAsmSymbolParser::findsGlobalAndLocalLabels()
{
    const QString src =
        "_main:\n"
        "\tmoveq\t#0,d0\n"
        "\tbra\t.loop\n"
        ".loop\n"
        "\trts\n"
        "other$\n"
        "\trts\n";
    const auto result = AsmSymbolParser::parse(src, QStringLiteral("main.s"));

    bool foundMain = false, foundLoop = false, foundOther = false;
    for (const Symbol &s : result.symbols) {
        if (s.name == QLatin1String("_main") && s.kind == SymbolKind::Label)
            foundMain = true;
        if (s.name == QLatin1String(".loop") && s.kind == SymbolKind::LocalLabel && s.scope == QLatin1String("_main"))
            foundLoop = true;
        if (s.name == QLatin1String("other$") && s.kind == SymbolKind::LocalLabel)
            foundOther = true;
    }
    QVERIFY(foundMain);
    QVERIFY(foundLoop);
    QVERIFY(foundOther);
}

void TstAsmSymbolParser::findsEquAndMacro()
{
    const QString src =
        "STACKSIZE equ 8192\n"
        "MyMacro macro\n"
        "\tendm\n";
    const auto result = AsmSymbolParser::parse(src, QStringLiteral("main.s"));

    bool foundEqu = false, foundMacro = false;
    for (const Symbol &s : result.symbols) {
        if (s.name == QLatin1String("STACKSIZE") && s.kind == SymbolKind::EquConstant)
            foundEqu = true;
        if (s.name == QLatin1String("MyMacro") && s.kind == SymbolKind::AsmMacro)
            foundMacro = true;
    }
    QVERIFY(foundEqu);
    QVERIFY(foundMacro);
}

void TstAsmSymbolParser::findsIncludeAndIncdir()
{
    const QString src =
        "\tinclude\t\"hw.i\"\n"
        "\tincdir\tinclude\n";
    const auto result = AsmSymbolParser::parse(src, QStringLiteral("main.s"));
    QCOMPARE(result.includes.size(), 2);
    QCOMPARE(result.includes[0].first, QStringLiteral("hw.i"));
    QCOMPARE(result.includes[0].second, false);
    QCOMPARE(result.includes[1].first, QStringLiteral("include"));
    QCOMPARE(result.includes[1].second, true);
}

QTEST_APPLESS_MAIN(TstAsmSymbolParser)
#include "tst_asmsymbolparser.moc"
