#pragma once

#include "EditorTheme.h"

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// ACE BASIC highlighter. Keywords come from core's generated tables, which
// are produced from the ACE repo's own CubicIDE dictionaries, so commands
// and functions stay in step with the language and are coloured apart the
// way CubicIDE colours them.
//
// ACE has four comment forms (docs/ref.txt): REM and ' run to end of line,
// while { ... } and C-style block comments can span lines and therefore
// need block state.
class BasicHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit BasicHighlighter(QTextDocument *parent, const EditorTheme &theme);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    // Which block comment, if any, is still open at the end of a block.
    enum BlockState { StateNone = 0, StateBrace = 1, StateCStyle = 2 };

    void applyBlockComments(const QString &text);

    QVector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
};
