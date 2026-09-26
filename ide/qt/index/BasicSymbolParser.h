#pragma once

#include "Symbol.h"

#include <QList>
#include <QString>
#include <QStringList>

// Line-oriented ACE BASIC parser for the symbol outline: SUB/FUNCTION/
// METHOD definitions, DECLARE prototypes, STRUCT definitions and their
// members, CONST, module-level variables, labels, and #include directives.
namespace BasicSymbolParser {

struct Result {
    QList<Symbol> symbols;
    QStringList includes;
};

Result parse(const QString &text, const QString &absoluteFilePath);

}
