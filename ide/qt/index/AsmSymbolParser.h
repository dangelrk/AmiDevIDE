#pragma once

#include "Symbol.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Line-oriented m68k asm parser (plan section 8): column-0 global labels,
// local .x/x$ labels scoped to the preceding global label, equ/=/set,
// macro definitions, and include/incdir directives.
namespace AsmSymbolParser {

struct Result {
    QList<Symbol> symbols;
    // Each entry: {pathText, isIncdir}. incdir entries are search-path
    // additions, not direct file includes.
    QList<QPair<QString, bool>> includes;
};

Result parse(const QString &text, const QString &absoluteFilePath);

}
