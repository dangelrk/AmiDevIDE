#include "BasicSymbolParser.h"

#include "aide/aide_basicparser.h"

// Thin adapter over the portable core/ implementation
// (core/src/aide_basicparser.c), shared with the Amiga/MUI build in the
// same way AsmSymbolParser wraps aide_asmparser.

namespace BasicSymbolParser {

static SymbolKind toKind(AideSymbolKind k)
{
    switch (k) {
    case AIDE_SYM_PROTOTYPE:     return SymbolKind::Prototype;
    case AIDE_SYM_VARIABLE:      return SymbolKind::Variable;
    case AIDE_SYM_DEFINE:        return SymbolKind::Define;
    case AIDE_SYM_STRUCT:        return SymbolKind::Struct;
    case AIDE_SYM_STRUCT_MEMBER: return SymbolKind::StructMember;
    case AIDE_SYM_LABEL:         return SymbolKind::Label;
    default:                     return SymbolKind::Function;
    }
}

Result parse(const QString &text, const QString &absoluteFilePath)
{
    const QByteArray textUtf8 = text.toUtf8();
    const QByteArray pathUtf8 = absoluteFilePath.toUtf8();

    AideBasicParseResult aideResult;
    aide_basicparser_result_init(&aideResult);
    aide_basicparser_parse(textUtf8.constData(), pathUtf8.constData(), &aideResult);

    Result result;
    for (size_t i = 0; i < aideResult.symbols.count; ++i) {
        const AideSymbol &a = aideResult.symbols.items[i];
        Symbol s;
        s.name = QString::fromUtf8(a.name);
        s.kind = toKind(a.kind);
        s.file = QString::fromUtf8(a.file);
        s.line = a.line;
        s.signature = QString::fromUtf8(a.signature);
        s.scope = QString::fromUtf8(a.scope);
        result.symbols << s;
    }
    for (size_t i = 0; i < aideResult.includes.count; ++i)
        result.includes << QString::fromUtf8(aideResult.includes.items[i]);

    aide_basicparser_result_free(&aideResult);
    return result;
}

} // namespace BasicSymbolParser
