#ifndef AIDE_MUI_PREFSWINDOW_H
#define AIDE_MUI_PREFSWINDOW_H

/* Global (not per-project) AmigaIDE settings window: make program name,
 * SDK root, toolchain root -- previously hardcoded in mainwindow.c
 * ("make", "SDK:", "vbcc:"). Persisted under ENVARC:AmigaIDE/{make,
 * sdkroot,vbccroot} (see prefswindow.c), read once at startup before
 * aide_toolchain_init()/aide_sdk_append_builtins() run in
 * mainwindow_run().
 *
 * Mirrors project/settingswindow.h's shape (init/free/show/handle_id),
 * much smaller: three plain string fields, no tabs, no validation
 * beyond "not empty" -- see its own header comment for why a *project*
 * settings window doesn't cover these (they aren't project data). */

#include <libraries/mui.h>

#include "aide/aide_sdk.h"

/* Loads the persisted values from ENVARC: (or defaults) with no Object*
 * needed -- call this FIRST, before aide_toolchain_init()/
 * aide_sdk_append_builtins() and before g_app itself exists.
 * prefswindow_init() (below) only builds the window's widgets from
 * whatever this loaded; it does not reload from ENVARC: itself. */
void prefswindow_load(void);

void prefswindow_init(Object *app);
void prefswindow_free(void);
void prefswindow_show(void);

/* Moves every built-in SDK the user gave an own drawer (page
 * "SDK-Pfade", ENVARC:AmigaIDE/sdkroot-<id>) there, with
 * aide_sdk_list_set_root(). Call right after aide_sdk_append_builtins()
 * -- and before aide_sdk_append_from_manifests(), so only built-ins move. */
void prefs_apply_sdk_roots(AideSdkList *registry);

/* Returns 0 if `id` doesn't belong to this window; 1 if it was closed
 * via Cancel; 2 if it was closed via OK -- the caller should then
 * re-init anything derived from the new values (toolchain, SDK
 * registry, make-availability cache) via the getters below. */
int prefswindow_handle_id(long id);

/* Current values -- "make"/"SDK:"/"vbcc:" until prefswindow_init() has
 * loaded ENVARC: (or found nothing there), or whatever the user last
 * saved since. Never NULL, never empty. */
const char *prefs_make_program(void);
const char *prefs_sdk_root(void);
const char *prefs_toolchain_root(void);
/* AmiVCS (optional tool): the amivcs program, default "amivcs" (C:) */
const char *prefs_amivcs_program(void);
/* the AmiVCS GUI (amivcs-gui), started by Versionen > AmiVCS-GUI oeffnen */
const char *prefs_amivcs_gui_program(void);

#endif /* AIDE_MUI_PREFSWINDOW_H */
