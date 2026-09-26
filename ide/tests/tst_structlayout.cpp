#include "index/StructLayout.h"

#include <QTest>

// Verifies the m68k/AmigaOS layout rules (2-byte alignment ceiling -- a
// 4-byte LONG/pointer only needs to start on an even address, not a 4-byte
// boundary) against hand-verified real Amiga struct offsets: exec/nodes.h's
// Node and MinNode, exec/lists.h's List, and exec/ports.h's Message (which
// embeds a Node by value, exercising the knownStructSizes lookup).
class TstStructLayout : public QObject {
    Q_OBJECT
private slots:
    void computesNodeOffsets();
    void computesMinNodeOffsets();
    void computesListOffsets();
    void computesMessageOffsetsWithEmbeddedStruct();
    void handlesArrayMembers();
    void handlesUnknownEmbeddedStructAsZeroSize();
};

void TstStructLayout::computesNodeOffsets()
{
    // struct Node {
    //     struct Node *ln_Succ;
    //     struct Node *ln_Pred;
    //     UBYTE        ln_Type;
    //     BYTE         ln_Pri;
    //     char        *ln_Name;
    // };
    const QList<StructLayout::MemberDecl> members = {
        { QStringLiteral("struct Node*"), QStringLiteral("ln_Succ") },
        { QStringLiteral("struct Node*"), QStringLiteral("ln_Pred") },
        { QStringLiteral("UBYTE"), QStringLiteral("ln_Type") },
        { QStringLiteral("BYTE"), QStringLiteral("ln_Pri") },
        { QStringLiteral("char*"), QStringLiteral("ln_Name") },
    };
    const auto result = StructLayout::compute(members, {});

    QCOMPARE(result.members.size(), 5);
    QCOMPARE(result.members[0].offset, 0);  // ln_Succ
    QCOMPARE(result.members[1].offset, 4);  // ln_Pred
    QCOMPARE(result.members[2].offset, 8);  // ln_Type
    QCOMPARE(result.members[3].offset, 9);  // ln_Pri
    QCOMPARE(result.members[4].offset, 10); // ln_Name -- 4-byte pointer at an even (not 4-aligned) offset
    QCOMPARE(result.totalSize, 14);
}

void TstStructLayout::computesMinNodeOffsets()
{
    // struct MinNode { struct MinNode *mln_Succ; struct MinNode *mln_Pred; };
    const QList<StructLayout::MemberDecl> members = {
        { QStringLiteral("struct MinNode*"), QStringLiteral("mln_Succ") },
        { QStringLiteral("struct MinNode*"), QStringLiteral("mln_Pred") },
    };
    const auto result = StructLayout::compute(members, {});

    QCOMPARE(result.members[0].offset, 0);
    QCOMPARE(result.members[1].offset, 4);
    QCOMPARE(result.totalSize, 8);
}

void TstStructLayout::computesListOffsets()
{
    // struct List {
    //     struct Node *lh_Head;
    //     struct Node *lh_Tail;
    //     struct Node *lh_TailPred;
    //     UBYTE        lh_Type;
    //     UBYTE        lh_pad;
    // };
    const QList<StructLayout::MemberDecl> members = {
        { QStringLiteral("struct Node*"), QStringLiteral("lh_Head") },
        { QStringLiteral("struct Node*"), QStringLiteral("lh_Tail") },
        { QStringLiteral("struct Node*"), QStringLiteral("lh_TailPred") },
        { QStringLiteral("UBYTE"), QStringLiteral("lh_Type") },
        { QStringLiteral("UBYTE"), QStringLiteral("lh_pad") },
    };
    const auto result = StructLayout::compute(members, {});

    QCOMPARE(result.members[0].offset, 0);
    QCOMPARE(result.members[1].offset, 4);
    QCOMPARE(result.members[2].offset, 8);
    QCOMPARE(result.members[3].offset, 12);
    QCOMPARE(result.members[4].offset, 13);
    QCOMPARE(result.totalSize, 14);
}

void TstStructLayout::computesMessageOffsetsWithEmbeddedStruct()
{
    // struct Message {
    //     struct Node    mn_Node;      -- embedded BY VALUE, not a pointer
    //     struct MsgPort *mn_ReplyPort;
    //     UWORD           mn_Length;
    // };
    const QList<StructLayout::MemberDecl> members = {
        { QStringLiteral("struct Node"), QStringLiteral("mn_Node") },
        { QStringLiteral("struct MsgPort*"), QStringLiteral("mn_ReplyPort") },
        { QStringLiteral("UWORD"), QStringLiteral("mn_Length") },
    };
    const QHash<QString, int> knownSizes = { { QStringLiteral("Node"), 14 } };
    const auto result = StructLayout::compute(members, knownSizes);

    QCOMPARE(result.members[0].offset, 0);
    QCOMPARE(result.members[0].size, 14);
    QCOMPARE(result.members[1].offset, 14); // right after the embedded Node, already even
    QCOMPARE(result.members[2].offset, 18);
    QCOMPARE(result.totalSize, 20);
}

void TstStructLayout::handlesArrayMembers()
{
    const QList<StructLayout::MemberDecl> members = {
        { QStringLiteral("UBYTE"), QStringLiteral("id") },
        { QStringLiteral("UBYTE"), QStringLiteral("pad[4]") },
        { QStringLiteral("ULONG"), QStringLiteral("checksum") },
    };
    const auto result = StructLayout::compute(members, {});

    QCOMPARE(result.members[0].offset, 0);
    QCOMPARE(result.members[1].name, QStringLiteral("pad")); // array suffix stripped from the display name
    QCOMPARE(result.members[1].offset, 2); // 4-byte array needs 2-byte alignment, bumped up from 1
    QCOMPARE(result.members[1].size, 4);
    QCOMPARE(result.members[2].offset, 6); // right after the 4-byte array (2+4=6, already even)
    QCOMPARE(result.totalSize, 10);
}

void TstStructLayout::handlesUnknownEmbeddedStructAsZeroSize()
{
    // An embedded struct whose definition wasn't seen (e.g. from another
    // file) can't be sized -- documented limitation: it's treated as 0
    // bytes, so it doesn't crash, it just can't offset what follows it
    // correctly.
    const QList<StructLayout::MemberDecl> members = {
        { QStringLiteral("struct Unknown"), QStringLiteral("thing") },
    };
    const auto result = StructLayout::compute(members, {});

    QCOMPARE(result.members[0].size, 0);
    QCOMPARE(result.members[0].offset, 0);
    QCOMPARE(result.totalSize, 0);
}

QTEST_APPLESS_MAIN(TstStructLayout)
#include "tst_structlayout.moc"
