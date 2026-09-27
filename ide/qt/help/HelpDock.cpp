#include "HelpDock.h"

#include "aide/aide_autodoc.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextDocumentFragment>
#include <QTextCursor>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const char *const kSectionHeaders[] = {
    "NAME", "SYNOPSIS", "FUNCTION", "INPUTS", "RESULT", "NOTES", "EXAMPLE",
    "EXAMPLES", "BUGS", "SEE ALSO", "TAGS", "WARNING", "ARGUMENTS", "HISTORY", "CAVEATS",
};
}

HelpDock::HelpDock(QWidget *parent)
    : QWidget(parent)
    , m_search(new QLineEdit(this))
    , m_resultsList(new QListWidget(this))
    , m_browser(new QTextBrowser(this))
    , m_backButton(new QToolButton(this))
    , m_forwardButton(new QToolButton(this))
    , m_searchSelectionButton(new QToolButton(this))
{
    m_search->setPlaceholderText(tr("Search autodocs... (* and ? wildcards)"));
    connect(m_search, &QLineEdit::returnPressed, this, &HelpDock::onSearchSubmitted);

    m_resultsList->setVisible(false);
    m_resultsList->setMaximumHeight(160);
    connect(m_resultsList, &QListWidget::itemActivated, this, &HelpDock::onCandidateActivated);

    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(false);
    connect(m_browser, &QTextBrowser::anchorClicked, this, &HelpDock::onAnchorClicked);
    m_browser->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_browser, &QWidget::customContextMenuRequested, this, &HelpDock::showBrowserContextMenu);

    m_backButton->setText(QStringLiteral("←"));
    m_backButton->setToolTip(tr("Back"));
    m_backButton->setEnabled(false);
    connect(m_backButton, &QToolButton::clicked, this, &HelpDock::goBack);

    m_forwardButton->setText(QStringLiteral("→"));
    m_forwardButton->setToolTip(tr("Forward"));
    m_forwardButton->setEnabled(false);
    connect(m_forwardButton, &QToolButton::clicked, this, &HelpDock::goForward);

    m_searchSelectionButton->setText(tr("Search Selection"));
    m_searchSelectionButton->setToolTip(tr("Look up the selected word in the help"));
    m_searchSelectionButton->setEnabled(false);
    connect(m_searchSelectionButton, &QToolButton::clicked, this, &HelpDock::searchSelection);
    connect(m_browser, &QTextBrowser::copyAvailable, m_searchSelectionButton, &QToolButton::setEnabled);

    auto *toolRow = new QWidget(this);
    auto *toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->addWidget(m_backButton);
    toolLayout->addWidget(m_forwardButton);
    toolLayout->addWidget(m_search);
    toolLayout->addWidget(m_searchSelectionButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(toolRow);
    layout->addWidget(m_resultsList);
    layout->addWidget(m_browser);
}

void HelpDock::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void HelpDock::prefillAndSearch(const QString &word)
{
    m_search->setText(word);
    onSearchSubmitted();
}

QString HelpDock::queryAt(const QPoint *pos) const
{
    // selection().toPlainText(), not selectedText(): the latter marks each
    // line break with U+2029, which any 8-bit conversion turns into '?' --
    // a wildcard to the core. UTF-8 keeps every non-ASCII character at
    // 0x80 and above, where the core never takes it for part of a name.
    QString text = m_browser->textCursor().selection().toPlainText();
    if (text.isEmpty() && pos) {
        QTextCursor cursor = m_browser->cursorForPosition(*pos);
        cursor.select(QTextCursor::WordUnderCursor);
        text = cursor.selection().toPlainText();
    }

    char query[128];
    if (!aide_autodoc_query_from_selection(text.toUtf8().constData(), query, sizeof(query)))
        return QString();
    return QString::fromLatin1(query);
}

void HelpDock::searchFor(const QString &query)
{
    if (query.isEmpty())
        return;
    m_search->setText(query);
    onSearchSubmitted();
}

void HelpDock::searchSelection()
{
    searchFor(queryAt(nullptr));
}

void HelpDock::showBrowserContextMenu(const QPoint &pos)
{
    // The standard menu wants document coordinates (for "Copy Link
    // Location"); pos is in viewport coordinates, like everything else here.
    const QPoint docPos = pos + QPoint(m_browser->horizontalScrollBar()->value(),
                                       m_browser->verticalScrollBar()->value());
    QMenu *menu = m_browser->createStandardContextMenu(docPos);
    const QString query = queryAt(&pos);
    if (!query.isEmpty()) {
        menu->addSeparator();
        menu->addAction(tr("Search Help for \"%1\"").arg(query), this, [this, query] { searchFor(query); });
    }
    menu->exec(m_browser->viewport()->mapToGlobal(pos));
    delete menu;
}

void HelpDock::showCandidateList(const QStringList &names)
{
    m_resultsList->clear();

    if (names.isEmpty()) {
        auto *item = new QListWidgetItem(tr("(no matches)"), m_resultsList);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    } else {
        m_resultsList->addItems(names);
    }

    m_resultsList->setVisible(true);
    m_browser->setPlainText(names.isEmpty() ? QString()
                                             : tr("%1 match(es) -- select one above.").arg(names.size()));
    setWindowTitle(tr("Search Results"));
}

void HelpDock::onCandidateActivated(QListWidgetItem *item)
{
    if (!(item->flags() & Qt::ItemIsEnabled))
        return;
    m_resultsList->setVisible(false);
    emit autodocRequested(item->text());
}

QString HelpDock::linkify(const QString &escapedHtml) const
{
    QString result = escapedHtml;

    for (const char *header : kSectionHeaders) {
        static const auto opts = QRegularExpression::MultilineOption;
        const QRegularExpression re(QStringLiteral("^(\\s*)(%1)(\\s*)$").arg(header), opts);
        result.replace(re, QStringLiteral("\\1<b>\\2</b>\\3"));
    }

    // Word() references -> autodoc:// links (e.g. "OpenWindow()" in a SEE ALSO list).
    static const QRegularExpression callRe(QStringLiteral(R"re(\b([A-Z][A-Za-z0-9_]{2,})\(\))re"));
    result.replace(callRe, QStringLiteral(R"(<a href="autodoc://\1">\1</a>())"));

    return result;
}

void HelpDock::renderHtml(const QString &html)
{
    m_browser->setHtml(html);
}

void HelpDock::pushHistory(const QString &title, const QString &html)
{
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append({ title, html });
    m_historyIndex = m_history.size() - 1;

    m_backButton->setEnabled(m_historyIndex > 0);
    m_forwardButton->setEnabled(false);
    m_resultsList->setVisible(false);

    renderHtml(html);
    setWindowTitle(title);
}

void HelpDock::showAutodocEntry(const QString &title, const QString &rawText)
{
    const QString escaped = rawText.toHtmlEscaped();
    const QString html = QStringLiteral("<pre>%1</pre>").arg(linkify(escaped));
    pushHistory(title, html);
}

void HelpDock::showPlainText(const QString &title, const QString &text)
{
    const QString html = QStringLiteral("<pre>%1</pre>").arg(text.toHtmlEscaped());
    pushHistory(title, html);
}

void HelpDock::onAnchorClicked(const QUrl &url)
{
    if (url.scheme() == QLatin1String("autodoc"))
        emit autodocRequested(url.host().isEmpty() ? url.path() : url.host());
}

void HelpDock::onSearchSubmitted()
{
    const QString query = m_search->text().trimmed();
    if (!query.isEmpty())
        emit autodocRequested(query);
}

void HelpDock::goBack()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    m_resultsList->setVisible(false);
    renderHtml(m_history[m_historyIndex].second);
    setWindowTitle(m_history[m_historyIndex].first);
    m_backButton->setEnabled(m_historyIndex > 0);
    m_forwardButton->setEnabled(true);
}

void HelpDock::goForward()
{
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size() - 1)
        return;
    ++m_historyIndex;
    m_resultsList->setVisible(false);
    renderHtml(m_history[m_historyIndex].second);
    setWindowTitle(m_history[m_historyIndex].first);
    m_backButton->setEnabled(true);
    m_forwardButton->setEnabled(m_historyIndex < m_history.size() - 1);
}
