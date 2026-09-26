#include "M68kDisassembler.h"

#include "aide/aide_disasm.h"

// Thin adapter over the portable core/ implementation (core/src/aide_disasm.c,
// shared with the Amiga/MUI build).
// All decode logic lives in aide_disasm.c now; this file only converts the
// result to QString/quint32.

namespace M68kDisassembler {

DisassembledInstruction decodeOne(const QByteArray &data, int &offset, quint32 baseAddress)
{
    AideDisasmInstruction ins;
    aide_disasm_decode_one(reinterpret_cast<const unsigned char *>(data.constData()),
                            size_t(data.size()), &offset, baseAddress, &ins);

    DisassembledInstruction result;
    result.address = quint32(ins.address);
    result.length = ins.length;
    result.text = QString::fromUtf8(ins.text);
    result.rawHex = QString::fromUtf8(ins.rawHex);

    aide_disasm_instruction_free(&ins);
    return result;
}

} // namespace M68kDisassembler
