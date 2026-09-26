#include "HunkReader.h"

#include "aide/aide_hunk.h"

#include <QFile>

// Thin adapter over the portable core/ implementation (core/src/aide_hunk.c,
// shared with the Amiga/MUI build).
// Big-endian byte parsing lives entirely in aide_hunk.c now; this file only
// does the Qt-specific file I/O and converts the result to QByteArray/QString.

bool HunkReader::load(const QString &path)
{
    m_sections.clear();
    m_error.clear();

    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        m_error = QStringLiteral("Could not open file");
        return false;
    }

    const QByteArray data = file.readAll();

    AideHunkReader reader;
    aide_hunkreader_init(&reader);
    const bool ok = aide_hunkreader_load_bytes(
        &reader, reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size()));

    if (!ok) {
        m_error = QString::fromUtf8(reader.error);
        aide_hunkreader_free(&reader);
        return false;
    }

    for (size_t i = 0; i < reader.sectionCount; ++i) {
        const AideHunkSection &src = reader.sections[i];
        HunkSection section;
        section.kind = src.kind == AIDE_HUNK_CODE ? HunkSection::Kind::Code
                      : src.kind == AIDE_HUNK_DATA ? HunkSection::Kind::Data
                                                    : HunkSection::Kind::Bss;
        section.index = src.index;
        if (src.data)
            section.data = QByteArray(reinterpret_cast<const char *>(src.data), int(src.dataSize));
        section.sizeBytes = quint32(src.sizeBytes);
        for (size_t j = 0; j < src.symbolCount; ++j) {
            HunkSymbol sym;
            sym.name = QString::fromUtf8(src.symbols[j].name);
            sym.offset = quint32(src.symbols[j].offset);
            section.symbols << sym;
        }
        m_sections << section;
    }

    aide_hunkreader_free(&reader);
    return true;
}
