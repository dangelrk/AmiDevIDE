#include "AsmHighlighter.h"

#include <QStringList>
#include <cstddef>

namespace {
// Base m68k mnemonics (without size suffix); vasmm68k_mot accepts .b/.w/.l/.s/.q
// suffixes on most of these, so the regex generated below appends an optional
// "\.[bwlsq]" to each entry instead of hand-listing every sized variant.
const char *const kMnemonics[] = {
    "move", "movea", "movem", "movep", "moveq", "lea", "pea",
    "add", "adda", "addi", "addq", "addx",
    "sub", "suba", "subi", "subq", "subx",
    "mulu", "muls", "divu", "divs",
    "neg", "negx", "clr", "not", "ext", "extb", "swap",
    "and", "andi", "or", "ori", "eor", "eori",
    "cmp", "cmpa", "cmpi", "cmpm",
    "tst", "btst", "bset", "bclr", "bchg",
    "asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr",
    "bra", "bsr", "jmp", "jsr", "rts", "rte", "rtr",
    "beq", "bne", "bcc", "bcs", "bge", "bgt", "ble", "blt",
    "bhi", "bls", "bpl", "bmi", "bvc", "bvs",
    "dbra", "dbeq", "dbne", "dbcc", "dbcs", "dbf", "dbt",
    "nop", "trap", "trapv", "reset", "stop", "illegal",
    "link", "unlk", "exg",
    "seq", "sne", "scc", "scs", "sge", "sgt", "sle", "slt",
    "shi", "sls", "spl", "smi", "svc", "svs", "st", "sf",
    "abcd", "sbcd", "nbcd", "chk",
    "fadd", "fsub", "fmul", "fdiv", "fmove", "fcmp", "ftst",
    "fabs", "fneg", "fsqrt",
};

const char *const kRegisters[] = {
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "sp", "pc", "sr", "ccr",
    "fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7",
};

const char *const kDirectives[] = {
    "section", "dc", "ds", "dcb", "equ", "set", "rs", "rsreset", "rsset",
    "macro", "endm", "mexit", "rept", "endr",
    "if", "ifd", "ifnd", "ifeq", "ifne", "ifgt", "iflt", "else", "endc",
    "xdef", "xref", "public", "extern", "global",
    "incbin", "include", "incdir", "org", "even", "cnop", "align",
    "end", "list", "nolist", "opt", "module", "endmodule",
};
}

AsmHighlighter::AsmHighlighter(QTextDocument *parent, const EditorTheme &theme)
    : QSyntaxHighlighter(parent)
{
    auto alternation = [](const char *const *items, size_t count) {
        QStringList parts;
        parts.reserve(int(count));
        for (size_t i = 0; i < count; ++i)
            parts << QRegularExpression::escape(QString::fromLatin1(items[i]));
        return parts.join('|');
    };

    QTextCharFormat mnemonicFormat;
    mnemonicFormat.setForeground(theme.keyword);
    mnemonicFormat.setFontWeight(QFont::Bold);
    {
        Rule rule;
        const QString alt = alternation(kMnemonics, std::size(kMnemonics));
        rule.pattern = QRegularExpression(
            QStringLiteral("(?<![\\w.])(%1)(\\.[bwlsqBWLSQ])?\\b").arg(alt),
            QRegularExpression::CaseInsensitiveOption);
        rule.format = mnemonicFormat;
        m_rules.append(rule);
    }

    QTextCharFormat registerFormat;
    registerFormat.setForeground(theme.type);
    {
        Rule rule;
        const QString alt = alternation(kRegisters, std::size(kRegisters));
        rule.pattern = QRegularExpression(QStringLiteral("(?<![\\w.])(%1)\\b").arg(alt),
                                           QRegularExpression::CaseInsensitiveOption);
        rule.format = registerFormat;
        m_rules.append(rule);
    }

    QTextCharFormat directiveFormat;
    directiveFormat.setForeground(theme.preprocessor);
    {
        Rule rule;
        const QString alt = alternation(kDirectives, std::size(kDirectives));
        rule.pattern = QRegularExpression(QStringLiteral("(?<![\\w.])(%1)\\b").arg(alt),
                                           QRegularExpression::CaseInsensitiveOption);
        rule.format = directiveFormat;
        m_rules.append(rule);
    }

    // Column-0 global label, e.g. "MyLabel:" or "MyLabel" at line start.
    QTextCharFormat labelFormat;
    labelFormat.setForeground(theme.label);
    labelFormat.setFontWeight(QFont::Bold);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*:?"));
        rule.format = labelFormat;
        m_rules.append(rule);
    }

    // Local labels: ".x" or "x$" style.
    QTextCharFormat localLabelFormat;
    localLabelFormat.setForeground(theme.localLabel);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("(?:^|\\s)(\\.[A-Za-z0-9_]+|[A-Za-z0-9_]+\\$)"));
        rule.format = localLabelFormat;
        m_rules.append(rule);
    }

    QTextCharFormat numberFormat;
    numberFormat.setForeground(theme.number);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\$[0-9a-fA-F]+|%[01]+|#\\-?[0-9]+|\\b[0-9]+\\b"));
        rule.format = numberFormat;
        m_rules.append(rule);
    }

    QTextCharFormat stringFormat;
    stringFormat.setForeground(theme.string);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\".*?(?<!\\\\)\"|'.*?(?<!\\\\)'"));
        rule.format = stringFormat;
        m_rules.append(rule);
    }

    // Comments: ";..." anywhere, "*" only in column 0 (classic m68k asm rule).
    QTextCharFormat commentFormat;
    commentFormat.setForeground(theme.comment);
    commentFormat.setFontItalic(true);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral(";.*$"));
        rule.format = commentFormat;
        m_rules.append(rule);
    }
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^\\*.*$"));
        rule.format = commentFormat;
        m_rules.append(rule);
    }
}

void AsmHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
