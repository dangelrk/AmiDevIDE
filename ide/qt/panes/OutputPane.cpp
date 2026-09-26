#include "OutputPane.h"

#include <QFontDatabase>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>

OutputPane::OutputPane(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setMaximumBlockCount(20000);
}

QRegularExpression OutputPane::locationPattern()
{
    // Matches "path/to/file.c:123" possibly followed by more text.
    static const QRegularExpression re(QStringLiteral("([^\\s:][^\\s:]*\\.(?:c|h|s|asm|i)):(\\d+)"));
    return re;
}

void OutputPane::appendLine(const QString &line)
{
    // Only follow new output if the user was already looking at the
    // bottom -- someone who scrolled up mid-build to re-read an earlier
    // diagnostic shouldn't get yanked back down by the next line.
    QScrollBar *bar = verticalScrollBar();
    const bool wasAtBottom = bar->value() >= bar->maximum() - 1;

    appendPlainText(line);

    if (wasAtBottom)
        bar->setValue(bar->maximum());
}

void OutputPane::clearOutput()
{
    clear();
}

void OutputPane::mouseDoubleClickEvent(QMouseEvent *event)
{
    QPlainTextEdit::mouseDoubleClickEvent(event);

    const QTextCursor cursor = cursorForPosition(event->pos());
    const QString text = cursor.block().text();

    const QRegularExpressionMatch match = locationPattern().match(text);
    if (match.hasMatch()) {
        emit locationActivated(match.captured(1), match.captured(2).toInt());
    }
}
