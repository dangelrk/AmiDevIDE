#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Line-oriented text search across a set of files. Backs both "Find in
// Project..." and "Find All References" (there is no reference index --
// CSymbolParser/AsmSymbolParser only record definitions -- so references are
// located via a whole-word textual scan of the project's own files instead).
namespace ProjectSearch {

struct SearchMatch {
    QString file;             // absolute path
    int line = 0;             // 1-based
    int column = 0;           // 0-based, start of the match within lineText
    QString lineText;         // full line, for display in the results dock
    bool isDefinition = false; // set by the caller (e.g. find-references cross-checking SymbolDatabase); search() itself never sets this
};

// `pattern` is a literal substring unless `regex` is true, in which case it's
// used as a QRegularExpression pattern directly. `wholeWord` wraps the
// (possibly already-regex) pattern in \b...\b. Files that can't be opened are
// silently skipped. Returns every match, in file order then line order.
QList<SearchMatch> search(const QStringList &files, const QString &pattern,
                          bool caseSensitive, bool wholeWord, bool regex);

} // namespace ProjectSearch
