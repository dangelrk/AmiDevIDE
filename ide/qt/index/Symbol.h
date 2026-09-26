#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

enum class SymbolKind {
    Function,
    Prototype,
    Variable,
    Define,
    Typedef,
    Struct,
    Union,
    Enum,
    Label,
    LocalLabel,
    EquConstant,
    AsmMacro,
    StructMember,
};

struct Symbol {
    QString name;
    SymbolKind kind = SymbolKind::Function;
    QString file;       // absolute path
    int line = 0;
    QString signature;  // for tooltips: return type + params, or raw declaration text; for a StructMember, its declared type
    QString scope;      // for local asm labels: the enclosing global label; for a StructMember: the owning struct/union (or typedef) name
    // For a Struct/Union/Typedef symbol representing a struct: its total
    // size in bytes, once StructLayout has computed it (-1 until then). For
    // a StructMember: its byte offset within the owning struct.
    int offset = -1;
};

Q_DECLARE_METATYPE(Symbol)
Q_DECLARE_METATYPE(QList<Symbol>)
