#pragma once

#include <QStringList>

// Static keyword lists (C/Amiga-type/asm) that seed completion alongside the
// live SymbolDatabase names and SFD function names MainWindow supplies.
namespace CompletionModel {
QStringList staticKeywords();
}
