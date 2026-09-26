#include "CSymbolParser.h"

#include "aide/aide_cparser.h"

// Thin adapter over the portable core/ implementation (core/src/aide_cparser.c,
// shared with the Amiga/MUI build).
// All parsing logic lives in aide_cparser.c now; this file only converts
// the result to QString/QList.

namespace CSymbolParser {

namespace {
SymbolKind toQtKind(AideSymbolKind k)
{
    switch (k) {
    case AIDE_SYM_FUNCTION: return SymbolKind::Function;
    case AIDE_SYM_PROTOTYPE: return SymbolKind::Prototype;
    case AIDE_SYM_VARIABLE: return SymbolKind::Variable;
    case AIDE_SYM_DEFINE: return SymbolKind::Define;
    case AIDE_SYM_TYPEDEF: return SymbolKind::Typedef;
    case AIDE_SYM_STRUCT: return SymbolKind::Struct;
    case AIDE_SYM_UNION: return SymbolKind::Union;
    case AIDE_SYM_ENUM: return SymbolKind::Enum;
    case AIDE_SYM_LABEL: return SymbolKind::Label;
    case AIDE_SYM_LOCAL_LABEL: return SymbolKind::LocalLabel;
    case AIDE_SYM_EQU_CONSTANT: return SymbolKind::EquConstant;
    case AIDE_SYM_ASM_MACRO: return SymbolKind::AsmMacro;
    case AIDE_SYM_STRUCT_MEMBER: default: return SymbolKind::StructMember;
    }
}
}

Result parse(const QString &text, const QString &absoluteFilePath)
{
    const QByteArray textUtf8 = text.toUtf8();
    const QByteArray pathUtf8 = absoluteFilePath.toUtf8();

    AideCParseResult aideResult;
    aide_cparser_result_init(&aideResult);
    aide_cparser_parse(textUtf8.constData(), pathUtf8.constData(), &aideResult);

    Result result;
    for (size_t i = 0; i < aideResult.symbols.count; ++i) {
        const AideSymbol &a = aideResult.symbols.items[i];
        Symbol s;
        s.name = QString::fromUtf8(a.name);
        s.kind = toQtKind(a.kind);
        s.file = QString::fromUtf8(a.file);
        s.line = a.line;
        s.signature = QString::fromUtf8(a.signature);
        s.scope = QString::fromUtf8(a.scope);
        s.offset = a.offset;
        result.symbols << s;
    }
    for (size_t i = 0; i < aideResult.includes.count; ++i) {
        const AideCInclude &inc = aideResult.includes.items[i];
        result.includes << qMakePair(QString::fromUtf8(inc.directive), bool(inc.quoted));
    }

    aide_cparser_result_free(&aideResult);
    return result;
}

} // namespace CSymbolParser
