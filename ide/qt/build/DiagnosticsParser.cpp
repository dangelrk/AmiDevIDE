#include "DiagnosticsParser.h"

#include "aide/aide_diag.h"

#include <cstdlib>

namespace DiagnosticsParser {

// Thin adapter over the portable core/ implementation (core/src/aide_diag.c)
// shared with the Amiga/MUI build.
// Behavior (including the verified toolchain formats documented in the
// header) lives entirely in aide_diag_parse_line() now.
std::optional<Issue> parseLine(const QString &line)
{
    const QByteArray utf8 = line.toUtf8();
    AideIssue cIssue{};
    if (!aide_diag_parse_line(utf8.constData(), &cIssue))
        return std::nullopt;

    Issue issue;
    issue.severity = cIssue.severity == AIDE_ISSUE_ERROR ? IssueSeverity::Error : IssueSeverity::Warning;
    issue.file = QString::fromUtf8(cIssue.file);
    issue.line = cIssue.line;
    issue.message = QString::fromUtf8(cIssue.message);
    aide_issue_release(&cIssue);
    return issue;
}

QList<Issue> parse(const QStringList &lines)
{
    QList<Issue> issues;
    for (const QString &line : lines) {
        if (auto issue = parseLine(line))
            issues << *issue;
    }
    return issues;
}

} // namespace DiagnosticsParser
