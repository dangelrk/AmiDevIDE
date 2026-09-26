#include "EditorWidget.h"
#include "LineNumberArea.h"
#include "app/AppSettings.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFile>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QStringListModel>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextStream>
#include <QFileInfo>
#include <QToolTip>

EditorWidget::EditorWidget(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_completer(new QCompleter(this))
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    applyEditorSettings();

    connect(this, &QPlainTextEdit::blockCountChanged, this, &EditorWidget::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &EditorWidget::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &EditorWidget::highlightCurrentLine);

    m_completer->setModel(new QStringListModel(m_completer));
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated),
            this, &EditorWidget::insertCompletion);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

void EditorWidget::setCompletionWords(const QStringList &words)
{
    auto *model = qobject_cast<QStringListModel *>(m_completer->model());
    model->setStringList(words);
}

void EditorWidget::showCallTip(const QString &text)
{
    QToolTip::showText(mapToGlobal(cursorRect().bottomLeft()), text, this);
}

QString EditorWidget::textUnderCursorPrefix() const
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

void EditorWidget::insertCompletion(const QString &completion)
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    cursor.insertText(completion);
    setTextCursor(cursor);
}

void EditorWidget::setHighlighter(QSyntaxHighlighter *highlighter)
{
    delete m_highlighter;
    m_highlighter = highlighter;
}

void EditorWidget::setTabWidthChars(int chars)
{
    QFontMetrics metrics(font());
    setTabStopDistance(metrics.horizontalAdvance(' ') * chars);
}

void EditorWidget::applyEditorSettings()
{
    AppSettings &settings = AppSettings::instance();

    QFont editorFont = settings.editorFontFamily().isEmpty()
                            ? QFontDatabase::systemFont(QFontDatabase::FixedFont)
                            : QFont(settings.editorFontFamily());
    editorFont.setPointSize(settings.editorFontPointSize());
    setFont(editorFont);

    m_theme = EditorTheme::resolve(settings.theme());
    QPalette pal = palette();
    pal.setColor(QPalette::Base, m_theme.background);
    pal.setColor(QPalette::Text, m_theme.foreground);
    pal.setColor(QPalette::Window, m_theme.background);
    setPalette(pal);

    setTabWidthChars(settings.editorTabWidth());

    highlightCurrentLine();
    if (m_lineNumberArea)
        m_lineNumberArea->update();
}

bool EditorWidget::loadFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return false;

    QTextStream in(&file);
    setPlainText(in.readAll());
    m_filePath = path;
    document()->setModified(false);
    emit filePathChanged(m_filePath);
    return true;
}

bool EditorWidget::saveFile(const QString &path)
{
    const QString target = path.isEmpty() ? m_filePath : path;
    if (target.isEmpty())
        return false;

    QFile file(target);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    QTextStream out(&file);
    out << toPlainText();
    m_filePath = target;
    document()->setModified(false);
    emit filePathChanged(m_filePath);
    return true;
}

int EditorWidget::lineNumberAreaWidth() const
{
    int digits = 1;
    int maxValue = qMax(1, blockCount());
    while (maxValue >= 10) {
        maxValue /= 10;
        ++digits;
    }
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void EditorWidget::updateLineNumberAreaWidth(int /*newBlockCount*/)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void EditorWidget::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void EditorWidget::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void EditorWidget::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(m_theme.currentLineBackground);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void EditorWidget::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), m_theme.background);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const QString number = QString::number(blockNumber + 1);
            painter.setPen(m_theme.lineNumberForeground);
            painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                              Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

QString EditorWidget::wordUnderCursor() const
{
    QTextCursor cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    return cursor.selectedText();
}

QString EditorWidget::enclosingCallIdentifier() const
{
    const QString text = toPlainText();
    const int pos = textCursor().position();

    int depth = 0;
    int i = pos - 1;
    while (i >= 0) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char(')')) {
            ++depth;
        } else if (ch == QLatin1Char('(')) {
            if (depth == 0)
                break;
            --depth;
        }
        --i;
    }
    if (i < 0)
        return QString(); // no unmatched '(' before the cursor

    int j = i - 1;
    while (j >= 0 && text.at(j).isSpace())
        --j;
    const int identEnd = j + 1;
    while (j >= 0 && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('_')))
        --j;
    const int identStart = j + 1;
    if (identStart >= identEnd)
        return QString();
    return text.mid(identStart, identEnd - identStart);
}

int EditorWidget::currentLine() const
{
    return textCursor().blockNumber() + 1;
}

void EditorWidget::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            && !(m_completer && m_completer->popup()->isVisible())) {
        // Auto-indent: the new line inherits the previous line's leading
        // whitespace -- per user feedback. Not brace-depth-aware (doesn't
        // add extra indentation after an opening brace), just carries
        // over what was already there; see aide_reindent_braces() for the
        // separate "Format" menu action that reindents a whole file by
        // brace depth (that one's needed on the Amiga side too, where
        // TextEditor.mcc has no hook to intercept Enter, so it doesn't
        // get this live behavior at all).
        const QString lineText = textCursor().block().text();
        QString indent;
        for (QChar ch : lineText) {
            if (ch == QLatin1Char(' ') || ch == QLatin1Char('\t'))
                indent += ch;
            else
                break;
        }
        QPlainTextEdit::keyPressEvent(event);
        if (!indent.isEmpty()) {
            QTextCursor cursor = textCursor();
            cursor.insertText(indent);
            setTextCursor(cursor);
        }
        return;
    }
    if (event->key() == Qt::Key_Space
            && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        // Manual call-tip re-trigger (e.g. after moving to a later argument):
        // checked ahead of the plain Ctrl+Space completer branch below, since
        // that one only tests the Control bit and would otherwise also match.
        const QString word = enclosingCallIdentifier();
        if (!word.isEmpty())
            emit signatureHelpRequested(word);
        return;
    }
    if (event->key() == Qt::Key_Space && (event->modifiers() & Qt::ControlModifier)) {
        const QString prefix = textUnderCursorPrefix();
        m_completer->setCompletionPrefix(prefix);
        QRect rect = cursorRect();
        rect.setWidth(m_completer->popup()->sizeHintForColumn(0)
                      + m_completer->popup()->verticalScrollBar()->sizeHint().width());
        m_completer->complete(rect);
        return;
    }
    if (event->key() == Qt::Key_F3 && (event->modifiers() & Qt::ShiftModifier)) {
        const QString word = wordUnderCursor();
        if (!word.isEmpty())
            emit findReferencesRequested(word);
        return;
    }
    if (event->key() == Qt::Key_F3) {
        const QString word = wordUnderCursor();
        if (!word.isEmpty())
            emit gotoDefinitionRequested(word, currentLine());
        return;
    }
    if (event->key() == Qt::Key_F1 && (event->modifiers() & Qt::ShiftModifier)) {
        const QString word = wordUnderCursor();
        if (!word.isEmpty())
            emit structInfoRequested(word);
        return;
    }
    if (event->key() == Qt::Key_F1) {
        const QString word = wordUnderCursor();
        if (!word.isEmpty())
            emit helpRequested(word);
        return;
    }
    if (event->key() == Qt::Key_F2) {
        const QString word = wordUnderCursor();
        if (!word.isEmpty())
            emit autodocSearchRequested(word);
        return;
    }
    if (event->text() == QLatin1String("(")) {
        // The cursor is still right after the identifier at this point (the
        // '(' hasn't been inserted yet), so wordUnderCursor() picks it up
        // directly -- no need for the enclosingCallIdentifier() bracket scan.
        const QString word = wordUnderCursor();
        if (!word.isEmpty())
            emit signatureHelpRequested(word);
        // fall through: still insert the '(' normally.
    }
    QPlainTextEdit::keyPressEvent(event);
}

void EditorWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        QTextCursor cursor = cursorForPosition(event->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        const QString word = cursor.selectedText();
        if (!word.isEmpty()) {
            emit gotoDefinitionRequested(word, cursor.blockNumber() + 1);
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}
