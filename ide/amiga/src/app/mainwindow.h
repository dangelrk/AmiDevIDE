#ifndef AIDE_MAINWINDOW_H
#define AIDE_MAINWINDOW_H

/* Builds and runs the AmigaIDE main window (menu, project tree, editor,
 * output/issues) and its event loop (Application/Window/
 * MUIM_Application_Input), modelled on
 * mui38dev/MUI/Developer/C/Examples/EnvBrowser.c.
 *
 * UNVERIFIED beyond compiling clean with vbcc against the real toolchain
 * (see amiga/Makefile.amiga-cross) -- there is no
 * FS-UAE/real-Amiga interactive test rig in this environment. */

/* Returns a process exit code. */
int mainwindow_run(void);

#endif /* AIDE_MAINWINDOW_H */
