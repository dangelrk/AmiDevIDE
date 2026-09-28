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
 * SDKs tab has a checkbox per SDK in the registry -- the built-ins plus
 * every AmiDevSDK.json found under SDK: -- and one per SDK the project
 * names that is not installed, ticked and marked as such so it survives
 * OK. SDKs are not entered here the way Qt's "Add SDK..." does: on the
 * Amiga an SDK describes itself with an AmiDevSDK.json. */

#include <libraries/mui.h>

#include "aide/aide_project.h"
#include "aide/aide_sdk.h"

/* `registry` is read on every settingswindow_show(); the caller keeps it
 * alive for the window's lifetime. */
void settingswindow_init(Object *app, const AideSdkList *registry);
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
