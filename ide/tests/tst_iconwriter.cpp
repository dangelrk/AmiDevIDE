#include "build/IconWriter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// Verifies the generated .info DiskObject structure against the byte layout
// verified from a real icon (sdk/MUI_3.8/.info, see IconWriter.h comment):
// 78-byte fixed header, do_StackSize as a big-endian LONG at offset 74,
// followed by one Image struct (20 bytes) + its bitplane data.
class TstIconWriter : public QObject {
    Q_OBJECT
private slots:
    void generatesValidDiskObject();
    void patchesStackSizeInPlace();

private:
    QTemporaryDir m_dir;
};

void TstIconWriter::generatesValidDiskObject()
{
    const QString path = m_dir.path() + QStringLiteral("/test.info");
    QVERIFY(IconWriter::writeDefaultIcon(path, 16384));

    QFile file(path);
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray data = file.readAll();

    QVERIFY(data.size() > 78 + 20);

    const quint16 magic = (quint8(data[0]) << 8) | quint8(data[1]);
    QCOMPARE(magic, quint16(0xE310));

    const quint16 version = (quint8(data[2]) << 8) | quint8(data[3]);
    QCOMPARE(version, quint16(1));

    QCOMPARE(quint8(data[48]), quint8(3)); // do_Type == WBTOOL

    const quint32 stackSize = (quint8(data[74]) << 24) | (quint8(data[75]) << 16)
                             | (quint8(data[76]) << 8) | quint8(data[77]);
    QCOMPARE(stackSize, quint32(16384));

    // Image struct starts right at offset 78: Width/Height should match the
    // 24x24 glyph IconWriter generates.
    const quint16 imgWidth = (quint8(data[82]) << 8) | quint8(data[83]);
    const quint16 imgHeight = (quint8(data[84]) << 8) | quint8(data[85]);
    QCOMPARE(imgWidth, quint16(24));
    QCOMPARE(imgHeight, quint16(24));

    const quint16 imgDepth = (quint8(data[86]) << 8) | quint8(data[87]);
    QCOMPARE(imgDepth, quint16(2));

    // 78 (header) + 20 (Image struct) + rasterBytesPerRow(24)=4 * 24 rows * 2 planes = 192
    QCOMPARE(data.size(), 78 + 20 + 192);
}

void TstIconWriter::patchesStackSizeInPlace()
{
    const QString src = m_dir.path() + QStringLiteral("/src.info");
    const QString dst = m_dir.path() + QStringLiteral("/dst.info");
    QVERIFY(IconWriter::writeDefaultIcon(src, 8192));

    QVERIFY(IconWriter::copyIcon(src, dst, true, 65536));

    QFile file(dst);
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray data = file.readAll();
    const quint32 stackSize = (quint8(data[74]) << 24) | (quint8(data[75]) << 16)
                             | (quint8(data[76]) << 8) | quint8(data[77]);
    QCOMPARE(stackSize, quint32(65536));

    // Everything else (e.g. the image data) must be untouched.
    QFile srcFile(src);
    QVERIFY(srcFile.open(QFile::ReadOnly));
    const QByteArray srcData = srcFile.readAll();
    QCOMPARE(data.size(), srcData.size());
    QCOMPARE(data.left(74), srcData.left(74));
    QCOMPARE(data.mid(78), srcData.mid(78));
}

QTEST_APPLESS_MAIN(TstIconWriter)
#include "tst_iconwriter.moc"
