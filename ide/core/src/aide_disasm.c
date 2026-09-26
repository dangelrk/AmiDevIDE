#include "aide/aide_disasm.h"
#include "aide/aide_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned short read_word(const unsigned char *d, size_t dataSize, size_t pos)
{
    if (pos + 2 > dataSize)
        return 0;
    return (unsigned short)((d[pos] << 8) | d[pos + 1]);
}

static unsigned long read_long(const unsigned char *d, size_t dataSize, size_t pos)
{
    if (pos + 4 > dataSize)
        return 0;
    return ((unsigned long)d[pos] << 24) | ((unsigned long)d[pos + 1] << 16)
         | ((unsigned long)d[pos + 2] << 8) | (unsigned long)d[pos + 3];
}

static void append_hex(AideStr *s, unsigned long v, int width)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lx", v);
    int pad = width - len;
    aide_str_append_char(s, '$');
    while (pad-- > 0)
        aide_str_append_char(s, '0');
    aide_str_append(s, buf);
}

static char *hex_str(unsigned long v, int width)
{
    AideStr s;
    aide_str_init(&s);
    append_hex(&s, v, width);
    return aide_str_take(&s);
}

static char size_char(int size) /* 0=byte,1=word,2=long */
{
    return size == 0 ? 'b' : size == 1 ? 'w' : 'l';
}

static const char *const kConditions[16] = {
    "t", "f", "hi", "ls", "cc", "cs", "ne", "eq",
    "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le",
};

/* Decodes one effective-address field (mode/reg), consuming extension
 * words from *pos (mutated). `size` (0=byte,1=word,2=long) sizes
 * immediate/abs operands. Returns a malloc'd string, or NULL on an
 * unrecognized combination (mirrors the empty-QString-means-failure
 * convention of the original). */
static char *decode_ea(const unsigned char *data, size_t dataSize, size_t *pos, int mode, int reg, int size)
{
    AideStr s;
    aide_str_init(&s);

    switch (mode) {
    case 0: aide_str_appendf(&s, "d%d", reg); return aide_str_take(&s);
    case 1: aide_str_appendf(&s, "a%d", reg); return aide_str_take(&s);
    case 2: aide_str_appendf(&s, "(a%d)", reg); return aide_str_take(&s);
    case 3: aide_str_appendf(&s, "(a%d)+", reg); return aide_str_take(&s);
    case 4: aide_str_appendf(&s, "-(a%d)", reg); return aide_str_take(&s);
    case 5: {
        short disp = (short)read_word(data, dataSize, *pos);
        *pos += 2;
        aide_str_appendf(&s, "%d(a%d)", disp, reg);
        return aide_str_take(&s);
    }
    case 6: {
        unsigned short ext = read_word(data, dataSize, *pos);
        int isA, xn, isLong;
        signed char disp8;
        *pos += 2;
        isA = (ext >> 15) & 1;
        xn = (ext >> 12) & 7;
        isLong = (ext >> 11) & 1;
        disp8 = (signed char)(ext & 0xFF);
        aide_str_appendf(&s, "%d(a%d,%s%d.%s)", disp8, reg, isA ? "a" : "d", xn, isLong ? "l" : "w");
        return aide_str_take(&s);
    }
    case 7:
        switch (reg) {
        case 0: {
            unsigned short v = read_word(data, dataSize, *pos);
            *pos += 2;
            append_hex(&s, v, 4);
            aide_str_append(&s, ".w");
            return aide_str_take(&s);
        }
        case 1: {
            unsigned long v = read_long(data, dataSize, *pos);
            *pos += 4;
            append_hex(&s, v, 8);
            return aide_str_take(&s);
        }
        case 2: {
            short disp = (short)read_word(data, dataSize, *pos);
            *pos += 2;
            aide_str_appendf(&s, "%d(pc)", disp);
            return aide_str_take(&s);
        }
        case 3: {
            unsigned short ext = read_word(data, dataSize, *pos);
            int isA, xn, isLong;
            signed char disp8;
            *pos += 2;
            isA = (ext >> 15) & 1;
            xn = (ext >> 12) & 7;
            isLong = (ext >> 11) & 1;
            disp8 = (signed char)(ext & 0xFF);
            aide_str_appendf(&s, "%d(pc,%s%d.%s)", disp8, isA ? "a" : "d", xn, isLong ? "l" : "w");
            return aide_str_take(&s);
        }
        case 4:
            if (size == 0) {
                unsigned short v = read_word(data, dataSize, *pos) & 0xFF;
                *pos += 2;
                aide_str_append_char(&s, '#');
                append_hex(&s, v, 2);
                return aide_str_take(&s);
            } else if (size == 1) {
                unsigned short v = read_word(data, dataSize, *pos);
                *pos += 2;
                aide_str_append_char(&s, '#');
                append_hex(&s, v, 4);
                return aide_str_take(&s);
            } else {
                unsigned long v = read_long(data, dataSize, *pos);
                *pos += 4;
                aide_str_append_char(&s, '#');
                append_hex(&s, v, 8);
                return aide_str_take(&s);
            }
        default:
            aide_str_free(&s);
            return NULL;
        }
    default:
        aide_str_free(&s);
        return NULL;
    }
}

static const char *reg_name(int i, char *buf)
{
    if (i < 8)
        sprintf(buf, "d%d", i);
    else
        sprintf(buf, "a%d", i - 8);
    return buf;
}

static char *movem_reg_list(unsigned short mask, int reversed)
{
    /* Normal order: bit0=D0..bit7=D7,bit8=A0..bit15=A7. Predecrement mode
     * (reg->mem, destination -(An)) stores the mask reversed: bit0=A7..bit15=D0. */
    AideStr s;
    int i;
    int first = 1;
    char buf1[8], buf2[8];

    aide_str_init(&s);
    for (i = 0; i < 16;) {
        int bit = reversed ? (15 - i) : i;
        int j;
        if (!((mask >> bit) & 1)) {
            ++i;
            continue;
        }
        j = i;
        while (j + 1 < 16) {
            int nextBit = reversed ? (15 - (j + 1)) : (j + 1);
            if (!((mask >> nextBit) & 1))
                break;
            ++j;
        }
        if (!first)
            aide_str_append_char(&s, '/');
        first = 0;
        if (j > i) {
            aide_str_append(&s, reg_name(i, buf1));
            aide_str_append_char(&s, '-');
            aide_str_append(&s, reg_name(j, buf2));
        } else {
            aide_str_append(&s, reg_name(i, buf1));
        }
        i = j + 1;
    }
    return aide_str_take(&s);
}

static void set_unknown(AideDisasmInstruction *ins, const unsigned char *data, size_t dataSize,
                         size_t opcodePos, unsigned long addr)
{
    AideStr s;
    unsigned short w = read_word(data, dataSize, opcodePos);
    ins->address = addr;
    ins->length = 2;
    aide_str_init(&s);
    aide_str_append(&s, "dc.w    ");
    append_hex(&s, w, 4);
    ins->text = aide_str_take(&s);
    ins->rawHex = aide_strdup("");
}

static char *hex_dump(const unsigned char *data, size_t start, int length)
{
    static const char digits[] = "0123456789abcdef";
    char *out = (char *)malloc((size_t)length * 2 + 1);
    int i;
    for (i = 0; i < length; ++i) {
        unsigned char b = data[start + (size_t)i];
        out[i * 2] = digits[(b >> 4) & 0xF];
        out[i * 2 + 1] = digits[b & 0xF];
    }
    out[length * 2] = '\0';
    return out;
}

void aide_disasm_instruction_free(AideDisasmInstruction *ins)
{
    free(ins->text);
    free(ins->rawHex);
    ins->text = NULL;
    ins->rawHex = NULL;
}

void aide_disasm_decode_one(const unsigned char *data, size_t dataSize, int *offset,
                             unsigned long baseAddress, AideDisasmInstruction *out)
{
    const size_t opcodePos = (size_t)*offset;
    const unsigned long addr = (baseAddress + (unsigned long)opcodePos) & 0xFFFFFFFFUL;
    const unsigned short w = read_word(data, dataSize, opcodePos);
    size_t pos = opcodePos + 2; /* extension-word cursor */

    int matched = 0;
    AideStr mnem, ops;
    const int line = (w >> 12) & 0xF;

    out->address = addr;
    out->length = 2;
    out->text = NULL;
    out->rawHex = NULL;

    aide_str_init(&mnem);
    aide_str_init(&ops);

    if (line == 1 || line == 2 || line == 3) {
        /* MOVE.B / MOVE.L / MOVE.W (line encodes size directly) */
        const int size = line == 1 ? 0 : line == 2 ? 2 : 1;
        const int destReg = (w >> 9) & 7;
        const int destMode = (w >> 6) & 7;
        const int srcMode = (w >> 3) & 7;
        const int srcReg = w & 7;
        char *src = decode_ea(data, dataSize, &pos, srcMode, srcReg, size);
        char *dst = decode_ea(data, dataSize, &pos, destMode, destReg, size);
        if (src && dst && src[0] && dst[0]) {
            aide_str_append(&mnem, destMode == 1 ? "movea." : "move.");
            aide_str_append_char(&mnem, size_char(size));
            aide_str_append(&ops, src);
            aide_str_append_char(&ops, ',');
            aide_str_append(&ops, dst);
            matched = 1;
        }
        free(src);
        free(dst);
    } else if (line == 0) {
        const int op = (w >> 9) & 7;
        const int sizeBits = (w >> 6) & 3;
        const int mode = (w >> 3) & 7;
        const int reg = w & 7;

        if (w == 0x003C) { aide_str_append(&mnem, "ori.b"); aide_str_append(&ops, "#imm,ccr"); matched = 1; pos += 2; }
        else if (w == 0x007C) { aide_str_append(&mnem, "ori.w"); aide_str_append(&ops, "#imm,sr"); matched = 1; pos += 2; }
        else if (w == 0x023C) { aide_str_append(&mnem, "andi.b"); aide_str_append(&ops, "#imm,ccr"); matched = 1; pos += 2; }
        else if (w == 0x027C) { aide_str_append(&mnem, "andi.w"); aide_str_append(&ops, "#imm,sr"); matched = 1; pos += 2; }
        else if (w == 0x0A3C) { aide_str_append(&mnem, "eori.b"); aide_str_append(&ops, "#imm,ccr"); matched = 1; pos += 2; }
        else if (w == 0x0A7C) { aide_str_append(&mnem, "eori.w"); aide_str_append(&ops, "#imm,sr"); matched = 1; pos += 2; }
        else if (sizeBits != 3 && (op == 0 || op == 1 || op == 2 || op == 3 || op == 5 || op == 6)) {
            /* Immediate ops: ORI/ANDI/SUBI/ADDI/EORI/CMPI  #imm,<ea> */
            static const char *const names[8] = { "ori", "andi", "subi", "addi", NULL, "eori", "cmpi", NULL };
            const int size = sizeBits;
            char *imm = decode_ea(data, dataSize, &pos, 7, 4, size); /* immediate encoding reuses EA mode7/reg4 */
            char *dst = decode_ea(data, dataSize, &pos, mode, reg, size);
            if (names[op] && imm && dst && imm[0] && dst[0]) {
                aide_str_append(&mnem, names[op]);
                aide_str_append_char(&mnem, '.');
                aide_str_append_char(&mnem, size_char(size));
                aide_str_append(&ops, imm);
                aide_str_append_char(&ops, ',');
                aide_str_append(&ops, dst);
                matched = 1;
            }
            free(imm);
            free(dst);
        } else if ((w & 0xF900) == 0x0800) {
            /* Static bit ops: BTST/BCHG/BCLR/BSET #bitnum,<ea> */
            static const char *const names[4] = { "btst", "bchg", "bclr", "bset" };
            const int bop = (w >> 6) & 3;
            const unsigned short bitnum = read_word(data, dataSize, pos); pos += 2;
            char *dst = decode_ea(data, dataSize, &pos, mode, reg, 0);
            if (dst && dst[0]) {
                aide_str_append(&mnem, names[bop]);
                aide_str_appendf(&ops, "#%d,%s", bitnum & 0xFF, dst);
                matched = 1;
            }
            free(dst);
        } else if ((w & 0xF100) == 0x0100 && ((w >> 6) & 3) != 1) {
            /* Dynamic bit ops: BTST/BCHG/BCLR/BSET Dn,<ea> (bit8=1 already
             * implied by 0xF100 mask catching bits 15-12=0000, bit8=1) */
            static const char *const names[4] = { "btst", "bchg", "bclr", "bset" };
            const int bop = (w >> 6) & 3;
            const int dreg = (w >> 9) & 7;
            char *dst = decode_ea(data, dataSize, &pos, mode, reg, 0);
            if (dst && dst[0]) {
                aide_str_append(&mnem, names[bop]);
                aide_str_appendf(&ops, "d%d,%s", dreg, dst);
                matched = 1;
            }
            free(dst);
        }
    } else if (line == 4) {
        const int mode = (w >> 3) & 7;
        const int reg = w & 7;

        if (w == 0x4E71) { aide_str_append(&mnem, "nop"); matched = 1; }
        else if (w == 0x4E75) { aide_str_append(&mnem, "rts"); matched = 1; }
        else if (w == 0x4E73) { aide_str_append(&mnem, "rte"); matched = 1; }
        else if (w == 0x4E77) { aide_str_append(&mnem, "rtr"); matched = 1; }
        else if (w == 0x4E70) { aide_str_append(&mnem, "reset"); matched = 1; }
        else if (w == 0x4AFC) { aide_str_append(&mnem, "illegal"); matched = 1; }
        else if (w == 0x4E72) { aide_str_append(&mnem, "stop"); aide_str_append_char(&ops, '#'); append_hex(&ops, read_word(data, dataSize, pos), 4); pos += 2; matched = 1; }
        else if ((w & 0xFFF0) == 0x4E40) { aide_str_append(&mnem, "trap"); aide_str_appendf(&ops, "#%d", w & 0xF); matched = 1; }
        else if ((w & 0xFFF8) == 0x4E50) {
            short disp = (short)read_word(data, dataSize, pos); pos += 2;
            aide_str_append(&mnem, "link");
            aide_str_appendf(&ops, "a%d,#%d", w & 7, disp);
            matched = 1;
        } else if ((w & 0xFFF8) == 0x4E58) { aide_str_append(&mnem, "unlk"); aide_str_appendf(&ops, "a%d", w & 7); matched = 1; }
        else if ((w & 0xFFF8) == 0x4E60) { aide_str_append(&mnem, "move.l"); aide_str_appendf(&ops, "a%d,usp", w & 7); matched = 1; }
        else if ((w & 0xFFF8) == 0x4E68) { aide_str_append(&mnem, "move.l"); aide_str_appendf(&ops, "usp,a%d", w & 7); matched = 1; }
        else if ((w & 0xFFC0) == 0x4E80) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 2); if (ea && ea[0]) { aide_str_append(&mnem, "jsr"); aide_str_append(&ops, ea); matched = 1; } free(ea); }
        else if ((w & 0xFFC0) == 0x4EC0) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 2); if (ea && ea[0]) { aide_str_append(&mnem, "jmp"); aide_str_append(&ops, ea); matched = 1; } free(ea); }
        else if ((w & 0xF1C0) == 0x41C0) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 2); if (ea && ea[0]) { aide_str_append(&mnem, "lea"); aide_str_appendf(&ops, "%s,a%d", ea, (w >> 9) & 7); matched = 1; } free(ea); }
        else if ((w & 0xF1C0) == 0x4180) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 1); if (ea && ea[0]) { aide_str_append(&mnem, "chk.w"); aide_str_appendf(&ops, "%s,d%d", ea, (w >> 9) & 7); matched = 1; } free(ea); }
        else if ((w & 0xFFC0) == 0x4840) {
            if (mode == 0) { aide_str_append(&mnem, "swap"); aide_str_appendf(&ops, "d%d", reg); matched = 1; }
            else { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 2); if (ea && ea[0]) { aide_str_append(&mnem, "pea"); aide_str_append(&ops, ea); matched = 1; } free(ea); }
        } else if ((w & 0xFFF8) == 0x4880) { aide_str_append(&mnem, "ext.w"); aide_str_appendf(&ops, "d%d", reg); matched = 1; }
        else if ((w & 0xFFF8) == 0x48C0) { aide_str_append(&mnem, "ext.l"); aide_str_appendf(&ops, "d%d", reg); matched = 1; }
        else if ((w & 0xFF80) == 0x4880) {
            const int isLong = (w >> 6) & 1;
            const unsigned short mask = read_word(data, dataSize, pos); pos += 2;
            char *ea = decode_ea(data, dataSize, &pos, mode, reg, 2);
            if (ea && ea[0]) {
                char *list = movem_reg_list(mask, mode == 4);
                aide_str_append(&mnem, "movem.");
                aide_str_append_char(&mnem, size_char(isLong ? 2 : 1));
                aide_str_append(&ops, list);
                aide_str_append_char(&ops, ',');
                aide_str_append(&ops, ea);
                matched = 1;
                free(list);
            }
            free(ea);
        } else if ((w & 0xFF80) == 0x4C80) {
            const int isLong = (w >> 6) & 1;
            const unsigned short mask = read_word(data, dataSize, pos); pos += 2;
            char *ea = decode_ea(data, dataSize, &pos, mode, reg, 2);
            if (ea && ea[0]) {
                char *list = movem_reg_list(mask, 0);
                aide_str_append(&mnem, "movem.");
                aide_str_append_char(&mnem, size_char(isLong ? 2 : 1));
                aide_str_append(&ops, ea);
                aide_str_append_char(&ops, ',');
                aide_str_append(&ops, list);
                matched = 1;
                free(list);
            }
            free(ea);
        } else if ((w & 0xFF00) == 0x4000) { const int size = (w >> 6) & 3; if (size != 3) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, size); if (ea && ea[0]) { aide_str_append(&mnem, "negx."); aide_str_append_char(&mnem, size_char(size)); aide_str_append(&ops, ea); matched = 1; } free(ea); } }
        else if ((w & 0xFF00) == 0x4200) { const int size = (w >> 6) & 3; if (size != 3) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, size); if (ea && ea[0]) { aide_str_append(&mnem, "clr."); aide_str_append_char(&mnem, size_char(size)); aide_str_append(&ops, ea); matched = 1; } free(ea); } }
        else if ((w & 0xFF00) == 0x4400) { const int size = (w >> 6) & 3; if (size != 3) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, size); if (ea && ea[0]) { aide_str_append(&mnem, "neg."); aide_str_append_char(&mnem, size_char(size)); aide_str_append(&ops, ea); matched = 1; } free(ea); } }
        else if ((w & 0xFF00) == 0x4600) { const int size = (w >> 6) & 3; if (size != 3) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, size); if (ea && ea[0]) { aide_str_append(&mnem, "not."); aide_str_append_char(&mnem, size_char(size)); aide_str_append(&ops, ea); matched = 1; } free(ea); } }
        else if ((w & 0xFFC0) == 0x4800) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 0); if (ea && ea[0]) { aide_str_append(&mnem, "nbcd"); aide_str_append(&ops, ea); matched = 1; } free(ea); }
        else if ((w & 0xFF00) == 0x4A00) {
            const int size = (w >> 6) & 3;
            if (size == 3) {
                if ((w & 0x3F) == 0x3C) { aide_str_append(&mnem, "illegal"); matched = 1; }
                else { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 0); if (ea && ea[0]) { aide_str_append(&mnem, "tas"); aide_str_append(&ops, ea); matched = 1; } free(ea); }
            } else {
                char *ea = decode_ea(data, dataSize, &pos, mode, reg, size);
                if (ea && ea[0]) { aide_str_append(&mnem, "tst."); aide_str_append_char(&mnem, size_char(size)); aide_str_append(&ops, ea); matched = 1; }
                free(ea);
            }
        } else if ((w & 0xFFC0) == 0x40C0) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 1); if (ea && ea[0]) { aide_str_append(&mnem, "move.w"); aide_str_append(&ops, "sr,"); aide_str_append(&ops, ea); matched = 1; } free(ea); }
        else if ((w & 0xFFC0) == 0x44C0) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 1); if (ea && ea[0]) { aide_str_append(&mnem, "move.w"); aide_str_append(&ops, ea); aide_str_append(&ops, ",ccr"); matched = 1; } free(ea); }
        else if ((w & 0xFFC0) == 0x46C0) { char *ea = decode_ea(data, dataSize, &pos, mode, reg, 1); if (ea && ea[0]) { aide_str_append(&mnem, "move.w"); aide_str_append(&ops, ea); aide_str_append(&ops, ",sr"); matched = 1; } free(ea); }
    } else if (line == 5) {
        const int mode = (w >> 3) & 7;
        const int reg = w & 7;
        const int cond = (w >> 8) & 0xF;
        const int data3 = (w >> 9) & 7;
        const int sizeBits = (w >> 6) & 3;

        if (sizeBits == 3 && mode == 1) {
            short disp = (short)read_word(data, dataSize, pos); pos += 2;
            aide_str_appendf(&mnem, "db%s", kConditions[cond]);
            aide_str_appendf(&ops, "d%d,", reg);
            /* quint32-width modular arithmetic (address is a 32-bit
             * register on real hardware) -- must mask explicitly since
             * `unsigned long` is 64-bit on the host build. */
            append_hex(&ops, (addr + 2 + (unsigned long)(long)disp) & 0xFFFFFFFFUL, 8);
            matched = 1;
        } else if (sizeBits == 3) {
            char *ea = decode_ea(data, dataSize, &pos, mode, reg, 0);
            if (ea && ea[0]) { aide_str_appendf(&mnem, "s%s", kConditions[cond]); aide_str_append(&ops, ea); matched = 1; }
            free(ea);
        } else {
            const int size = sizeBits;
            char *ea = decode_ea(data, dataSize, &pos, mode, reg, size);
            const int quick = data3 == 0 ? 8 : data3;
            if (ea && ea[0]) {
                aide_str_append(&mnem, (w >> 8) & 1 ? "subq" : "addq");
                aide_str_append_char(&mnem, '.');
                aide_str_append_char(&mnem, size_char(size));
                aide_str_appendf(&ops, "#%d,%s", quick, ea);
                matched = 1;
            }
            free(ea);
        }
    } else if (line == 6) {
        const int cond = (w >> 8) & 0xF;
        long disp = (signed char)(w & 0xFF);
        int extra = 0;
        unsigned long target;
        if (disp == 0) { disp = (short)read_word(data, dataSize, pos); extra = 2; }
        /* quint32-width modular arithmetic -- see the db<cc> branch above. */
        target = (addr + 2 + (unsigned long)disp) & 0xFFFFFFFFUL;
        pos += (size_t)extra;
        if (cond == 0) aide_str_append(&mnem, "bra");
        else if (cond == 1) aide_str_append(&mnem, "bsr");
        else aide_str_appendf(&mnem, "b%s", kConditions[cond]);
        aide_str_append(&mnem, extra ? ".w" : ".s");
        append_hex(&ops, target, 8);
        matched = 1;
    } else if (line == 7) {
        if (!((w >> 8) & 1)) {
            const int reg = (w >> 9) & 7;
            const signed char data8 = (signed char)(w & 0xFF);
            aide_str_append(&mnem, "moveq.l");
            aide_str_appendf(&ops, "#%d,d%d", data8, reg);
            matched = 1;
        }
    } else if (line == 8 || line == 9 || line == 0xB || line == 0xC || line == 0xD) {
        /* OR/SUB/CMP/AND/ADD families, plus their -A/-I/-X/-M variants, MULU/MULS/DIVU/DIVS/ABCD/SBCD/EXG. */
        const int reg = (w >> 9) & 7;
        const int opmode = (w >> 6) & 7;
        const int mode = (w >> 3) & 7;
        const int rm = w & 7;

        /* Line 0xB is asymmetric: opmode 0-2 is CMP (ea,Dn) but opmode 4-6 is
         * EOR (Dn,ea) -- a genuinely different mnemonic, not "CMP reversed"
         * (verified: real vasm-assembled "eor.l d0,d1" decodes to 0xB181,
         * sharing CMP's line/opmode-family bit layout). All other lines use
         * the same mnemonic in both directions. */
        static const char *const base[5] = { "or", "sub", "cmp", "and", "add" };
        const int famIdx = line == 8 ? 0 : line == 9 ? 1 : line == 0xB ? 2 : line == 0xC ? 3 : 4;

        if (line == 0xC && opmode == 3) { /* MULU */
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, 1);
            if (ea && ea[0]) { aide_str_append(&mnem, "mulu.w"); aide_str_appendf(&ops, "%s,d%d", ea, reg); matched = 1; }
            free(ea);
        } else if (line == 0xC && opmode == 7) { /* MULS */
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, 1);
            if (ea && ea[0]) { aide_str_append(&mnem, "muls.w"); aide_str_appendf(&ops, "%s,d%d", ea, reg); matched = 1; }
            free(ea);
        } else if (line == 8 && opmode == 3) {
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, 1);
            if (ea && ea[0]) { aide_str_append(&mnem, "divu.w"); aide_str_appendf(&ops, "%s,d%d", ea, reg); matched = 1; }
            free(ea);
        } else if (line == 8 && opmode == 7) {
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, 1);
            if (ea && ea[0]) { aide_str_append(&mnem, "divs.w"); aide_str_appendf(&ops, "%s,d%d", ea, reg); matched = 1; }
            free(ea);
        } else if (line == 8 && opmode == 4 && mode == 0) { aide_str_append(&mnem, "sbcd"); aide_str_appendf(&ops, "d%d,d%d", rm, reg); matched = 1; }
        else if (line == 8 && opmode == 4 && mode == 1) { aide_str_append(&mnem, "sbcd"); aide_str_appendf(&ops, "-(a%d),-(a%d)", rm, reg); matched = 1; }
        else if (line == 0xC && opmode == 4 && mode == 0) { aide_str_append(&mnem, "abcd"); aide_str_appendf(&ops, "d%d,d%d", rm, reg); matched = 1; }
        else if (line == 0xC && opmode == 4 && mode == 1) { aide_str_append(&mnem, "abcd"); aide_str_appendf(&ops, "-(a%d),-(a%d)", rm, reg); matched = 1; }
        else if (line == 0xC && (opmode == 5 || opmode == 6) && mode == 0) { aide_str_append(&mnem, "exg"); aide_str_appendf(&ops, "d%d,d%d", reg, rm); matched = 1; }
        else if (line == 0xC && opmode == 5 && mode == 1) { aide_str_append(&mnem, "exg"); aide_str_appendf(&ops, "a%d,a%d", reg, rm); matched = 1; }
        else if (line == 0xC && opmode == 6 && mode == 1) { aide_str_append(&mnem, "exg"); aide_str_appendf(&ops, "d%d,a%d", reg, rm); matched = 1; }
        else if (line == 0xB && opmode == 3) { char *ea = decode_ea(data, dataSize, &pos, mode, rm, 1); if (ea && ea[0]) { aide_str_append(&mnem, "cmpa.w"); aide_str_appendf(&ops, "%s,a%d", ea, reg); matched = 1; } free(ea); }
        else if (line == 0xB && opmode == 7) { char *ea = decode_ea(data, dataSize, &pos, mode, rm, 2); if (ea && ea[0]) { aide_str_append(&mnem, "cmpa.l"); aide_str_appendf(&ops, "%s,a%d", ea, reg); matched = 1; } free(ea); }
        else if ((line == 9 || line == 0xD) && (opmode == 3 || opmode == 7)) {
            const int size = opmode == 3 ? 1 : 2;
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, size);
            if (ea && ea[0]) {
                aide_str_append(&mnem, line == 9 ? "suba." : "adda.");
                aide_str_append_char(&mnem, size_char(size));
                aide_str_appendf(&ops, "%s,a%d", ea, reg);
                matched = 1;
            }
            free(ea);
        } else if (line == 0xB && opmode >= 4 && opmode <= 6 && mode == 1) {
            const int size = opmode - 4;
            aide_str_appendf(&ops, "(a%d)+,(a%d)+", rm, reg);
            aide_str_append(&mnem, "cmpm.");
            aide_str_append_char(&mnem, size_char(size));
            matched = 1;
        } else if ((line == 9 || line == 0xD) && opmode >= 4 && mode <= 1 && ((opmode - 4) <= 2)) {
            const int size = opmode - 4;
            aide_str_append(&mnem, line == 9 ? "subx." : "addx.");
            aide_str_append_char(&mnem, size_char(size));
            if (mode == 0)
                aide_str_appendf(&ops, "d%d,d%d", rm, reg);
            else
                aide_str_appendf(&ops, "-(a%d),-(a%d)", rm, reg);
            matched = 1;
        } else if (opmode <= 2 && base[famIdx]) {
            const int size = opmode;
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, size);
            if (ea && ea[0]) {
                aide_str_append(&mnem, base[famIdx]);
                aide_str_append_char(&mnem, '.');
                aide_str_append_char(&mnem, size_char(size));
                aide_str_appendf(&ops, "%s,d%d", ea, reg);
                matched = 1;
            }
            free(ea);
        } else if (opmode >= 4 && opmode <= 6 && base[famIdx]) {
            const int size = opmode - 4;
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, size);
            const char *reverseName = line == 0xB ? "eor" : base[famIdx];
            if (ea && ea[0]) {
                aide_str_append(&mnem, reverseName);
                aide_str_append_char(&mnem, '.');
                aide_str_append_char(&mnem, size_char(size));
                aide_str_appendf(&ops, "d%d,", reg);
                aide_str_append(&ops, ea);
                matched = 1;
            }
            free(ea);
        }
    } else if (line == 0xE) {
        const int mode = (w >> 3) & 7;
        const int rm = w & 7;
        if (((w >> 6) & 3) == 3) {
            /* Memory shift/rotate (word only, shift by 1, EA destination). */
            static const char *const memOps[8] = { "asr", "asl", "lsr", "lsl", "roxr", "roxl", "ror", "rol" };
            const int op = (w >> 9) & 7;
            char *ea = decode_ea(data, dataSize, &pos, mode, rm, 1);
            if (ea && ea[0]) {
                aide_str_append(&mnem, memOps[op]);
                aide_str_append(&mnem, ".w");
                aide_str_append(&ops, ea);
                matched = 1;
            }
            free(ea);
        } else {
            const int size = (w >> 6) & 3;
            const int dir = (w >> 8) & 1; /* 0=right,1=left */
            const int type = (w >> 3) & 3; /* 0=asx,1=lsx,2=roxx,3=rox */
            const int countOrReg = (w >> 9) & 7;
            const int useReg = (w >> 5) & 1;
            static const char *const names[4][2] = {
                { "asr", "asl" }, { "lsr", "lsl" }, { "roxr", "roxl" }, { "ror", "rol" },
            };
            aide_str_append(&mnem, names[type][dir]);
            aide_str_append_char(&mnem, '.');
            aide_str_append_char(&mnem, size_char(size));
            if (useReg)
                aide_str_appendf(&ops, "d%d,d%d", countOrReg, rm);
            else
                aide_str_appendf(&ops, "#%d,d%d", countOrReg == 0 ? 8 : countOrReg, rm);
            matched = 1;
        }
    }

    if (!matched) {
        /* Always advance by at least one word, even for unrecognized
         * opcodes (68020+/FPU/reserved) -- a stuck offset here would hang
         * the caller's decode loop forever. */
        aide_str_free(&mnem);
        aide_str_free(&ops);
        *offset = (int)opcodePos + 2;
        set_unknown(out, data, dataSize, opcodePos, addr);
        return;
    }

    out->length = (int)(pos - opcodePos);
    {
        AideStr text;
        aide_str_init(&text);
        if (ops.len == 0) {
            aide_str_append(&text, mnem.data);
        } else {
            size_t mnemLen = mnem.len;
            aide_str_append(&text, mnem.data);
            while (mnemLen < 8) {
                aide_str_append_char(&text, ' ');
                ++mnemLen;
            }
            aide_str_append(&text, ops.data);
        }
        out->text = aide_str_take(&text);
    }
    out->rawHex = hex_dump(data, opcodePos, out->length);
    *offset = (int)pos;

    aide_str_free(&mnem);
    aide_str_free(&ops);
}
