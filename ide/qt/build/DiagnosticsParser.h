#pragma once

#include "panes/Issue.h"

#include <QList>
#include <QString>
#include <optional>

// Parses vc/vbccm68k, vasmm68k_mot and vlink diagnostics out of build output.
//
// IMPORTANT (verified against the real toolchain, contradicting the original
// plan doc): vc/vbccm68k does NOT use a "file:line: warning N: msg" one-line
// format. It uses the SAME two-token format as vasmm68k_mot:
//
//   warning 161 in line 2 of "bad.c": implicit declaration of function ...
//   error 76 in line 2 of "bad2.c": identifier expected
//
// possibly preceded or followed by a ">    <source context>" line, which we
// ignore. vlink errors look like:
//
//   bad3.o: In "_main":
//   Error 21: bad3.o (CODE+0x2): Reference to undefined symbol ...
//
// with no source line number (the file is the .o, not the original source).
namespace DiagnosticsParser {

// Parses a single line. Returns an Issue if the line matched a known
// diagnostic format, std::nullopt otherwise (includes ">" context lines and
// summary lines like "1 error found!").
std::optional<Issue> parseLine(const QString &line);

// Convenience: parses a whole buffer, one call per line.
QList<Issue> parse(const QStringList &lines);

} // namespace DiagnosticsParser
