#ifndef AIDE_MUI_SETTINGSWINDOW_H
#define AIDE_MUI_SETTINGSWINDOW_H

/* Project settings window -- MUI counterpart of
 * qt/project/ProjectSettingsDialog.{h,cpp}'s General/Compiler/Assembler/
 * Linker/SDKs tabs (its Run tab is FS-UAE-specific and has no meaning on
 * real/emulated AmigaOS, where "Run" is just executing the built binary
 * -- see mainwindow.c's do_run() -- so it's omitted here; the underlying
 * AideRunConfig fields are still preserved verbatim through the .aproj
 * JSON round trip, just not editable from this UI).
 *
 * SDKs tab lists the 9 built-in SDK ids (ndk-3.2 through posixlib, see
 * aide_sdk_append_builtins()) as checkboxes -- user-defined SDK
 * registration (Qt's "Add User SDK...") isn't implemented on this side,
 * but a user SDK's id already in the project survives OK unchanged. */

#include <libraries/mui.h>

#include "aide/aide_project.h"

void settingswindow_init(Object *app);
void settingswindow_free(void);

/* Opens the window, populating every field from a deep copy of
 * *project (via a JSON round trip -- see settingswindow.c). Fields this
 * UI doesn't expose (sources/headers/editor state/run config) are
 * preserved unedited in that copy. */
void settingswindow_show(const AideProject *project);

/* Returns 0 if `id` doesn't belong to this window; 1 if the window was
 * closed via Cancel; 2 if it was closed via OK -- the caller should then
 * call settingswindow_take_result(); 3 if it was handled but the window
 * is still open (an internal Add/Remove-library click) -- distinct from
 * 1/Cancel so a caller doing something on window-close (e.g. mainwindow's
 * "Neu" -> settings -> save flow) doesn't misfire on every library edit. */
int settingswindow_handle_id(long id);

/* Only valid right after settingswindow_handle_id() returned 2: frees
 * *outProject's current contents and replaces them with the edited
 * project. */
void settingswindow_take_result(AideProject *outProject);

#endif /* AIDE_MUI_SETTINGSWINDOW_H */
