#pragma once

#include "Symbol.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Lightweight, line-oriented C parser (plan section 8): strips comments and
// string/char literals first, tracks brace depth. Extracts #define, typedef
// (both "typedef X Name;" and "typedef struct {...} Name;"), struct/union/
// enum tags, function prototypes/definitions at depth 0, and #include
// directives. False positives (K&R, function pointers, macro-generated
// declarations) are accepted, per plan's explicit risk tolerance for a light
// indexer -- global variable declarations are deliberately not extracted, to
// keep the heuristics' false-positive surface small.
namespace CSymbolParser {

struct Result {
    QList<Symbol> symbols;
    // Each entry: {directiveText, quoted}. quoted == true for "..." includes.
    QList<QPair<QString, bool>> includes;
};

Result parse(const QString &text, const QString &absoluteFilePath);

}
