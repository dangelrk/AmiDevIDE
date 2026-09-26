#include "disasm/M68kDisassembler.h"

#include <QTest>

// Bytes captured from a real `vasmm68k_mot -Fhunk -m68000 -kick1hunks -nosym`
// run assembling:
//   moveq #5,d0 / move.l d0,d1 / move.w #$1234,d2 / add.l d1,d0 /
//   sub.w #4,d0 / cmp.l d0,d1 / lea $1000,a0 / jsr (a0) / bra.s skip /
//   nop / skip: rts
// (see tst_hunkreader.cpp for the full object-file round trip). Verifies
// the disassembler against genuine toolchain output, not hand-encoded
// opcodes. Note vasm silently promotes "sub.w #4,d0" to SUBQ since the
// immediate fits the quick range -- that's real assembler behavior, not
// a test quirk.
class TstM68kDisassembler : public QObject {
    Q_OBJECT
private slots:
    void decodesRealAssembledSequence();
    void decodesEorCmpmAddxSubx();
    void unknownOpcodeFallsBackGracefully();
    void neverStallsOnUnknownOpcodes();

private:
    static QByteArray code()
    {
        return QByteArray::fromHex(
            "7005" "2200" "343c1234" "d081" "5940" "b280" "41f81000" "4e90" "6002" "4e71" "4e75");
    }
};

void TstM68kDisassembler::decodesRealAssembledSequence()
{
    const QByteArray data = code();
    int offset = 0;
    QStringList texts;
    while (offset < data.size())
        texts << M68kDisassembler::decodeOne(data, offset, 0).text.simplified();

    const QStringList expected = {
        QStringLiteral("moveq.l #5,d0"),
        QStringLiteral("move.l d0,d1"),
        QStringLiteral("move.w #$1234,d2"),
        QStringLiteral("add.l d1,d0"),
        QStringLiteral("subq.w #4,d0"),
        QStringLiteral("cmp.l d0,d1"),
        QStringLiteral("lea $1000.w,a0"),
        QStringLiteral("jsr (a0)"),
        QStringLiteral("bra.s $00000018"), // targets the rts below (addr 0x14+2+2)
        QStringLiteral("nop"),
        QStringLiteral("rts"),
    };
    QCOMPARE(texts, expected);
}

void TstM68kDisassembler::decodesEorCmpmAddxSubx()
{
    // From a real vasm run: eor.l d0,d1 / cmpm.l (a0)+,(a1)+ / addx.l d0,d1 /
    // subx.l d0,d1 / addx.l -(a0),-(a1) / subx.l -(a0),-(a1) / cmp.l d0,d1 /
    // cmp.w (a0),d1 / or.l d0,d1 / or.l d0,(a1)
    const QByteArray data = QByteArray::fromHex("b181" "b388" "d380" "9380" "d388" "9388" "b280" "b250" "8280" "8191");
    int offset = 0;
    QStringList texts;
    while (offset < data.size())
        texts << M68kDisassembler::decodeOne(data, offset, 0).text.simplified();

    const QStringList expected = {
        QStringLiteral("eor.l d0,d1"),
        QStringLiteral("cmpm.l (a0)+,(a1)+"),
        QStringLiteral("addx.l d0,d1"),
        QStringLiteral("subx.l d0,d1"),
        QStringLiteral("addx.l -(a0),-(a1)"),
        QStringLiteral("subx.l -(a0),-(a1)"),
        QStringLiteral("cmp.l d0,d1"),
        QStringLiteral("cmp.w (a0),d1"),
        QStringLiteral("or.l d0,d1"),
        QStringLiteral("or.l d0,(a1)"),
    };
    QCOMPARE(texts, expected);
}

void TstM68kDisassembler::unknownOpcodeFallsBackGracefully()
{
    // 0xFFFF is not a valid 68000 opcode in our decode table.
    const QByteArray data = QByteArray::fromHex("ffff");
    int offset = 0;
    const auto ins = M68kDisassembler::decodeOne(data, offset, 0x1000);
    QCOMPARE(offset, 2);
    QCOMPARE(ins.address, quint32(0x1000));
    QVERIFY(ins.text.contains(QStringLiteral("dc.w")));
}

void TstM68kDisassembler::neverStallsOnUnknownOpcodes()
{
    // Regression guard: every reserved/unrecognized word in the 16-bit space
    // must still advance the offset by at least 2 bytes (a prior bug left
    // offset unmodified on the unmatched path, hanging any caller's loop).
    for (quint32 w = 0; w <= 0xFFFF; ++w) {
        QByteArray data(2, char(0));
        data[0] = char((w >> 8) & 0xFF);
        data[1] = char(w & 0xFF);
        int offset = 0;
        M68kDisassembler::decodeOne(data, offset, 0);
        QVERIFY(offset >= 2);
    }
}

QTEST_APPLESS_MAIN(TstM68kDisassembler)
#include "tst_m68kdisassembler.moc"
