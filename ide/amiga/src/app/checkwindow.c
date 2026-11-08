#include "app/checkwindow.h"
#include "app/requirements.h"

#include <libraries/iffparse.h> /* MAKE_ID() */
#include <clib/alib_protos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "mui/mui_stubs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_CHECK_RECHECK 3201
#define ID_CHECK_CLOSE   3202

#define CHECK_PATH_LEN 256
#define CHECK_TMP      "T:aide-check.tmp"

static Object *g_app;
static Object *g_window;
static CheckContext g_ctx;
static int g_haveCtx;
static int g_problems;
/* Every string handed to a Text object lives here until the window is
 * rebuilt or freed, so it never matters whether MUI copies it. */
static AideStrList g_texts;

/* ---- small helpers ---- */

static const char *keep(const char *s)
{
    aide_strlist_push(&g_texts, s ? s : "");
    return g_texts.items[g_texts.count - 1];
}

static Object *cell(const char *text)
{
    return TextObject, MUIA_Text_Contents, (ULONG)keep(text), End;
}

static void add_row(Object *group, const char *name, const char *state,
                    const char *version, const char *where)
{
    DoMethod(group, OM_ADDMEMBER, cell(name));
    DoMethod(group, OM_ADDMEMBER, cell(state));
    DoMethod(group, OM_ADDMEMBER, cell(version));
    DoMethod(group, OM_ADDMEMBER, cell(where));
}

static void add_heading(Object *group, const char *title)
{
    char buf[80];
    sprintf(buf, "\033b%s", title);
    DoMethod(group, OM_ADDMEMBER, cell(""));
    DoMethod(group, OM_ADDMEMBER, cell(""));
    DoMethod(group, OM_ADDMEMBER, cell(""));
    DoMethod(group, OM_ADDMEMBER, cell(""));
    add_row(group, buf, "", "", "");
}

/* State texts. A problem is bold and counted; an optional item that is
 * missing is neither. */
static const char *state_ok(void)        { return "vorhanden"; }
static const char *state_missing(int optional)
{
    if (optional)
        return "fehlt (optional)";
    ++g_problems;
    return "\033bfehlt";
}

/* Lock without "please insert volume" -- a missing assign such as SDK:
 * or ACE: is an answer here, not a reason to ask. */
static BPTR quiet_lock(const char *path)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    BPTR lock;
    me->pr_WindowPtr = (APTR)-1;
    lock = Lock((STRPTR)path, ACCESS_READ);
    me->pr_WindowPtr = old;
    return lock;
}

/* 1 if `path` exists; `full` gets its full name, *isDir its kind. */
static int locate(const char *path, char *full, size_t fullsz, int *isDir)
{
    BPTR lock;
    struct FileInfoBlock *fib;

    full[0] = '\0';
    if (isDir) *isDir = 0;
    if (!path || !path[0])
        return 0;
    lock = quiet_lock(path);
    if (!lock)
        return 0;
    if (!NameFromLock(lock, (STRPTR)full, (LONG)fullsz))
        strncpy(full, path, fullsz - 1), full[fullsz - 1] = '\0';
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (fib) {
        if (Examine(lock, fib) && isDir)
            *isDir = fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return 1;
}

static void join(const char *dir, const char *rel, char *out, size_t outsz)
{
    strncpy(out, dir, outsz - 1);
    out[outsz - 1] = '\0';
    AddPart((STRPTR)out, (STRPTR)rel, (ULONG)outsz);
}

/* Looks for `pattern` in the file and copies what follows it (up to the
 * end of that line or string) into `out`; with keepPattern the pattern
 * itself starts the result. Reads in chunks, so a 600 KB vbccm68k costs
 * no more memory than a small tool. */
static int scan_file(const char *path, const char *pattern, int keepPattern, char *out, size_t outsz)
{
    static char buf[4096 + 128];
    size_t plen = strlen(pattern);
    size_t keepLen = 0;
    BPTR fh;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    int found = 0;

    out[0] = '\0';
    me->pr_WindowPtr = (APTR)-1;
    fh = Open((STRPTR)path, MODE_OLDFILE);
    me->pr_WindowPtr = old;
    if (!fh)
        return 0;

    for (;;) {
        LONG n = Read(fh, buf + keepLen, 4096);
        size_t len, i, start;
        int eof;
        if (n < 0) n = 0;
        len = keepLen + (size_t)n;
        eof = (n == 0);
        start = len > 120 ? len - 120 : 0;
        for (i = 0; i + plen <= len; ++i) {
            if (buf[i] == pattern[0] && memcmp(buf + i, pattern, plen) == 0) {
                if (!eof && len - i < 120) {
                    start = i; /* may be cut off: read on, keep it */
                    break;
                }
                {
                    size_t j = keepPattern ? i : i + plen, k = 0;
                    while (j < len && buf[j] == ' ') ++j;
                    while (j < len && k + 1 < outsz && (unsigned char)buf[j] >= 32)
                        out[k++] = buf[j++];
                    out[k] = '\0';
                }
                found = 1;
                break;
            }
        }
        if (found || eof)
            break;
        memmove(buf, buf + start, len - start);
        keepLen = len - start;
    }
    Close(fh);
    return found && out[0];
}

/* The version a program or library file announces: its $VER string, or
 * else the text after `fallback` (vc has no $VER, only "vc frontend for
 * vbcc (c) in 1995-2020 ..."). "" if neither is there. */
static void file_version(const char *path, const char *fallback, char *out, size_t outsz)
{
    if (scan_file(path, "$VER:", 0, out, outsz))
        return;
    if (fallback && scan_file(path, fallback, 1, out, outsz)) {
        char *c = strstr(out, " (c)");
        if (c) *c = '\0';
        return;
    }
    out[0] = '\0';
}

/* Where the shell finds `prog`: a name with ':' or '/' as it is, a bare
 * name through the Which command -- the same search the build's shell
 * does. 1 with the full name in `out`. */
static int find_program(const char *prog, char *out, size_t outsz)
{
    char cmd[CHECK_PATH_LEN + 64];
    struct TagItem tags[5];
    BPTR nilIn, nilOut, fh;
    char *nl;

    out[0] = '\0';
    if (!prog || !prog[0])
        return 0;
    if (strchr(prog, ':') || strchr(prog, '/'))
        return locate(prog, out, outsz, NULL);

    sprintf(cmd, "Which >" CHECK_TMP " \"%.200s\"", prog);
    nilIn  = Open((STRPTR)"NIL:", MODE_OLDFILE);
    nilOut = Open((STRPTR)"NIL:", MODE_NEWFILE);
    tags[0].ti_Tag = SYS_Input;    tags[0].ti_Data = (ULONG)nilIn;
    tags[1].ti_Tag = SYS_Output;   tags[1].ti_Data = (ULONG)nilOut;
    tags[2].ti_Tag = SYS_Asynch;   tags[2].ti_Data = FALSE;
    tags[3].ti_Tag = NP_WindowPtr; tags[3].ti_Data = (ULONG)-1;
    tags[4].ti_Tag = TAG_DONE;     tags[4].ti_Data = 0;
    SystemTagList((STRPTR)cmd, tags);
    if (nilIn)  Close(nilIn);
    if (nilOut) Close(nilOut);

    fh = Open((STRPTR)CHECK_TMP, MODE_OLDFILE);
    if (fh) {
        LONG n = Read(fh, out, (LONG)outsz - 1);
        out[n > 0 ? n : 0] = '\0';
        Close(fh);
    }
    DeleteFile((STRPTR)CHECK_TMP);
    nl = strpbrk(out, "\r\n");
    if (nl) *nl = '\0';
    /* Which may print something other than a path; only an existing
     * file counts. locate() writes its result first, so not in place. */
    {
        char found[CHECK_PATH_LEN];
        strncpy(found, out, sizeof(found) - 1);
        found[sizeof(found) - 1] = '\0';
        if (!found[0] || !locate(found, out, outsz, NULL)) {
            out[0] = '\0';
            return 0;
        }
    }
    return 1;
}

/* ---- the sections ---- */

static void check_drawer(Object *g, const char *name, const char *path, int optional, const char *help)
{
    char full[CHECK_PATH_LEN];
    int isDir;
    if (locate(path, full, sizeof(full), &isDir) && isDir)
        add_row(g, name, state_ok(), "", full);
    else
        add_row(g, name, state_missing(optional), "", help);
}

static void check_file(Object *g, const char *name, const char *path, const char *fallback,
                       int optional, const char *help)
{
    char full[CHECK_PATH_LEN], ver[96];
    if (locate(path, full, sizeof(full), NULL)) {
        file_version(full, fallback, ver, sizeof(ver));
        add_row(g, name, state_ok(), ver, full);
    } else {
        add_row(g, name, state_missing(optional), "", help);
    }
}

static void check_program(Object *g, const char *name, const char *prog, int optional, const char *help)
{
    char full[CHECK_PATH_LEN], ver[96];
    if (find_program(prog, full, sizeof(full))) {
        file_version(full, NULL, ver, sizeof(ver));
        add_row(g, name, state_ok(), ver, full);
    } else {
        char where[CHECK_PATH_LEN + 40];
        sprintf(where, "\"%.60s\" nicht im Pfad. %s", prog, help);
        add_row(g, name, state_missing(optional), "", where);
    }
}

static void check_library(Object *g, const char *name, const char *altPath,
                          UWORD minVersion, const char *package)
{
    struct Library *lib = OpenLibrary((STRPTR)name, 0);
    char ver[32], where[200], label[64];
    const char *slash = strrchr(name, '/');

    if (!lib && altPath)
        lib = OpenLibrary((STRPTR)altPath, 0);
    strncpy(label, slash ? slash + 1 : name, sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';
    if (!lib) {
        sprintf(where, "Aminet: %.150s", package);
        add_row(g, label, state_missing(0), "", where);
        return;
    }
    sprintf(ver, "%d.%d", (int)lib->lib_Version, (int)lib->lib_Revision);
    if (lib->lib_Version < minVersion) {
        ++g_problems;
        sprintf(where, "ab Version %d n\366tig. Aminet: %.120s", (int)minVersion, package);
        add_row(g, label, "\033bzu alt", ver, where);
    } else {
        sprintf(where, minVersion ? "ab Version %d n\366tig" : "", (int)minVersion);
        add_row(g, label, state_ok(), ver, where);
    }
    CloseLibrary(lib);
}

static int sdk_used_by_project(const AideSdkDescriptor *d)
{
    size_t i;
    if (!g_ctx.projectSdks)
        return 0;
    for (i = 0; i < g_ctx.projectSdks->count; ++i)
        if (aide_sdk_list_resolve(g_ctx.sdks, g_ctx.projectSdks->items[i]) == d)
            return 1;
    return 0;
}

static void check_sdk(Object *g, const AideSdkDescriptor *d)
{
    AideSdkCheck c;
    char name[96], where[CHECK_PATH_LEN + 80];
    const char *state;
    int used = sdk_used_by_project(d);

    sprintf(name, "%.70s%s", d->displayName ? d->displayName : d->id, used ? " (Projekt)" : "");
    aide_sdk_check_init(&c);
    aide_sdk_check(d, &c);
    if (c.missing == 0) {
        state = state_ok();
        sprintf(where, "%.200s", d->rootDir);
    } else if (c.count > 0 && !c.items[0].present && c.items[0].kind == AIDE_SDK_PART_ROOT) {
        state = used ? state_missing(0) : "nicht installiert";
        if (c.items[0].similar)
            sprintf(where, "%.100s fehlt, vorhanden ist %.100s (umbenennen oder unter Einstellungen > SDK-Pfade eintragen)",
                    d->rootDir, c.items[0].similar);
        else
            sprintf(where, "erwartet in %.200s", d->rootDir);
    } else {
        size_t i;
        if (used) ++g_problems;
        state = used ? "\033bunvollst\344ndig" : "unvollst\344ndig";
        sprintf(where, "%lu von %lu Teilen fehlen, z.B. ", (unsigned long)c.missing, (unsigned long)c.count);
        for (i = 0; i < c.count; ++i) {
            if (!c.items[i].present) {
                strncat(where, c.items[i].path, sizeof(where) - strlen(where) - 1);
                break;
            }
        }
    }
    add_row(g, name, state, d->version && d->version[0] ? d->version : "", where);
    aide_sdk_check_free(&c);
}

/* Fills `g` (a 4-column group) with every row; returns the number of
 * problems found. */
static void fill(Object *g)
{
    char path[CHECK_PATH_LEN];
    const char *tc = g_ctx.toolchainRoot;
    const char *vbccHelp = "Aminet: dev/c/vbcc_bin_amigaos68k.lha";
    size_t i;

    g_problems = 0;
    add_row(g, "\033bName", "\033bZustand", "\033bVersion", "\033bOrt / Abhilfe");

    add_heading(g, "Ordner");
    check_drawer(g, "Toolchain (vbcc:)", tc, 0,
                 "vbcc installieren und Assign vbcc: setzen, oder Einstellungen > Toolchain-Root");
    join(tc, "targets/m68k-amigaos", path, sizeof(path));
    check_drawer(g, "vbcc-Target m68k-amigaos", path, 0,
                 "vbcc_target_m68k-amigaos.lha (sun.hasenbraten.de/vbcc)");
    check_drawer(g, "SDK-Root", g_ctx.sdkRoot, 1,
                 "Assign SDK: setzen oder eigene Ordner unter Einstellungen > SDK-Pfade");
    check_drawer(g, "ACE BASIC (ACE:)", "ACE:", 1,
                 "nur f\374r BASIC-Projekte. Aminet: dev/basic/ace-basic.lha");

    add_heading(g, "Compiler und Werkzeuge");
    join(tc, "bin/vc", path, sizeof(path));
    check_file(g, "vc", path, "vc frontend for vbcc", 0, vbccHelp);
    join(tc, "bin/vbccm68k", path, sizeof(path));
    check_file(g, "vbccm68k", path, NULL, 0, vbccHelp);
    join(tc, "bin/vasmm68k_mot", path, sizeof(path));
    check_file(g, "vasmm68k_mot", path, NULL, 0, vbccHelp);
    join(tc, "bin/vlink", path, sizeof(path));
    check_file(g, "vlink", path, NULL, 0, vbccHelp);
    join(tc, "targets/m68k-amigaos/lib/startup.o", path, sizeof(path));
    check_file(g, "startup.o (Target)", path, NULL, 0,
               "vbcc_target_m68k-amigaos.lha (sun.hasenbraten.de/vbcc)");
    check_program(g, "make", g_ctx.makeProgram, 0,
                  "Aminet: dev/c/make_bin.lha oder dev/c/make-4.4.1.lha, make nach C:");
    check_file(g, "ace", "ACE:bin/ace", NULL, 1, "nur f\374r BASIC-Projekte. Aminet: dev/basic/ace-basic.lha");
    check_program(g, "amivcs", g_ctx.amivcsProgram, 1, "Aminet: dev/misc/AmiVCS.lha");
    check_program(g, "amivcs-gui", g_ctx.amivcsGuiProgram, 1, "Aminet: dev/misc/AmiVCS.lha");

    add_heading(g, "Libraries und MUI-Klassen");
    for (i = 0; i < kAideLibReqCount; ++i)
        check_library(g, kAideLibReqs[i].name, kAideLibReqs[i].altPath,
                      kAideLibReqs[i].minVersion, kAideLibReqs[i].package);
    check_library(g, "asl.library", NULL, 38, "Teil von AmigaOS (Neues Projekt)");

    add_heading(g, "SDKs");
    if (g_ctx.sdks) {
        AideStrList missing;
        for (i = 0; i < g_ctx.sdks->count; ++i)
            check_sdk(g, &g_ctx.sdks->items[i]);
        if (g_ctx.projectSdks) {
            aide_strlist_init(&missing);
            aide_sdk_list_missing(g_ctx.sdks, g_ctx.projectSdks, &missing);
            for (i = 0; i < missing.count; ++i)
                add_row(g, missing.items[i], state_missing(0), "",
                        "vom Projekt verlangt, unbekannt: SDK mit AmiDevSDK.json unter SDK-Root ablegen");
            aide_strlist_free(&missing);
        }
    }
}

/* ---- window ---- */

static void dispose_window(void)
{
    if (!g_window)
        return;
    set(g_window, MUIA_Window_Open, FALSE);
    DoMethod(g_app, OM_REMMEMBER, g_window);
    MUI_DisposeObject(g_window);
    g_window = NULL;
    aide_strlist_clear(&g_texts);
}

static void build_and_open(void)
{
    Object *table, *summary, *recheck, *close;
    char text[160];

    dispose_window();

    table = VirtgroupObject, MUIA_Group_Columns, 4, MUIA_Group_HorizSpacing, 8, End;
    fill(table);
    if (g_problems == 0)
        strcpy(text, "Alles N\366tige ist vorhanden.");
    else
        sprintf(text, "\033b%d Punkt%s fehl%s oder %s zu alt.\033n Abhilfe steht rechts.",
                g_problems, g_problems == 1 ? "" : "e", g_problems == 1 ? "t" : "en",
                g_problems == 1 ? "ist" : "sind");
    summary = TextObject, MUIA_Text_Contents, (ULONG)keep(text), End;
    recheck = SimpleButton("_Erneut pr\374fen");
    close = SimpleButton("_Schlie\337en");

    g_window = WindowObject,
        MUIA_Window_ID,     MAKE_ID('C','H','E','K'),
        MUIA_Window_Title,  "AmigaIDE - Installation pr\374fen",
        MUIA_Window_Width,  MUIV_Window_Width_Screen(80),
        MUIA_Window_Height, MUIV_Window_Height_Screen(70),
        MUIA_Window_Menu,   MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, summary,
            Child, ScrollgroupObject,
                MUIA_Scrollgroup_Contents, table,
            End,
            Child, HGroup,
                Child, recheck,
                Child, close,
            End,
        End,
    End;
    if (!g_window)
        return;

    DoMethod(g_app, OM_ADDMEMBER, g_window);
    AIDE_REMEMBER_WINDOW(g_window);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             g_app, 2, MUIM_Application_ReturnID, ID_CHECK_CLOSE);
    DoMethod(recheck, MUIM_Notify, MUIA_Pressed, FALSE,
             g_app, 2, MUIM_Application_ReturnID, ID_CHECK_RECHECK);
    DoMethod(close, MUIM_Notify, MUIA_Pressed, FALSE,
             g_app, 2, MUIM_Application_ReturnID, ID_CHECK_CLOSE);
    set(g_window, MUIA_Window_Open, TRUE);
}

void checkwindow_init(Object *app)
{
    g_app = app;
    aide_strlist_init(&g_texts);
}

void checkwindow_free(void)
{
    /* g_window, if any, is a member of g_app and goes with it */
    g_window = NULL;
    aide_strlist_free(&g_texts);
}

void checkwindow_show(const CheckContext *ctx)
{
    g_ctx = *ctx;
    g_haveCtx = 1;
    set(g_app, MUIA_Application_Sleep, TRUE);
    build_and_open();
    set(g_app, MUIA_Application_Sleep, FALSE);
}

int checkwindow_handle_id(long id)
{
    switch (id) {
    case ID_CHECK_RECHECK:
        if (g_haveCtx) {
            set(g_app, MUIA_Application_Sleep, TRUE);
            build_and_open();
            set(g_app, MUIA_Application_Sleep, FALSE);
        }
        return 1;
    case ID_CHECK_CLOSE:
        if (g_window)
            set(g_window, MUIA_Window_Open, FALSE);
        return 1;
    default:
        return 0;
    }
}
