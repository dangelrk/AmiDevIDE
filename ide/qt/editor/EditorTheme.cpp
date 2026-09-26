#include "EditorTheme.h"

#include "aide/aide_theme.h"

#include <QApplication>
#include <QPalette>

// Thin adapter over the portable core/ implementation (core/src/aide_theme.c,
// shared with the Amiga/MUI build).
// The color table itself lives in aide_theme.c now; "system" (OS light/dark)
// resolution is inherently Qt-specific and stays here.

namespace {
// QColor(QRgb) expects 0xAARRGGBB with a non-zero alpha byte; aide_theme.c
// packs plain 0xRRGGBB (alpha implicitly opaque, like the "#RRGGBB" string
// literals this replaces), so every value needs 0xFF000000 OR'd in here.
QColor toQColor(unsigned long rgb)
{
    return QColor(QRgb(0xFF000000u | rgb));
}

EditorTheme fromAide(const AideEditorTheme &a)
{
    EditorTheme t;
    t.id = QString::fromUtf8(a.id);
    t.displayName = QString::fromUtf8(a.displayName);
    t.dark = a.dark != 0;
    t.background = toQColor(a.background);
    t.foreground = toQColor(a.foreground);
    t.currentLineBackground = toQColor(a.currentLineBackground);
    t.lineNumberForeground = toQColor(a.lineNumberForeground);
    t.keyword = toQColor(a.keyword);
    t.type = toQColor(a.type);
    t.number = toQColor(a.number);
    t.preprocessor = toQColor(a.preprocessor);
    t.function = toQColor(a.function);
    t.string = toQColor(a.string);
    t.comment = toQColor(a.comment);
    t.label = toQColor(a.label);
    t.localLabel = toQColor(a.localLabel);
    return t;
}
}

const QVector<EditorTheme> &EditorTheme::allThemes()
{
    static const QVector<EditorTheme> themes = [] {
        size_t count = 0;
        const AideEditorTheme *all = aide_theme_all(&count);
        QVector<EditorTheme> v;
        v.reserve(int(count));
        for (size_t i = 0; i < count; ++i)
            v.append(fromAide(all[i]));
        return v;
    }();
    return themes;
}

const EditorTheme &EditorTheme::resolve(const QString &id)
{
    const QVector<EditorTheme> &themes = allThemes();

    if (id != QStringLiteral("system")) {
        for (const EditorTheme &t : themes) {
            if (t.id == id)
                return t;
        }
    }

    // "system" (or an id that doesn't match any built-in theme): follow the
    // OS appearance rather than fall back to a fixed default.
    const bool osDark = qApp->palette().color(QPalette::Window).lightness() < 128;
    const QString fallbackId = osDark ? QStringLiteral("one-dark") : QStringLiteral("one-light");
    for (const EditorTheme &t : themes) {
        if (t.id == fallbackId)
            return t;
    }
    return themes.first();
}
