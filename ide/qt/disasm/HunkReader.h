#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// Parses AmigaOS Hunk-format executables (HUNK_HEADER-led) and simple object
// files (HUNK_UNIT-led), extracting CODE/DATA/BSS sections with their symbol
// tables. Verified against a real vlink -bamigahunk executable (see M-plan
// disassemble feature): HUNK_HEADER(0x3F3)/CODE(0x3E9)/DATA(0x3EA)/
// BSS(0x3EB)/RELOC32(0x3EC)/SYMBOL(0x3F0)/DEBUG(0x3F1)/END(0x3F2).
//
// Hunk types genuinely produced by vbcc/vasm/vlink are covered; anything
// else (overlay hunks, libraries, ...) is reported as a parse error rather
// than guessed at, since those aren't self-describing enough to skip safely.
struct HunkSymbol {
    QString name;
    quint32 offset = 0; // byte offset within the hunk
};

struct HunkSection {
    enum class Kind { Code, Data, Bss };
    Kind kind = Kind::Code;
    int index = 0;         // hunk number, for display ("hunk 0")
    QByteArray data;        // raw bytes; empty for Bss (size only)
    quint32 sizeBytes = 0;   // for Bss, the reserved size
    QList<HunkSymbol> symbols;
};

class HunkReader {
public:
    bool load(const QString &path);

    QString errorString() const { return m_error; }
    QList<HunkSection> sections() const { return m_sections; }

private:
    QList<HunkSection> m_sections;
    QString m_error;
};
