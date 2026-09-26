#ifndef AIDE_THEME_H
#define AIDE_THEME_H

/* Portable C99 mirror of editor/EditorTheme.h's data half: the 5 built-in
 * color palettes (One Dark/Light, Solarized Dark/Light, Monokai), each 13
 * RGB token colors. Pure data, reused as-is by the MUI side's PenPolicy
 * -- the "system" (OS light/dark)
 * resolution logic is inherently UI-toolkit-specific (Qt's qApp->palette()
 * vs. MUI's screen depth) and stays in each side's own adapter/UI layer,
 * not here. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideEditorTheme {
    const char *id;
    const char *displayName;
    int dark; /* bool */

    /* Every color is packed 0xRRGGBB. */
    unsigned long background;
    unsigned long foreground;
    unsigned long currentLineBackground;
    unsigned long lineNumberForeground;

    unsigned long keyword;
    unsigned long type;
    unsigned long number;
    unsigned long preprocessor;
    unsigned long function;
    unsigned long string;
    unsigned long comment;
    unsigned long label;
    unsigned long localLabel;
} AideEditorTheme;

/* Returns the built-in theme table, in display order; *outCount is its
 * length. The returned pointer is static -- never free() it. */
const AideEditorTheme *aide_theme_all(size_t *outCount);

/* Finds a built-in theme by id (e.g. "one-dark"). Returns NULL if `id`
 * doesn't match any -- callers decide their own fallback (Qt: OS
 * appearance; MUI: screen depth), matching the two sides' genuinely
 * different "system theme" semantics. */
const AideEditorTheme *aide_theme_find(const char *id);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_THEME_H */
