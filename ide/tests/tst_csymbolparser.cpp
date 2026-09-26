#include "index/CSymbolParser.h"

#include <QTest>

namespace {
const Symbol *findMember(const QList<Symbol> &symbols, const QString &scope, const QString &name)
{
    for (const Symbol &s : symbols) {
        if (s.kind == SymbolKind::StructMember && s.scope == scope && s.name == name)
            return &s;
    }
    return nullptr;
}

const Symbol *findSymbol(const QList<Symbol> &symbols, const QString &name, SymbolKind kind)
{
    for (const Symbol &s : symbols) {
        if (s.kind == kind && s.name == name)
            return &s;
    }
    return nullptr;
}
}

class TstCSymbolParser : public QObject {
    Q_OBJECT
private slots:
    void findsDefineTypedefStructAndFunction();
    void findsIncludes();
    void findsBlockTypedef();
    void findsTaggedStructMembersWithOffsets();
    void findsTypedefStructMembersWithOffsets();
    void findsEmbeddedStructMemberUsingEarlierStructInSameFile();
};

void TstCSymbolParser::findsDefineTypedefStructAndFunction()
{
    const QString src = R"(
#define VERSION 1

struct Point {
    int x;
    int y;
};

int add(int a, int b);

int add(int a, int b) {
    return a + b;
}
)";
    const auto result = CSymbolParser::parse(src, QStringLiteral("main.c"));

    bool foundDefine = false, foundStruct = false, foundProto = false, foundFunc = false;
    for (const Symbol &s : result.symbols) {
        if (s.name == QLatin1String("VERSION") && s.kind == SymbolKind::Define)
            foundDefine = true;
        if (s.name == QLatin1String("Point") && s.kind == SymbolKind::Struct)
            foundStruct = true;
        if (s.name == QLatin1String("add") && s.kind == SymbolKind::Prototype)
            foundProto = true;
        if (s.name == QLatin1String("add") && s.kind == SymbolKind::Function)
            foundFunc = true;
    }
    QVERIFY(foundDefine);
    QVERIFY(foundStruct);
    QVERIFY(foundProto);
    QVERIFY(foundFunc);
}

void TstCSymbolParser::findsIncludes()
{
    const QString src = "#include <exec/types.h>\n#include \"utils.h\"\n";
    const auto result = CSymbolParser::parse(src, QStringLiteral("main.c"));
    QCOMPARE(result.includes.size(), 2);
    QCOMPARE(result.includes[0].first, QStringLiteral("exec/types.h"));
    QCOMPARE(result.includes[0].second, false);
    QCOMPARE(result.includes[1].first, QStringLiteral("utils.h"));
    QCOMPARE(result.includes[1].second, true);
}

void TstCSymbolParser::findsBlockTypedef()
{
    const QString src = R"(
typedef struct {
    int x;
} Point;
)";
    const auto result = CSymbolParser::parse(src, QStringLiteral("main.c"));
    bool found = false;
    for (const Symbol &s : result.symbols) {
        if (s.name == QLatin1String("Point") && s.kind == SymbolKind::Typedef)
            found = true;
    }
    QVERIFY(found);
}

void TstCSymbolParser::findsTaggedStructMembersWithOffsets()
{
    // Real exec/nodes.h Node layout: verifies StructMember extraction for a
    // tagged struct, including the m68k "4-byte fields only need 2-byte
    // alignment" quirk (ln_Name sits at offset 10, not 12).
    const QString src = R"(
struct Node {
    struct Node *ln_Succ;
    struct Node *ln_Pred;
    UBYTE ln_Type;
    BYTE ln_Pri;
    char *ln_Name;
};
)";
    const auto result = CSymbolParser::parse(src, QStringLiteral("nodes.h"));

    const Symbol *succ = findMember(result.symbols, QStringLiteral("Node"), QStringLiteral("ln_Succ"));
    const Symbol *pred = findMember(result.symbols, QStringLiteral("Node"), QStringLiteral("ln_Pred"));
    const Symbol *type = findMember(result.symbols, QStringLiteral("Node"), QStringLiteral("ln_Type"));
    const Symbol *pri = findMember(result.symbols, QStringLiteral("Node"), QStringLiteral("ln_Pri"));
    const Symbol *name = findMember(result.symbols, QStringLiteral("Node"), QStringLiteral("ln_Name"));
    QVERIFY(succ); QVERIFY(pred); QVERIFY(type); QVERIFY(pri); QVERIFY(name);
    QCOMPARE(succ->offset, 0);
    QCOMPARE(pred->offset, 4);
    QCOMPARE(type->offset, 8);
    QCOMPARE(pri->offset, 9);
    QCOMPARE(name->offset, 10);

    const Symbol *nodeStruct = findSymbol(result.symbols, QStringLiteral("Node"), SymbolKind::Struct);
    QVERIFY(nodeStruct);
    QCOMPARE(nodeStruct->offset, 14); // total size, backfilled after the closing brace
}

void TstCSymbolParser::findsTypedefStructMembersWithOffsets()
{
    const QString src = R"(
typedef struct {
    int x;
    int y;
} Point;
)";
    const auto result = CSymbolParser::parse(src, QStringLiteral("point.h"));

    const Symbol *x = findMember(result.symbols, QStringLiteral("Point"), QStringLiteral("x"));
    const Symbol *y = findMember(result.symbols, QStringLiteral("Point"), QStringLiteral("y"));
    QVERIFY(x);
    QVERIFY(y);
    QCOMPARE(x->offset, 0);
    QCOMPARE(y->offset, 4);

    const Symbol *pointTypedef = findSymbol(result.symbols, QStringLiteral("Point"), SymbolKind::Typedef);
    QVERIFY(pointTypedef);
    QCOMPARE(pointTypedef->offset, 8);
}

void TstCSymbolParser::findsEmbeddedStructMemberUsingEarlierStructInSameFile()
{
    // struct Message embeds struct Node BY VALUE -- CSymbolParser must have
    // already computed Node's size (from parsing it earlier in the same
    // text) to offset mn_ReplyPort/mn_Length correctly.
    const QString src = R"(
struct Node {
    struct Node *ln_Succ;
    struct Node *ln_Pred;
    UBYTE ln_Type;
    BYTE ln_Pri;
    char *ln_Name;
};

struct Message {
    struct Node mn_Node;
    struct MsgPort *mn_ReplyPort;
    UWORD mn_Length;
};
)";
    const auto result = CSymbolParser::parse(src, QStringLiteral("ports.h"));

    const Symbol *mnNode = findMember(result.symbols, QStringLiteral("Message"), QStringLiteral("mn_Node"));
    const Symbol *replyPort = findMember(result.symbols, QStringLiteral("Message"), QStringLiteral("mn_ReplyPort"));
    const Symbol *length = findMember(result.symbols, QStringLiteral("Message"), QStringLiteral("mn_Length"));
    QVERIFY(mnNode);
    QVERIFY(replyPort);
    QVERIFY(length);
    QCOMPARE(mnNode->offset, 0);
    QCOMPARE(replyPort->offset, 14); // right after the embedded 14-byte Node
    QCOMPARE(length->offset, 18);

    const Symbol *messageStruct = findSymbol(result.symbols, QStringLiteral("Message"), SymbolKind::Struct);
    QVERIFY(messageStruct);
    QCOMPARE(messageStruct->offset, 20);
}

QTEST_APPLESS_MAIN(TstCSymbolParser)
#include "tst_csymbolparser.moc"
