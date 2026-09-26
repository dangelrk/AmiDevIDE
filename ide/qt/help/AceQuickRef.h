#pragma once

#include <QHash>
#include <QString>

// One-line help for ACE BASIC keywords, read from the ACE SDK's
// quickref.txt ($VBCC/sdk/ACE/quickref.txt, installed by the ACE repo's
// "make -f Makefile-host install"). That file is ACE's own maintained
// quick reference -- the same text CubicIDE shows -- so the IDE does not
// keep a second copy of the language documentation.
//
// Format is one entry per line: "<WORD> <description>", where WORD may end
// in '(' for functions (e.g. "ABS(") or in a type suffix (e.g. "ARG$(").
class AceQuickRef {
public:
    // Loads (once) from `sdkDir`/quickref.txt. Calling it again with a
    // different directory reloads; a missing file leaves the index empty,
    // which simply means no BASIC help is offered.
    void load(const QString &sdkDir);

    bool isEmpty() const { return m_entries.isEmpty(); }

    // Case-insensitive lookup. `word` is matched as given and, failing
    // that, with a '(' appended, so hovering "ABS" finds the "ABS(" entry.
    // Returns an empty string when there is no entry.
    QString lookup(const QString &word) const;

private:
    QString m_loadedFrom;
    QHash<QString, QString> m_entries; // upper-cased word -> description
};
