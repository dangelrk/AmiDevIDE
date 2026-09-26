#include "aide/aide_theme.h"

#include <string.h>

static const AideEditorTheme kThemes[] = {
    { "one-dark", "One Dark", 1,
      0x282c34, 0xabb2bf, 0x2c313c, 0x495162,
      0xc678dd, 0xe5c07b, 0xd19a66, 0x56b6c2, 0x61afef, 0x98c379, 0x5c6370, 0x61afef, 0x98c379 },
    { "one-light", "One Light", 0,
      0xfafafa, 0x383a42, 0xf0f0f1, 0x9d9d9f,
      0xa626a4, 0xc18401, 0x986801, 0x0184bc, 0x4078f2, 0x50a14f, 0xa0a1a7, 0x4078f2, 0x50a14f },
    { "solarized-dark", "Solarized Dark", 1,
      0x002b36, 0x839496, 0x073642, 0x586e75,
      0x859900, 0xb58900, 0xd33682, 0x2aa198, 0x268bd2, 0x2aa198, 0x586e75, 0x268bd2, 0x2aa198 },
    { "solarized-light", "Solarized Light", 0,
      0xfdf6e3, 0x657b83, 0xeee8d5, 0x93a1a1,
      0x859900, 0xb58900, 0xd33682, 0x2aa198, 0x268bd2, 0x2aa198, 0x93a1a1, 0x268bd2, 0x2aa198 },
    { "monokai", "Monokai", 1,
      0x272822, 0xf8f8f2, 0x3e3d32, 0x75715e,
      0xf92672, 0x66d9ef, 0xae81ff, 0x66d9ef, 0xa6e22e, 0xe6db74, 0x75715e, 0xa6e22e, 0xe6db74 },
};

static const size_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

const AideEditorTheme *aide_theme_all(size_t *outCount)
{
    *outCount = kThemeCount;
    return kThemes;
}

const AideEditorTheme *aide_theme_find(const char *id)
{
    size_t i;
    for (i = 0; i < kThemeCount; ++i) {
        if (strcmp(kThemes[i].id, id) == 0)
            return &kThemes[i];
    }
    return NULL;
}
