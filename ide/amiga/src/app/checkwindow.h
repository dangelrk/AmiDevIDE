#ifndef AIDE_MUI_CHECKWINDOW_H
#define AIDE_MUI_CHECKWINDOW_H

/* Projekt > Installation pruefen...: one window that lists everything
 * AmigaIDE and a build need -- drawers (vbcc:, SDK:, the vbcc target,
 * ACE:), compiler and tools (vc, vbccm68k, vasm, vlink, make, ace,
 * amivcs), libraries and MUI classes, SDKs -- each with whether it is
 * there, its version and where it is, or the Aminet package that
 * provides it. Rebuilt on every open and on "Erneut pruefen", so it
 * always shows the machine as it is now.
 *
 * Shape like the other windows: init/show/handle_id/free. */

#include <libraries/mui.h>

#include "aide/aide_sdk.h"
#include "aide/aide_str.h"

/* What the check looks at. Everything is borrowed and must stay valid
 * while the window is open (the prefs getters and mainwindow.c's
 * registry/project do). projectSdks may be NULL (no project). */
typedef struct CheckContext {
    const char *toolchainRoot;   /* "vbcc:" */
    const char *sdkRoot;         /* "SDK:" */
    const char *makeProgram;     /* "make" or a path */
    const char *amivcsProgram;   /* "amivcs" or a path */
    const char *amivcsGuiProgram;
    const AideSdkList *sdks;
    const AideStrList *projectSdks;
} CheckContext;

void checkwindow_init(Object *app);
void checkwindow_free(void);
/* Runs the check and opens (or refreshes) the window. */
void checkwindow_show(const CheckContext *ctx);
/* 1 if `id` was this window's (handled), 0 otherwise. */
int checkwindow_handle_id(long id);

#endif /* AIDE_MUI_CHECKWINDOW_H */
