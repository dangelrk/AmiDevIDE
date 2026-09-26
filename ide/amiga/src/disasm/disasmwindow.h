#ifndef AIDE_MUI_DISASMWINDOW_H
#define AIDE_MUI_DISASMWINDOW_H

/* Disassembly window: reads an AmigaOS hunk executable/object off disk,
 * disassembles every CODE hunk with aide_disasm, and shows the result as
 * plain "address: hex  instruction" text in a read-only TextEditor.mcc
 * pane (an NList-per-instruction view isn't worth the complexity for a
 * single scrollable read-only listing).
 *
 * Owns a private MUI sub-window registered into `app` on init, hidden
 * until first shown. Has a "Speichern..." button (selection alone
 * wasn't usable -- see MUIA_TextEditor_ActiveObjectOnClick note in the
 * .c file -- and there's no menu on this secondary window to hang a
 * save command off of); its press routes through the same
 * MUIM_Application_ReturnID loop as the rest of the app, so
 * disasmwindow_handle_id() must be called from mainwindow's
 * MUIM_Application_Input switch's default case. Closing the window
 * itself is still handled locally (no round trip needed for that). */

#include <libraries/mui.h>

void disasmwindow_init(Object *app);
void disasmwindow_free(void);

/* Opens/updates the window with `binaryPath`'s disassembly, or an error
 * message if it can't be read or parsed as a hunk file. */
void disasmwindow_show(const char *binaryPath);

/* Returns 1 and performs the action if `id` belongs to this window
 * (currently just "Speichern..."); 0 otherwise. */
int disasmwindow_handle_id(long id);

#endif /* AIDE_MUI_DISASMWINDOW_H */
