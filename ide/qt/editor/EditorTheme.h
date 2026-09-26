#pragma once

#include <QColor>
#include <QString>
#include <QVector>

// A named color palette applied to the editor surface (background/foreground/
// current-line/line-numbers) and to the syntax highlighters' token colors.
// CHighlighter and AsmHighlighter both draw their QTextCharFormats from the
// same EditorTheme so C and assembly sources stay visually consistent.
struct EditorTheme {
    QString id;
    QString displayName;
    bool dark = false;

    QColor background;
    QColor foreground;
    QColor currentLineBackground;
    QColor lineNumberForeground;

    QColor keyword;
    QColor type;
    QColor number;
    QColor preprocessor;
    QColor function;
    QColor string;
    QColor comment;
    QColor label;
    QColor localLabel;

    // Resolves a theme id (as stored by AppSettings::theme()) to a concrete
    // palette. "system" -- and any id that doesn't match a known theme --
    // picks a dark or light built-in based on the current OS appearance.
    static const EditorTheme &resolve(const QString &id);

    // All concrete (non-"system") built-in themes, in display order. Used to
    // populate the theme picker in SettingsDialog.
    static const QVector<EditorTheme> &allThemes();
};
