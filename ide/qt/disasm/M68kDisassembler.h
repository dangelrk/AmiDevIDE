#pragma once

#include <QByteArray>
#include <QString>

// Minimal but broad MC68000 disassembler: covers the full 68000 base
// instruction set (data movement, arithmetic, logic, shift/rotate, bit
// ops, branches, jumps, misc). CPU>68000-specific and FPU/coprocessor
// opcodes are not decoded and fall back to a "dc.w $xxxx" placeholder
// rather than guessing -- acceptable for a light disassembler (plan's own
// tolerance for a bounded, honestly-scoped feature), since vbcc/vasm
// output for the common 68000 target is what this exists to inspect.
struct DisassembledInstruction {
    quint32 address = 0;
    int length = 2;      // bytes consumed
    QString text;         // e.g. "move.l  d0,d1"
    QString rawHex;       // e.g. "203c"
};

namespace M68kDisassembler {

// Decodes exactly one instruction starting at data[offset]. Always advances
// offset by at least 2 (even on unknown opcodes, treated as a raw data word).
DisassembledInstruction decodeOne(const QByteArray &data, int &offset, quint32 baseAddress);

}
