#include "SymbolIndexer.h"
#include "CSymbolParser.h"
#include "AsmSymbolParser.h"
#include "BasicSymbolParser.h"
#include "IncludeGraph.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

SymbolIndexer::SymbolIndexer(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QList<Symbol>>();
}

void SymbolIndexer::setProjectContext(const QString &projectRoot, const QStringList &includePaths,
                                      ProjectLanguage language)
{
    m_projectRoot = projectRoot;
    m_includePaths = includePaths;
    m_language = language;
}

void SymbolIndexer::indexFile(const QString &absolutePath)
{
    QFile file(absolutePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return;

    QTextStream in(&file);
    const QString text = in.readAll();
    const QString suffix = QFileInfo(absolutePath).suffix().toLower();
    const QString fileDir = QFileInfo(absolutePath).absolutePath();

    QList<Symbol> symbols;
    QStringList resolvedIncludes;

    const bool isAsm = suffix == QLatin1String("s") || suffix == QLatin1String("asm") || suffix == QLatin1String("i");
    // ".h" belongs to whichever language the project is written in; see
    // HighlighterFactory for why content sniffing is not used.
    const bool isBasic = suffix == QLatin1String("b") || suffix == QLatin1String("bas")
                         || (m_language == ProjectLanguage::Basic && suffix == QLatin1String("h"));
    if (isBasic) {
        const auto result = BasicSymbolParser::parse(text, absolutePath);
        symbols = result.symbols;
        for (const QString &inc : result.includes) {
            // yap resolves <...> against ACE_INC and "..." relative to the
            // including file; both are covered by the same search paths the
            // C side uses, so treat them alike here.
            const QString resolved = IncludeGraph::resolveInclude(inc, false, fileDir, m_includePaths);
            if (!resolved.isEmpty())
                resolvedIncludes << resolved;
        }
    } else if (isAsm) {
        const auto result = AsmSymbolParser::parse(text, absolutePath);
        symbols = result.symbols;
        for (const auto &inc : result.includes) {
            // "include" is a direct file reference; "incdir" only adds a
            // search path and isn't itself an edge in the include graph.
            if (inc.second)
                continue;
            const QString resolved = IncludeGraph::resolveInclude(inc.first, false, fileDir, m_includePaths);
            if (!resolved.isEmpty())
                resolvedIncludes << resolved;
        }
    } else {
        const auto result = CSymbolParser::parse(text, absolutePath);
        symbols = result.symbols;
        for (const auto &inc : result.includes) {
            const QString resolved = IncludeGraph::resolveInclude(inc.first, inc.second, fileDir, m_includePaths);
            if (!resolved.isEmpty())
                resolvedIncludes << resolved;
        }
    }

    emit fileIndexed(absolutePath, symbols, resolvedIncludes);
}
