#include "app/prefswindow.h"

#include <libraries/iffparse.h> /* MAKE_ID() */
#include <clib/alib_protos.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "mui/mui_stubs.h"

#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

#define ID_PREFS_OK     3101
#define ID_PREFS_CANCEL 3102

#define ENV_MAKE       "AmigaIDE/make"
#define ENV_SDKROOT    "AmigaIDE/sdkroot"
#define ENV_VBCCROOT   "AmigaIDE/vbccroot"

static Object *g_app;
static Object *g_window;
static Object *g_makeStr, *g_sdkRootStr, *g_toolchainRootStr;
static Object *g_okBtn, *g_cancelBtn;

/* The persisted values themselves -- not per-project, so these live
 * here rather than in mainwindow.c's g_project. Read once at
 * prefswindow_load() (before mainwindow_run() initializes the
 * toolchain/SDK registry from them), updated on every OK. */
static char g_makeProgram[64]   = "make";
static char g_sdkRoot[64]       = "SDK:";
static char g_toolchainRoot[64] = "vbcc:";

static void load_var(const char *name, char *out, size_t outsz, const char *fallback)
{
    LONG len = GetVar((CONST_STRPTR)name, (STRPTR)out, (LONG)outsz - 1, GVF_GLOBAL_ONLY);
    if (len < 0) {
        strncpy(out, fallback, outsz - 1);
        out[outsz - 1] = '\0';
    }
}

/* Loads the persisted values only -- no widgets, no Object *app needed.
 * Called at the very top of mainwindow_run(), before
 * aide_toolchain_init()/aide_sdk_append_builtins() (which need
 * prefs_toolchain_root()/prefs_sdk_root() already loaded) -- well
 * before g_app itself exists, which is why this is split out from
 * prefswindow_init() rather than folded into it. */
void prefswindow_load(void)
{
    load_var(ENV_MAKE, g_makeProgram, sizeof(g_makeProgram), "make");
    load_var(ENV_SDKROOT, g_sdkRoot, sizeof(g_sdkRoot), "SDK:");
    load_var(ENV_VBCCROOT, g_toolchainRoot, sizeof(g_toolchainRoot), "vbcc:");
}

void prefswindow_init(Object *app)
{
    g_app = app;

    g_makeStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_makeProgram), End;
    g_sdkRootStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_sdkRoot), End;
    g_toolchainRootStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_toolchainRoot), End;
    g_okBtn = SimpleButton("_OK");
    g_cancelBtn = SimpleButton("_Abbrechen");

    g_window = WindowObject,
        MUIA_Window_ID,    MAKE_ID('P','R','E','F'),
        MUIA_Window_Title, "AmigaIDE - Einstellungen",
        MUIA_Window_Menu,  MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, ColGroup(2),
                Child, Label("Make-Programm:"),  Child, g_makeStr,
                Child, Label("SDK-Root:"),        Child, g_sdkRootStr,
                Child, Label("Toolchain-Root:"),  Child, g_toolchainRootStr,
            End,
            Child, HGroup,
                Child, g_okBtn,
                Child, g_cancelBtn,
            End,
        End,
    End;

    DoMethod(g_app, OM_ADDMEMBER, g_window);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_PREFS_CANCEL);
    DoMethod(g_okBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_PREFS_OK);
    DoMethod(g_cancelBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_PREFS_CANCEL);
}

void prefswindow_free(void)
{
    /* Nothing dynamically allocated to release -- g_window is an
     * OM_ADDMEMBER child of g_app and gets disposed along with it. */
}

void prefswindow_show(void)
{
    set(g_makeStr, MUIA_String_Contents, g_makeProgram);
    set(g_sdkRootStr, MUIA_String_Contents, g_sdkRoot);
    set(g_toolchainRootStr, MUIA_String_Contents, g_toolchainRoot);
    set(g_window, MUIA_Window_Open, TRUE);
}

int prefswindow_handle_id(long id)
{
    switch (id) {
    case ID_PREFS_OK: {
        STRPTR v;
        char *make = NULL, *sdkRoot = NULL, *toolchainRoot = NULL;

        get(g_makeStr, MUIA_String_Contents, &v);
        make = aide_str_trim_dup(v ? (char *)v : "");
        get(g_sdkRootStr, MUIA_String_Contents, &v);
        sdkRoot = aide_str_trim_dup(v ? (char *)v : "");
        get(g_toolchainRootStr, MUIA_String_Contents, &v);
        toolchainRoot = aide_str_trim_dup(v ? (char *)v : "");

        if (!make[0] || !sdkRoot[0] || !toolchainRoot[0]) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Kein Feld darf leer sein.");
            free(make); free(sdkRoot); free(toolchainRoot);
            return 3; /* handled, window still open -- mirrors settingswindow's convention */
        }

        strncpy(g_makeProgram, make, sizeof(g_makeProgram) - 1);
        g_makeProgram[sizeof(g_makeProgram) - 1] = '\0';
        strncpy(g_sdkRoot, sdkRoot, sizeof(g_sdkRoot) - 1);
        g_sdkRoot[sizeof(g_sdkRoot) - 1] = '\0';
        strncpy(g_toolchainRoot, toolchainRoot, sizeof(g_toolchainRoot) - 1);
        g_toolchainRoot[sizeof(g_toolchainRoot) - 1] = '\0';
        free(make); free(sdkRoot); free(toolchainRoot);

        SetVar((CONST_STRPTR)ENV_MAKE, (CONST_STRPTR)g_makeProgram, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
        SetVar((CONST_STRPTR)ENV_SDKROOT, (CONST_STRPTR)g_sdkRoot, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
        SetVar((CONST_STRPTR)ENV_VBCCROOT, (CONST_STRPTR)g_toolchainRoot, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);

        set(g_window, MUIA_Window_Open, FALSE);
        return 2;
    }
    case ID_PREFS_CANCEL:
        set(g_window, MUIA_Window_Open, FALSE);
        return 1;
    default:
        return 0;
    }
}

const char *prefs_make_program(void) { return g_makeProgram; }
const char *prefs_sdk_root(void) { return g_sdkRoot; }
const char *prefs_toolchain_root(void) { return g_toolchainRoot; }
