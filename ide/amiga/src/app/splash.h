#ifndef AIDE_SPLASH_H
#define AIDE_SPLASH_H

/* Startup splash: a plain Intuition window, no MUI. main() opens it as
 * soon as intuition.library is there -- before muimaster.library and the
 * MCCs are loaded, which together with building the MUI object tree is
 * what makes the start slow -- and mainwindow_run() closes it once the
 * main window is up.
 *
 * Every call is safe without an open splash (no Workbench screen, no
 * graphics.library, out of memory): the IDE then just starts without one.
 * splash_close() may be called any number of times. */

/* `version` is the second line, e.g. "Version 0.6 (09.11.2026)". */
void splash_open(const char *version);

/* Shows `text` as the current step and the bar at `percent` (0..100).
 * Drawn right away; `text` is not kept. */
void splash_status(const char *text, int percent);

void splash_close(void);

#endif /* AIDE_SPLASH_H */
