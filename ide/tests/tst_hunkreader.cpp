#include "disasm/HunkReader.h"

#include <QTemporaryDir>
#include <QTest>

// The CODE hunk bytes below were captured from a real `vasmm68k_mot -Fhunk
// -m68000 -kick1hunks -nosym` run (see M-plan disassemble feature) assembling:
//   moveq #5,d0 / move.l d0,d1 / move.w #$1234,d2 / add.l d1,d0 /
//   sub.w #4,d0 / cmp.l d0,d1 / lea $1000,a0 / jsr (a0) / bra.s skip /
//   nop / skip: rts
// -- real toolchain output, not hand-crafted. The surrounding HUNK_UNIT
// framing is added here to keep the test hermetic (no vasm dependency).
class TstHunkReader : public QObject {
    Q_OBJECT
private slots:
    void parsesRealObjectFile();

private:
    QTemporaryDir m_dir;
};

void TstHunkReader::parsesRealObjectFile()
{
    // Build the real byte stream precisely (easier to get right assembled
    // piece-by-piece here than as one giant hex literal above).
    QByteArray data;
    auto u32 = [&data](quint32 v) {
        data.append(char((v >> 24) & 0xFF));
        data.append(char((v >> 16) & 0xFF));
        data.append(char((v >> 8) & 0xFF));
        data.append(char(v & 0xFF));
    };

    u32(0x3E7); // HUNK_UNIT
    u32(3); data.append("alltest.s", 9); data.append(3, char(0)); // pad to 12 bytes (3 longwords)
    u32(0x3E8); // HUNK_NAME
    u32(1); data.append("CODE", 4);
    u32(0x3E9); // HUNK_CODE
    u32(7); // 7 longwords = 28 bytes
    const QByteArray code = QByteArray::fromHex(
        "7005" "2200" "343c1234" "d081" "5940" "b280" "41f81000" "4e90" "6002" "4e71" "4e75" "4e71");
    QCOMPARE(code.size(), 28);
    data.append(code);
    u32(0x3F2); // HUNK_END

    const QString path = m_dir.path() + QStringLiteral("/alltest.o");
    QFile file(path);
    QVERIFY(file.open(QFile::WriteOnly));
    file.write(data);
    file.close();

    HunkReader reader;
    QVERIFY2(reader.load(path), qPrintable(reader.errorString()));
    QCOMPARE(reader.sections().size(), 1);
    QCOMPARE(int(reader.sections().first().kind), int(HunkSection::Kind::Code));
    QCOMPARE(reader.sections().first().data, code);
}

QTEST_APPLESS_MAIN(TstHunkReader)
#include "tst_hunkreader.moc"
