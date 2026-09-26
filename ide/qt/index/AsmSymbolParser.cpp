#include "AsmSymbolParser.h"

#include "aide/aide_asmparser.h"

// Thin adapter over the portable core/ implementation (core/src/aide_asmparser.c,
// shared with the Amiga/MUI build).

namespace AsmSymbolParser {

Result parse(const QString &text, const QString &absoluteFilePath)
{
    const QByteArray textUtf8 = text.toUtf8();
    const QByteArray pathUtf8 = absoluteFilePath.toUtf8();

    AideAsmParseResult aideResult;
    aide_asmparser_result_init(&aideResult);
    aide_asmparser_parse(textUtf8.constData(), pathUtf8.constData(), &aideResult);

    Result result;
    for (size_t i = 0; i < aideResult.symbols.count; ++i) {
        const AideSymbol &a = aideResult.symbols.items[i];
        Symbol s;
        s.name = QString::fromUtf8(a.name);
        s.kind = a.kind == AIDE_SYM_LABEL ? SymbolKind::Label
               : a.kind == AIDE_SYM_LOCAL_LABEL ? SymbolKind::LocalLabel
               : a.kind == AIDE_SYM_EQU_CONSTANT ? SymbolKind::EquConstant
               : SymbolKind::AsmMacro;
        s.file = QString::fromUtf8(a.file);
        s.line = a.line;
        s.scope = QString::fromUtf8(a.scope);
        result.symbols << s;
    }
    for (size_t i = 0; i < aideResult.includes.count; ++i) {
        const AideAsmInclude &inc = aideResult.includes.items[i];
        result.includes << qMakePair(QString::fromUtf8(inc.path), bool(inc.isIncdir));
    }

    aide_asmparser_result_free(&aideResult);
    return result;
}

} // namespace AsmSymbolParser
