#include "app/prefswindow.h"

#include <libraries/iffparse.h> /* MAKE_ID() */
#include <clib/alib_protos.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "mui/mui_stubs.h"
#include "mui/reqtools_stubs.h"

#include "aide/aide_sdk.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

#define ID_PREFS_OK     3101
#define ID_PREFS_CANCEL 3102
#define ID_PREFS_SDK_BROWSE 3110 /* + row index */

#define ENV_MAKE       "AmigaIDE/make"
#define ENV_SDKROOT    "AmigaIDE/sdkroot"
#define ENV_VBCCROOT   "AmigaIDE/vbccroot"
#define ENV_AMIVCS     "AmigaIDE/amivcs"
#define ENV_AMIVCSGUI  "AmigaIDE/amivcs-gui"
#define ENV_SDKROOT_PREFIX "AmigaIDE/sdkroot-" /* + SDK id */

/* Where each built-in SDK lives, if the user put it somewhere else than
 * <SDK-Root>/<its default drawer> ("Work:NDK32" instead of "SDK:NDK_3.2").
 * Optional: an empty field means the default. One ENVARC: variable per
 * SDK, ENVARC:AmigaIDE/sdkroot-<id>, so a missing one simply means
 * "default". Applied with aide_sdk_list_set_root(). */
#define PREFS_MAX_SDK_ROWS 16
#define PREFS_SDK_ROOT_LEN 128
typedef struct SdkRootRow {
    char id[32];
    char label[48];
    char help[PREFS_SDK_ROOT_LEN + 16]; /* "Standard: SDK:NDK_3.2", MUIA_ShortHelp */
    char deflt[PREFS_SDK_ROOT_LEN];
    char root[PREFS_SDK_ROOT_LEN];      /* "" = default */
    Object *str;
    Object *browse;
} SdkRootRow;
static SdkRootRow g_sdkRows[PREFS_MAX_SDK_ROWS];
static int g_sdkRowCount;
static const char *g_prefsTabTitles[] = { "Allgemein", "SDK-Pfade", NULL };

static Object *g_app;
static Object *g_window;
static Object *g_makeStr, *g_sdkRootStr, *g_toolchainRootStr, *g_amivcsStr, *g_amivcsGuiStr;
static Object *g_okBtn, *g_cancelBtn;

/* The persisted values themselves -- not per-project, so these live
 * here rather than in mainwindow.c's g_project. Read once at
 * prefswindow_load() (before mainwindow_run() initializes the
 * toolchain/SDK registry from them), updated on every OK. */
static char g_makeProgram[64]   = "make";
static char g_sdkRoot[64]       = "SDK:";
static char g_toolchainRoot[64] = "vbcc:";
static char g_amivcsProgram[128] = "amivcs";
static char g_amivcsGuiProgram[128] = "amivcs-gui";

static void load_var(const char *name, char *out, size_t outsz, const char *fallback)
{
    LONG len = GetVar((CONST_STRPTR)name, (STRPTR)out, (LONG)outsz - 1, GVF_GLOBAL_ONLY);
    if (len < 0) {
        strncpy(out, fallback, outsz - 1);
        out[outsz - 1] = '\0';
    }
}

static void copy_str(char *dst, const char *src, size_t dstsz)
{
    strncpy(dst, src ? src : "", dstsz - 1);
    dst[dstsz - 1] = '\0';
}

static void sdk_var_name(const SdkRootRow *row, char *out, size_t outsz)
{
    copy_str(out, ENV_SDKROOT_PREFIX, outsz);
    strncat(out, row->id, outsz - strlen(out) - 1);
}

/* The built-ins' ids, names and default drawers below the current SDK
 * root -- the rows of the "SDK-Pfade" page. Keeps every row's `root`
 * (the user's own drawer) when called again for a new SDK root. */
static void sdk_rows_from_builtins(void)
{
    AideSdkList list;
    size_t i;

    aide_sdk_list_init(&list);
    aide_sdk_append_builtins(g_sdkRoot, &list);
    for (i = 0; i < list.count && i < PREFS_MAX_SDK_ROWS; ++i) {
        SdkRootRow *row = &g_sdkRows[i];
        if ((int)i >= g_sdkRowCount) {
            row->root[0] = '\0';
            row->str = NULL;
            row->browse = NULL;
        }
        copy_str(row->id, list.items[i].id, sizeof(row->id));
        copy_str(row->label, list.items[i].displayName, sizeof(row->label));
        strncat(row->label, ":", sizeof(row->label) - strlen(row->label) - 1);
        copy_str(row->deflt, list.items[i].rootDir, sizeof(row->deflt));
        copy_str(row->help, "Standard: ", sizeof(row->help));
        strncat(row->help, row->deflt, sizeof(row->help) - strlen(row->help) - 1);
    }
    if ((int)i > g_sdkRowCount)
        g_sdkRowCount = (int)i;
    aide_sdk_list_free(&list);
}

/* Whether `path` is a reachable drawer, asked without the "please
 * insert volume SDK" requester AmigaDOS would put up for a missing
 * assign -- the default "SDK:NDK_3.2" on a machine without SDK:. */
static int drawer_exists_quietly(const char *path)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldWindow = me->pr_WindowPtr;
    BPTR lock;

    me->pr_WindowPtr = (APTR)-1;
    lock = Lock((STRPTR)path, ACCESS_READ);
    me->pr_WindowPtr = oldWindow;
    if (!lock)
        return 0;
    UnLock(lock);
    return 1;
}

/* Directory-only reqtools requester, opened at `initial` if that exists. */
static int ask_drawer(const char *title, const char *initial, char *out, size_t outsz)
{
    struct rtFileRequester *filereq;
    char filename[108];
    struct TagItem tags[2];
    LONG ok;

    filereq = (struct rtFileRequester *)rtAllocRequestA(RT_FILEREQ, NULL);
    if (!filereq)
        return 0;
    if (initial && initial[0] && drawer_exists_quietly(initial)) {
        struct TagItem dirTags[2];
        dirTags[0].ti_Tag = RTFI_Dir;  dirTags[0].ti_Data = (ULONG)initial;
        dirTags[1].ti_Tag = TAG_DONE;  dirTags[1].ti_Data = 0;
        rtChangeReqAttrA(filereq, dirTags);
    }
    filename[0] = '\0';
    tags[0].ti_Tag = RTFI_Flags; tags[0].ti_Data = FREQF_NOFILES;
    tags[1].ti_Tag = TAG_DONE;   tags[1].ti_Data = 0;
    ok = rtFileRequestA(filereq, (STRPTR)filename, (CONST_STRPTR)title, tags);
    if (ok)
        copy_str(out, (char *)filereq->Dir, outsz);
    rtFreeRequest(filereq);
    return ok && out[0] ? 1 : 0;
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
    load_var(ENV_AMIVCS, g_amivcsProgram, sizeof(g_amivcsProgram), "amivcs");
    load_var(ENV_AMIVCSGUI, g_amivcsGuiProgram, sizeof(g_amivcsGuiProgram), "amivcs-gui");

    sdk_rows_from_builtins();
    {
        int i;
        for (i = 0; i < g_sdkRowCount; ++i) {
            char name[64];
            char *trimmed;
            sdk_var_name(&g_sdkRows[i], name, sizeof(name));
            load_var(name, g_sdkRows[i].root, sizeof(g_sdkRows[i].root), "");
            trimmed = aide_str_trim_dup(g_sdkRows[i].root);
            copy_str(g_sdkRows[i].root, trimmed, sizeof(g_sdkRows[i].root));
            free(trimmed);
        }
    }
}

void prefs_apply_sdk_roots(AideSdkList *registry)
{
    int i;
    for (i = 0; i < g_sdkRowCount; ++i) {
        if (g_sdkRows[i].root[0])
            aide_sdk_list_set_root(registry, g_sdkRows[i].id, g_sdkRows[i].root);
    }
}

void prefswindow_init(Object *app)
{
    Object *sdkRowsGroup;
    int i;

    g_app = app;

    g_makeStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_makeProgram), End;
    g_sdkRootStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_sdkRoot), End;
    g_toolchainRootStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_toolchainRoot), End;
    g_amivcsStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_amivcsProgram), End;
    g_amivcsGuiStr = StringObject, StringFrame, MUIA_String_MaxLen, sizeof(g_amivcsGuiProgram), End;
    g_okBtn = SimpleButton("_OK");
    g_cancelBtn = SimpleButton("_Abbrechen");

    sdkRowsGroup = ColGroup(3), End;
    for (i = 0; i < g_sdkRowCount; ++i) {
        SdkRootRow *row = &g_sdkRows[i];
        row->str = StringObject, StringFrame,
            MUIA_String_MaxLen, sizeof(row->root),
            MUIA_ShortHelp, (ULONG)row->help,
        End;
        row->browse = SimpleButton("W\344hlen...");
        DoMethod(sdkRowsGroup, OM_ADDMEMBER, Label(row->label));
        DoMethod(sdkRowsGroup, OM_ADDMEMBER, row->str);
        DoMethod(sdkRowsGroup, OM_ADDMEMBER, row->browse);
    }

    g_window = WindowObject,
        MUIA_Window_ID,    MAKE_ID('P','R','E','F'),
        MUIA_Window_Title, "AmigaIDE - Einstellungen",
        MUIA_Window_Width, MUIV_Window_Width_Screen(70), /* room for paths */
        MUIA_Window_Menu,  MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, RegisterObject,
                MUIA_Register_Titles, (ULONG)g_prefsTabTitles,
                Child, VGroup,
                    Child, ColGroup(2),
                        Child, Label("Make-Programm:"),  Child, g_makeStr,
                        Child, Label("SDK-Root:"),        Child, g_sdkRootStr,
                        Child, Label("Toolchain-Root:"),  Child, g_toolchainRootStr,
                        Child, Label("AmiVCS-Programm:"), Child, g_amivcsStr,
                        Child, Label("AmiVCS-GUI:"),      Child, g_amivcsGuiStr,
                    End,
                    Child, RectangleObject, End,
                End,
                Child, VGroup,
                    Child, TextObject,
                        MUIA_Text_Contents, (ULONG)"Eigener Ordner je SDK, z. B. Work:NDK32.\n"
                            "Leer lassen = Standard unter dem SDK-Root\n"
                            "(Hilfeblase am Feld zeigt ihn).",
                    End,
                    Child, sdkRowsGroup,
                    Child, RectangleObject, End,
                End,
            End,
            Child, HGroup,
                Child, g_okBtn,
                Child, g_cancelBtn,
            End,
        End,
    End;

    DoMethod(g_app, OM_ADDMEMBER, g_window);
    AIDE_REMEMBER_WINDOW(g_window);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_PREFS_CANCEL);
    DoMethod(g_okBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_PREFS_OK);
    DoMethod(g_cancelBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_PREFS_CANCEL);
    for (i = 0; i < g_sdkRowCount; ++i)
        DoMethod(g_sdkRows[i].browse, MUIM_Notify, MUIA_Pressed, FALSE,
                g_app, 2, MUIM_Application_ReturnID, (ULONG)(ID_PREFS_SDK_BROWSE + i));
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
    set(g_amivcsStr, MUIA_String_Contents, g_amivcsProgram);
    set(g_amivcsGuiStr, MUIA_String_Contents, g_amivcsGuiProgram);
    {
        int i;
        for (i = 0; i < g_sdkRowCount; ++i)
            set(g_sdkRows[i].str, MUIA_String_Contents, g_sdkRows[i].root);
    }
    set(g_window, MUIA_Window_Open, TRUE);
}

int prefswindow_handle_id(long id)
{
    if (id >= ID_PREFS_SDK_BROWSE && id < ID_PREFS_SDK_BROWSE + g_sdkRowCount) {
        SdkRootRow *row = &g_sdkRows[id - ID_PREFS_SDK_BROWSE];
        STRPTR v = NULL;
        char picked[PREFS_SDK_ROOT_LEN];
        char title[80];
        get(row->str, MUIA_String_Contents, &v);
        copy_str(title, row->label, sizeof(title));
        if (title[0] && title[strlen(title) - 1] == ':')
            title[strlen(title) - 1] = '\0';
        strncat(title, " - Ordner w\344hlen", sizeof(title) - strlen(title) - 1);
        if (ask_drawer(title, (v && v[0]) ? (char *)v : row->deflt, picked, sizeof(picked)))
            set(row->str, MUIA_String_Contents, picked);
        return 3; /* handled, window stays open */
    }

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
        get(g_amivcsStr, MUIA_String_Contents, &v);
        {
            /* optional tool: empty means the default "amivcs" */
            char *amivcs = aide_str_trim_dup(v ? (char *)v : "");
            strncpy(g_amivcsProgram, amivcs[0] ? amivcs : "amivcs", sizeof(g_amivcsProgram) - 1);
            g_amivcsProgram[sizeof(g_amivcsProgram) - 1] = '\0';
            free(amivcs);
        }
        get(g_amivcsGuiStr, MUIA_String_Contents, &v);
        {
            /* optional too: empty means the default "amivcs-gui" */
            char *gui = aide_str_trim_dup(v ? (char *)v : "");
            strncpy(g_amivcsGuiProgram, gui[0] ? gui : "amivcs-gui", sizeof(g_amivcsGuiProgram) - 1);
            g_amivcsGuiProgram[sizeof(g_amivcsGuiProgram) - 1] = '\0';
            free(gui);
        }

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
        SetVar((CONST_STRPTR)ENV_AMIVCS, (CONST_STRPTR)g_amivcsProgram, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
        SetVar((CONST_STRPTR)ENV_AMIVCSGUI, (CONST_STRPTR)g_amivcsGuiProgram, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);

        {
            int i;
            for (i = 0; i < g_sdkRowCount; ++i) {
                SdkRootRow *row = &g_sdkRows[i];
                char name[64];
                char *root;
                get(row->str, MUIA_String_Contents, &v);
                root = aide_str_trim_dup(v ? (char *)v : "");
                copy_str(row->root, root, sizeof(row->root));
                free(root);
                sdk_var_name(row, name, sizeof(name));
                if (row->root[0])
                    SetVar((CONST_STRPTR)name, (CONST_STRPTR)row->root, -1, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
                else
                    DeleteVar((CONST_STRPTR)name, GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
            }
        }
        /* a new SDK root moves the defaults shown in the help bubbles */
        sdk_rows_from_builtins();

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
const char *prefs_amivcs_program(void) { return g_amivcsProgram; }
const char *prefs_amivcs_gui_program(void) { return g_amivcsGuiProgram; }
