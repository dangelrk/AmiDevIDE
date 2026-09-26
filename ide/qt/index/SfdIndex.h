#pragma once

#include "aide/aide_sfd.h"

#include <QString>
#include <QStringList>

// Amiga function name -> library + prototype, parsed from *_lib.sfd files
// (plan section 2/8): "==libname"/"==base" directives, then C prototypes
// with a trailing register-allocation list, e.g.
//   ULONG Supervisor(ULONG (*userFunction)()) (a5)
// with continuation lines (tab-indented) for long parameter lists. Used for
// Hothelp routing (which library owns a given OS call), signature tooltips,
// and completion.
//
// Thin adapter over the portable core/ implementation (core/src/aide_sfd.c,
// shared with the Amiga/MUI build);
// this class just owns the AideSfdIndex's lifetime and converts
// QString/QStringList at the edges. find() returns a pointer into the C
// index's storage wrapped in a thin, always-current SfdFunction view --
// callers only ever use it transiently (see call sites), so this is safe.
struct SfdFunction {
    QString name;
    QString library;      // e.g. "exec.library"
    QString rawSignature; // full "ReturnType Name(params)" text
    QString registers;    // raw contents of the trailing register list
};

class SfdIndex {
public:
    SfdIndex();
    ~SfdIndex();
    SfdIndex(const SfdIndex &) = delete;
    SfdIndex &operator=(const SfdIndex &) = delete;

    void reload(const QStringList &sfdDirs);

    // Returns std::nullopt-like via a pointer to a cache slot valid until
    // the next find()/reload() call (mirrors the previous QHash-pointer
    // lifetime contract exactly).
    const SfdFunction *find(const QString &functionName) const;
    QStringList allFunctionNames() const;

private:
    AideSfdIndex m_index;
    mutable SfdFunction m_lastFound;
};
