#pragma once

#include <QHash>
#include <QList>
#include <QString>

// Computes AmigaOS (m68k/vbcc natural alignment -- no #pragma pack, and
// crucially no 4-byte alignment requirement: the 68000 only needs 2-byte
// word alignment, so a 4-byte LONG/pointer only needs to start on an even
// address) struct layouts from a flat, in-order member list. Used by
// CSymbolParser to attach byte offsets to struct/union members, and
// directly by the struct-info Shift+F1 feature's tests.
namespace StructLayout {

// One member as declared, before layout: `type` is the declared type text
// (e.g. "APTR", "UBYTE", "struct Node *", "struct Node", "UWORD"), `name` is
// the declarator, optionally with a trailing "[N]" array suffix (e.g.
// "pad[4]").
struct MemberDecl {
    QString type;
    QString name;
};

struct MemberLayout {
    QString name;   // array suffix stripped
    QString type;
    int offset = 0;
    int size = 0;   // 0 if the type couldn't be resolved (unknown embedded struct)
};

struct Result {
    QList<MemberLayout> members;
    int totalSize = 0;
};

// `knownStructSizes` supplies sizes (name -> bytes) for embedded (non-
// pointer) struct/union members whose tag isn't a built-in Amiga base type
// -- typically structs already computed earlier in the same file. A member
// whose type can't be resolved this way gets size 0, which means every
// member after it is offset incorrectly; that's an accepted, documented
// limitation for structs that embed a type defined in a different file.
Result compute(const QList<MemberDecl> &members, const QHash<QString, int> &knownStructSizes);

} // namespace StructLayout
