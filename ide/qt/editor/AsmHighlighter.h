#pragma once

#include "EditorTheme.h"

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

// vasmm68k_mot-flavoured m68k assembler highlighter (case-insensitive
// mnemonics/directives, column-0 labels, local .x/x$ labels, ;/'*' comments).
// Token colors come from the given EditorTheme.
class AsmHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit AsmHighlighter(QTextDocument *parent, const EditorTheme &theme);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<Rule> m_rules;
};
