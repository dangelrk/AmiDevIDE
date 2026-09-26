#include "CHighlighter.h"

namespace {
const char *const kKeywords[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "double",
    "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int",
    "long", "register", "restrict", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while", "_Bool", "_Complex", "_Imaginary",
};

const char *const kAmigaTypes[] = {
    "APTR", "BPTR", "BSTR", "UBYTE", "BYTE", "UWORD", "WORD", "ULONG", "LONG",
    "UQUAD", "QUAD", "STRPTR", "CONST_STRPTR", "TEXT", "BOOL", "VOID",
    "CPTR", "FLOAT", "DOUBLE", "RPTR", "Tag", "TagItem", "TagPtr",
    "size_t", "ssize_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
};
}

CHighlighter::CHighlighter(QTextDocument *parent, const EditorTheme &theme)
    : QSyntaxHighlighter(parent)
{
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(theme.keyword);
    keywordFormat.setFontWeight(QFont::Bold);
    for (const char *kw : kKeywords) {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(kw));
        rule.format = keywordFormat;
        m_rules.append(rule);
    }

    QTextCharFormat typeFormat;
    typeFormat.setForeground(theme.type);
    for (const char *ty : kAmigaTypes) {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(ty));
        rule.format = typeFormat;
        m_rules.append(rule);
    }

    QTextCharFormat numberFormat;
    numberFormat.setForeground(theme.number);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b0[xX][0-9a-fA-F]+\\b|\\b[0-9]+(\\.[0-9]+)?[uUlLfF]*\\b"));
        rule.format = numberFormat;
        m_rules.append(rule);
    }

    m_preprocessorFormat.setForeground(theme.preprocessor);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^\\s*#\\s*\\w+"));
        rule.format = m_preprocessorFormat;
        m_rules.append(rule);
    }

    QTextCharFormat functionFormat;
    functionFormat.setForeground(theme.function);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"));
        rule.format = functionFormat;
        m_rules.append(rule);
    }

    m_stringFormat.setForeground(theme.string);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("\".*?(?<!\\\\)\"|'.*?(?<!\\\\)'"));
        rule.format = m_stringFormat;
        m_rules.append(rule);
    }

    m_commentFormat.setForeground(theme.comment);
    m_commentFormat.setFontItalic(true);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("//[^\n]*"));
        rule.format = m_commentFormat;
        m_rules.append(rule);
    }

    m_commentStart = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEnd = QRegularExpression(QStringLiteral("\\*/"));
}

void CHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Block comments (state 1 = inside /* ... */)
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(m_commentStart);

    while (startIndex >= 0) {
        const QRegularExpressionMatch endMatch = m_commentEnd.match(text, startIndex);
        int endIndex = endMatch.capturedStart();
        int length;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            length = text.length() - startIndex;
        } else {
            length = endIndex - startIndex + endMatch.capturedLength();
        }
        setFormat(startIndex, length, m_commentFormat);
        startIndex = text.indexOf(m_commentStart, startIndex + length);
    }
}
