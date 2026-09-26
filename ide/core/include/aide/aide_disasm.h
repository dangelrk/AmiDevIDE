#ifndef AIDE_DISASM_H
#define AIDE_DISASM_H

/* Portable C99 mirror of disasm/M68kDisassembler.h -- minimal but broad
 * MC68000 disassembler covering the full base instruction set. See that
 * header for scope notes (CPU>68000/FPU opcodes fall back to a "dc.w"
 * placeholder rather than being guessed at). */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideDisasmInstruction {
    unsigned long address;
    int length;   /* bytes consumed */
    char *text;   /* malloc'd, e.g. "move.l  d0,d1" */
    char *rawHex; /* malloc'd, e.g. "203c" */
} AideDisasmInstruction;

void aide_disasm_instruction_free(AideDisasmInstruction *ins);

/* Decodes exactly one instruction starting at data[*offset] (dataSize is
 * the total buffer length, used for bounds-checked word/long reads).
 * Always advances *offset by at least 2, even on unknown opcodes
 * (treated as a raw data word) -- callers may loop unconditionally. */
void aide_disasm_decode_one(const unsigned char *data, size_t dataSize, int *offset,
                             unsigned long baseAddress, AideDisasmInstruction *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_DISASM_H */
