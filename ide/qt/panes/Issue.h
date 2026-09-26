#pragma once

#include <QList>
#include <QString>

enum class IssueSeverity {
    Error,
    Warning,
    Note,
};

struct Issue {
    IssueSeverity severity = IssueSeverity::Error;
    QString file;
    int line = 0;
    QString message;
};
