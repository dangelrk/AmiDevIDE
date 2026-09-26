/*
 * main.c — AmigaIDE (MUI) entry point: open_libs(), the main window,
 * close_libs().
 *
 * UNVERIFIED beyond compiling/linking clean with vbcc against the real
 * toolchain (see amiga/Makefile.amiga-cross) -- there is no FS-UAE/real-
 * Amiga interactive test rig in this environment; the user runs this.
 */
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include "mui/mui_stubs.h"
#include "app/mainwindow.h"

struct Library       *MUIMasterBase;
struct IntuitionBase *IntuitionBase;
struct Library        *RegexBase; /* core/include/aide/aide_regex.h backend, see mui/aide_regex_amiga.c */
struct Library        *ReqToolsBase; /* string/file requesters, see mui/reqtools_stubs.h */

long __stack = 65536;
/* __DATE__/__TIME__ resolve at compile time, so this (and the main
 * window's title bar, see mainwindow.c) changes on every rebuild --
 * visible via Workbench Icons->Information, no manual counter needed. */
const char amiga_ver[] = "$VER: AmigaIDE 0.1 (unverified) " __DATE__ " " __TIME__;

static int open_libs(void)
{
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    /* regex.library backs every core/ module's pattern matching (symbol
     * parsers, SFD/autodoc lookup, project search). Required, not
     * optional: without
     * it every aide_regex_compile() call returns NULL and the affected
     * feature silently does nothing rather than crashing (see
     * amiga/src/mui/aide_regex_amiga.c), but that's most of the IDE's
     * indexing/search functionality, so treat it as a hard dependency
     * here and report it plainly instead of limping along degraded. */
    RegexBase = OpenLibrary("regex.library", 0);
    /* reqtools.library backs every file/string requester (mainwindow.c's
     * ask_string()/ask_file()) -- same "hard dependency, report plainly"
     * reasoning as regex.library above; without it there is no way to
     * name a project or pick a file at all. */
    ReqToolsBase = OpenLibrary("reqtools.library", 38);
    return MUIMasterBase && IntuitionBase && RegexBase && ReqToolsBase;
}

static void close_libs(void)
{
    if (ReqToolsBase) CloseLibrary(ReqToolsBase);
    if (RegexBase) CloseLibrary(RegexBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (MUIMasterBase) CloseLibrary(MUIMasterBase);
    ReqToolsBase = NULL;
    RegexBase = NULL;
    IntuitionBase = NULL;
    MUIMasterBase = NULL;
}

int main(void)
{
    int rc;


    if (!open_libs()) {
        if (MUIMasterBase) {
            MUI_Request(NULL, NULL, 0, "AmigaIDE",
                        "_OK", "Konnte intuition.library/regex.library/reqtools.library "
                               "nicht oeffnen (regex.library und reqtools.library muessen "
                               "in LIBS: installiert sein).");
        }
        close_libs();
        return 20;
    }

    rc = mainwindow_run();

    close_libs();
    return rc;
}
