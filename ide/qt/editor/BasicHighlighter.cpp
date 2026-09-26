#include "BasicHighlighter.h"

#include "aide/aide_keywords.h"

#include <QStringList>

namespace {

// Builds an alternation from one of core's generated tables. Entries are
// sorted longest-first so a multi-word command ("END SUB", "AREA STEP")
// wins over its own first word, which is usually also a keyword.
QString alternationFrom(const char *const *items, size_t count)
{
    QStringList parts;
    parts.reserve(int(count));
    for (size_t i = 0; i < count; ++i)
        parts << QString::fromLatin1(items[i]);
    std::sort(parts.begin(), parts.end(), [](const QString &a, const QString &b) {
        if (a.size() != b.size())
            return a.size() > b.size();
        return a < b;
    });
    for (QString &p : parts) {
        p = QRegularExpression::escape(p);
        // a space in a dictionary entry may be any run of whitespace
        p.replace(QStringLiteral("\\ "), QStringLiteral("\\s+"));
    }
    return parts.join('|');
}

} // namespace

BasicHighlighter::BasicHighlighter(QTextDocument *parent, const EditorTheme &theme)
    : QSyntaxHighlighter(parent)
{
    size_t commandCount = 0;
    size_t functionCount = 0;
    const char *const *commands = aide_keywords_basic_commands(&commandCount);
    const char *const *functions = aide_keywords_basic_functions(&functionCount);

    // Commands: the statement vocabulary (PRINT, FOR, SUB, ...).
    QTextCharFormat commandFormat;
    commandFormat.setForeground(theme.keyword);
    commandFormat.setFontWeight(QFont::Bold);
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral("(?<![\\w$&!%#])(%1)\\b").arg(alternationFrom(commands, commandCount)),
            QRegularExpression::CaseInsensitiveOption);
        rule.format = commandFormat;
        m_rules.append(rule);
    }

    // Functions: ABS(, LEN(, MID$( and friends. Several already end in a
    // type suffix, so \b would not match after it -- allow either.
    QTextCharFormat functionFormat;
    functionFormat.setForeground(theme.function);
    {
        Rule rule;
        rule.pattern = QRegularExpression(
            QStringLiteral("(?<![\\w$&!%#])(%1)(?![\\w])").arg(alternationFrom(functions, functionCount)),
            QRegularExpression::CaseInsensitiveOption);
        rule.format = functionFormat;
        m_rules.append(rule);
    }

    // A label is an identifier alone on its line, followed by ':'.
    QTextCharFormat labelFormat;
    labelFormat.setForeground(theme.label);
    labelFormat.setFontWeight(QFont::Bold);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^\\s*[A-Za-z_][A-Za-z0-9_]*\\s*:\\s*$"));
        rule.format = labelFormat;
        m_rules.append(rule);
    }

    // Decimal, hex ($ or &H) and binary (&B) literals, plus floats.
    QTextCharFormat numberFormat;
    numberFormat.setForeground(theme.number);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral(
            "(?<![\\w$])(\\$[0-9a-fA-F]+|&[hH][0-9a-fA-F]+|&[bB][01]+"
            "|[0-9]*\\.[0-9]+([eE][-+]?[0-9]+)?|[0-9]+)[&!%#]?"));
        rule.format = numberFormat;
        m_rules.append(rule);
    }

    // yap directives (#include, #define, #ifdef, ...).
    QTextCharFormat preprocFormat;
    preprocFormat.setForeground(theme.preprocessor);
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("^\\s*#\\s*[A-Za-z]+.*$"));
        rule.format = preprocFormat;
        m_rules.append(rule);
    }

    QTextCharFormat stringFormat;
    stringFormat.setForeground(theme.string);
    {
        Rule rule;
        // BASIC has no escapes: a string ends at the next quote.
        rule.pattern = QRegularExpression(QStringLiteral("\"[^\"]*\"?"));
        rule.format = stringFormat;
        m_rules.append(rule);
    }

    m_commentFormat.setForeground(theme.comment);
    m_commentFormat.setFontItalic(true);
    {
        // REM only as a whole word: REMOVE is an identifier.
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("(?<![\\w])REM\\b.*$"),
                                          QRegularExpression::CaseInsensitiveOption);
        rule.format = m_commentFormat;
        m_rules.append(rule);
    }
    {
        Rule rule;
        rule.pattern = QRegularExpression(QStringLiteral("'[^\n]*$"));
        rule.format = m_commentFormat;
        m_rules.append(rule);
    }
}

void BasicHighlighter::applyBlockComments(const QString &text)
{
    // { ... } and /* ... */ both span lines, so the open one is carried
    // across blocks. Only one can be open at a time; ACE does not nest
    // them, and neither does this.
    static const QRegularExpression braceOpen(QStringLiteral("\\{"));
    static const QRegularExpression cStyleOpen(QStringLiteral("/\\*"));

    int state = previousBlockState() > 0 ? previousBlockState() : StateNone;
    int index = 0;

    setCurrentBlockState(StateNone);

    while (index <= text.length()) {
        if (state == StateNone) {
            const int brace = text.indexOf(braceOpen, index);
            const int cStyle = text.indexOf(cStyleOpen, index);
            int start;

            if (brace < 0 && cStyle < 0)
                return;
            if (cStyle < 0 || (brace >= 0 && brace < cStyle)) {
                start = brace;
                state = StateBrace;
            } else {
                start = cStyle;
                state = StateCStyle;
            }
            index = start;
        }

        {
            const QString closer = state == StateBrace ? QStringLiteral("}") : QStringLiteral("*/");
            const int searchFrom = index + (state == StateBrace ? 1 : 2);
            const int end = text.indexOf(closer, searchFrom);

            if (end < 0) {
                setFormat(index, text.length() - index, m_commentFormat);
                setCurrentBlockState(state);
                return;
            }
            const int length = end - index + closer.length();
            setFormat(index, length, m_commentFormat);
            index += length;
            state = StateNone;
        }
    }
}

void BasicHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : std::as_const(m_rules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Last, so a block comment wins over anything matched inside it.
    applyBlockComments(text);
}
