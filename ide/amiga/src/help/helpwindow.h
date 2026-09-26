#ifndef AIDE_MUI_HELPWINDOW_H
#define AIDE_MUI_HELPWINDOW_H

/* Hothelp/Autodoc window (F1 exact lookup, F2 wildcard search, and
 * Shift+F1 struct-layout display reuses its text pane): read-only
 * TextEditor, NList of candidates, back/forward (the search
 * field is F2's ask_string() input dialog instead of a BetterString for
 * v1, same reasoning as the rest of this app's input handling).
 *
 * Owns a private MUI sub-window registered into `app` on init, hidden
 * until first shown. Candidate double-click and Back/Forward route
 * through the same MUIM_Application_ReturnID loop as the rest of the
 * app; helpwindow_handle_id() must be called from mainwindow's
 * MUIM_Application_Input switch's default case. */

#include <libraries/mui.h>

#include "aide/aide_sdk.h"
#include "aide/aide_autodoc.h"

void helpwindow_init(Object *app, const char *cacheDir);
void helpwindow_free(void);

/* Rescans every active SDK's autodocs (see aide_autodocindex_reload) --
 * call after the project's SDK selection changes. Caller keeps ownership
 * of activeSdks. `progress`/`progressCtx` may be NULL to disable -- see
 * aide_autodocindex_reload()'s own doc comment. */
void helpwindow_reload_index(const AideSdkDescriptor *activeSdks, size_t sdkCount,
                              AideAutodocProgressFn progress, void *progressCtx);

/* F1: shows the window with every match for functionName as candidates
 * (exact/case-insensitive/Tags->TagList fallback, see aide_autodoc.h),
 * auto-selecting and displaying the first one. Pushes to Back/Forward
 * history. */
void helpwindow_show_function(const char *functionName);

/* F2: shows the window with every indexed name matching the "*"/"?"
 * wildcard `pattern` as candidates. Does not auto-select one (multiple
 * libraries may share a name) -- double-click a candidate to display it. */
void helpwindow_search_wildcard(const char *pattern);

/* Shift+F1 and similar: shows arbitrary plain text (not from the autodoc
 * index) in the window's text pane, with an empty candidate list. Does
 * not touch Back/Forward history. */
void helpwindow_show_text(const char *title, const char *text);

/* Returns 1 and performs the action if `id` belongs to this window
 * (candidate double-click, Back, Forward); 0 otherwise. */
int helpwindow_handle_id(long id);

#endif /* AIDE_MUI_HELPWINDOW_H */
