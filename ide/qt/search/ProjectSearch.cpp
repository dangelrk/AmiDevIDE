#include "ProjectSearch.h"

#include "aide/aide_search.h"

// Thin adapter over the portable core/ implementation (core/src/aide_search.c,
// shared with the Amiga/MUI build).
// The Qt QRegularExpression \b...\b wrap for wholeWord is replaced by a
// manual boundary check in aide_search.c (POSIX ERE has no \b) -- see that
// file's search_line() for the equivalence argument.

namespace ProjectSearch {

QList<SearchMatch> search(const QStringList &files, const QString &pattern,
                          bool caseSensitive, bool wholeWord, bool regex)
{
    AideStrList fileList;
    aide_strlist_init(&fileList);
    for (const QString &f : files) {
        const QByteArray utf8 = f.toUtf8();
        aide_strlist_push(&fileList, utf8.constData());
    }

    const QByteArray patternUtf8 = pattern.toUtf8();
    AideSearchMatchList aideMatches;
    aide_search_matchlist_init(&aideMatches);
    aide_search_search(&fileList, patternUtf8.constData(), caseSensitive ? 1 : 0, wholeWord ? 1 : 0,
                        regex ? 1 : 0, &aideMatches);
    aide_strlist_free(&fileList);

    QList<SearchMatch> matches;
    matches.reserve(int(aideMatches.count));
    for (size_t i = 0; i < aideMatches.count; ++i) {
        SearchMatch m;
        m.file = QString::fromUtf8(aideMatches.items[i].file);
        m.line = aideMatches.items[i].line;
        m.column = aideMatches.items[i].column;
        m.lineText = QString::fromUtf8(aideMatches.items[i].lineText);
        m.isDefinition = aideMatches.items[i].isDefinition != 0;
        matches << m;
    }
    aide_search_matchlist_free(&aideMatches);
    return matches;
}

} // namespace ProjectSearch
