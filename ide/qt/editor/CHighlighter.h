#pragma once

#include "EditorTheme.h"

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QVector>

// C99 + Amiga-typedef aware highlighter for QPlainTextEdit. Token colors come
// from the given EditorTheme, so re-constructing with a different theme (see
// HighlighterFactory::attach) re-colors the document.
class CHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit CHighlighter(QTextDocument *parent, const EditorTheme &theme);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<Rule> m_rules;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_preprocessorFormat;

    QRegularExpression m_commentStart;
    QRegularExpression m_commentEnd;
};
