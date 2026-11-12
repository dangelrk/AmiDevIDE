/*
 * mainwindow.c — AmigaIDE main window, event loop and build pipeline
 * (Amiga/MUI). See mainwindow.h. Application/Window/
 * MUIM_Application_Input shape as in the MUI 3.8 SDK examples.
 *
 * v1 scope, deliberately simplified to keep this a well-defined,
 * compilable slice rather than a half-verified everything:
 *   - Project tree is a real NListtree hierarchy (4 groups -- C-Quellen/
 *     ASM-Quellen/C-Header/ASM-Include -- each with real subfolder nodes
 *     built from path components), per user feedback; see the "Project
 *     tree" section below. The Symbole panel is still flat (not grouped
 *     by file/kind).
 *   - One editor pane (no tabs yet) -- opens the file double-clicked in
 *     the tree/Symbole/Suche/Issues lists, or via go-to-definition.
 *   - No syntax highlighting yet (aide_cparser/aide_asmparser-driven
 *     escape-sequence injection, per the plan's PenPolicy) -- plain text
 *     load/save via MUIM_TextEditor_{Insert,Export}Text.
 *   - Neither this file nor its helpers guard against discarding
 *     unsaved editor changes when switching files (New/Open/go-to-
 *     definition/double-click) -- a documented, pre-existing v1 gap,
 *     not something this pass's additions changed.
 *   - The symbol index and Hothelp/autodoc index are rebuilt wholesale
 *     (every project source/header re-parsed) after project load, after
 *     first save, and after every build -- no incremental invalidation.
 *   - Build generates a Makefile (core/src/aide_makegen.c, AmigaDOS
 *     dialect) and runs it asynchronously via GNU Make -- see
 *     gnu_make_amiga (https://github.com/dmcoles/gnu_make_amiga), since
 *     stock AmigaOS 3.x doesn't ship one. do_build() shows a clear
 *     message and refuses to build if it isn't found in the path
 *     (make_is_available()), rather than failing silently or falling
 *     back to a separate direct vc/vasm/vlink codepath.
 *   - String/file input goes through reqtools.library (rtGetStringA/
 *     rtFileRequestA) instead of a hand-rolled MUI window -- see
 *     ask_string()/ask_file() and mui/reqtools_stubs.h.
 *   - No toolbar -- every action lives in the (right-mouse-button) menu
 *     only, per user feedback that the button row was redundant with it.
 *   - "Speichern" is one unified action: saves the open editor file (if
 *     any) and the project, asking for a location only the first time
 *     (see do_save()) -- there is no separate "save file" command.
 *   - set_status() is called before every operation slow enough to be
 *     worth a status-bar message (project load/save, indexing, search,
 *     build), so the user sees that something is happening rather than
 *     a frozen window -- per user feedback.
 *
 * UNVERIFIED beyond compiling clean with vbcc against the real toolchain
 * (amiga/Makefile.amiga-cross) -- no FS-UAE/real-Amiga interactive test
 * rig in this environment. That includes the MUIA_Window_InputEvent
 * qualifier strings ("-shift f1" etc.) for the Shift+F-key shortcuts --
 * the plain "-fN" forms are known to work.
 */
#include <libraries/mui.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h> /* MAKE_ID() */
#include <libraries/reqtools.h>
#include <libraries/asl.h>
#include <inline/asl_protos.h> /* AslBase is local to ask_new_project_dir() */
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <exec/types.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include "mui/mui_stubs.h"
#include "mui/reqtools_stubs.h"
#include "mui/NList_mcc.h"
#include "mui/NListview_mcc.h"
/* NListtree_mcc.h's own macros (MUIV_NListtree_Insert_ListNode_Root etc.)
 * use IPTR unconditionally -- an OS4/MorphOS-era "integer sized like a
 * pointer" type that NDK 3.2 (this is an OS3.x/68k build) simply doesn't
 * define. On 68k a pointer already is 32 bits, i.e. exactly ULONG. */
#ifndef IPTR
typedef ULONG IPTR;
#endif
#include "mui/NListtree_mcc.h"
#include "mui/TextEditor_mcc.h"
#include "app/mainwindow.h"

#include "index/symbolindex.h"
#include "help/helpwindow.h"
#include "disasm/disasmwindow.h"
#include "project/settingswindow.h"
#include "app/prefswindow.h"
#include "app/checkwindow.h"
#include "app/splash.h"

/* Build stamp, generated into $(OBJDIR) by amiga/Makefile.amiga-cross --
** see the comment there for why it is a header and not a -D define. */
#include "AmigaIDEBuildStamp.h"
#include "aide/aide_version.h"
#include "aide/aide_project.h"
#include "aide/aide_sdk.h"
#include "aide/aide_toolchain.h"
#include "aide/aide_vccfg.h"
#include "aide/aide_makegen.h"
#include "aide/aide_diag.h"
#include "aide/aide_fs.h"
#include "aide/aide_logtail.h"
#include "aide/aide_str.h"
#include "aide/aide_symbol.h"
#include "aide/aide_search.h"
#include "aide/aide_hunk.h"
#include "aide/aide_includegraph.h"
#include "aide/aide_icon.h"
#include "aide/aide_reindent.h"
#include "vcs/vcspanel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* IDs dispatched by the main MUIM_Application_Input loop                */
/* ------------------------------------------------------------------ */
#define ID_QUIT          1
#define ID_NEW_PROJECT   2
#define ID_OPEN_PROJECT  3
#define ID_SAVE          4 /* saves the open file (if any) + the project */
/* "Neue Datei" is a submenu (5 types), not a single item that then asks a
 * question in a requester -- see MenuData[]. */
#define ID_ADD_NEW_C_SOURCE    5
#define ID_ADD_NEW_C_HEADER    26
#define ID_ADD_NEW_ASM_SOURCE  27
#define ID_ADD_NEW_ASM_INCLUDE 28
#define ID_ADD_NEW_BASIC_SOURCE 34
#define ID_BUILD         6
#define ID_RUN           7
#define ID_ABOUT         8
#define ID_TREE_DCLICK   9
#define ID_ISSUES_DCLICK 10
#define ID_SYMBOL_DCLICK 11
#define ID_SEARCH_DCLICK 12
#define ID_HELP_F1        13 /* Hothelp for the word under the cursor */
#define ID_HELP_SHIFT_F1  14 /* Struct-Layout for the word under the cursor */
#define ID_HELP_F2        15 /* Autodoc wildcard search */
#define ID_GOTO_DEF       16 /* F3 */
#define ID_FIND_REFS      17 /* Shift+F3 */
#define ID_ISSUE_NEXT     18 /* F4 */
#define ID_ISSUE_PREV     19 /* Shift+F4 */
#define ID_FIND_IN_PROJECT 20
#define ID_DISASSEMBLE     21
#define ID_ADD_EXISTING_FILE 22
#define ID_PROJECT_SETTINGS  23
#define ID_SAVE_OUTPUT        24
#define ID_EDITOR_DIRTY       25 /* fires once when the editor content transitions clean -> dirty */
#define ID_FORMAT             29
#define ID_CLEAN              30
#define ID_BUILD_STOP         31
#define ID_PREFS              32
#define ID_REMOVE_FILE        33
#define ID_CHECK_SDKS         35
#define ID_VCS_GUI            36

#define AIDE_MAX_PATH 256

extern const char amiga_ver[]; /* main.c */

static Object *g_app;
static Object *g_window;
static Object *g_editor;
static Object *g_treeList, *g_treeListview;
static Object *g_symbolsList, *g_symbolsListview;
static Object *g_issuesList, *g_issuesListview;
static Object *g_searchList, *g_searchListview;
static Object *g_memmapList, *g_memmapListview;
static Object *g_outputEditor;
static Object *g_statusText;
static Object *g_cursorPosText; /* "Zeile n, Spalte n" -- see main loop, updated every iteration */
static Object *g_bottomRegister;
static Object *g_menustrip; /* hoisted out of the inline WindowObject construction so
                             * set_build_menus_enabled() can look items up in it by ID
                             * (MUIM_FindUData) while a build is running/not running --
                             * see mainwindow_run(). */

static AideProject g_project;
static char g_projectFile[AIDE_MAX_PATH];  /* "" if the project has never been saved */
static char g_projectRoot[AIDE_MAX_PATH];  /* directory containing g_projectFile, "" if none */
static char g_currentFile[AIDE_MAX_PATH];  /* absolute path of the file in g_editor, "" if none */
static int g_projectDirty;  /* project data (sources/headers/settings) changed since last save */
/* Set by do_new_project(): "Neu" asks for a directory, then opens
 * Project Settings (name/binary/compiler/linker/SDKs all live there --
 * no separate name prompt) before anything hits disk. OK validates
 * (settingswindow_handle_id() itself refuses to close on an empty
 * name/binary) and creates+saves the project from g_newProjectDir plus
 * whatever name was entered; Cancel creates nothing at all. Handled
 * where settingswindow_handle_id() is polled in the main loop. */
static int g_newProjectAwaitingSave;
static char g_newProjectDir[AIDE_MAX_PATH];
/* g_newProjectDir was made by ask_new_project_dir() just now: Cancel in
 * the settings removes it again (only if still empty -- DeleteFile()
 * refuses a drawer with anything in it). */
static int g_newProjectDirCreated;
#define AIDE_WINDOW_TITLE_BASE "AmigaIDE [Build " __DATE__ " " __TIME__ "]"

static AideToolchainConfig g_toolchain;
static AideSdkList g_sdkRegistry;

static AideSymbolList g_symbols;
static AideSearchMatchList g_searchMatches;

/* MUIA_Register_Titles only stores the pointer it's given -- these MUST
 * be static, never a compound literal built inline inside
 * mainwindow_run() (that "worked" only because mainwindow_run() never
 * returns until the app quits, so its stack frame happened to stay
 * alive; still undefined behavior, and the exact bug that corrupted
 * settingswindow.c's tab titles/body text, since that window is built
 * once at startup but shown much later, well after its building
 * function had already returned and its stack frame was long since
 * reused). */
static const char *g_leftRegisterTitles[] = { "Projekt", "Symbole", "Versionen", NULL };
static const char *g_bottomRegisterTitles[] = { "Ausgabe", "Issues", "Suche", "Memory Map", "Verlauf", NULL };

/* Parallel to the Issues NList's rows (row i <-> g_issues[i]), so F4/
 * Shift+F4 and double-click can jump to an issue's file:line -- the
 * NList itself only ever holds display strings, see push_issue(). */
static AideIssue *g_issues;
static size_t g_issueCount, g_issueCap;
static long g_issueActive = -1;

/* ------------------------------------------------------------------ */
/* Requesters (reqtools.library) -- see mui/reqtools_stubs.h.            */
/* ------------------------------------------------------------------ */

/* Returns 1 + *out filled if OK, 0 if cancelled. */
static int ask_string(const char *title, const char *initial, char *out, size_t outsz)
{
    char buffer[256];
    LONG ok;

    strncpy(buffer, initial ? initial : "", sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    ok = rtGetStringA((STRPTR)buffer, (LONG)(sizeof(buffer) - 1), (CONST_STRPTR)title, NULL, NULL);
    if (!ok)
        return 0;

    strncpy(out, buffer, outsz - 1);
    out[outsz - 1] = '\0';
    return 1;
}

/* Returns 1 + *out filled with "dir/filename" if OK, 0 if cancelled.
 * Doesn't set an initial directory -- reqtools.library remembers the
 * last-used directory across its own invocations on its own. */
/* `initialDir` may be NULL/"" to just let reqtools remember its own
 * last-used directory (its normal default behavior); otherwise the
 * requester opens there via rtChangeReqAttrA()+RTFI_Dir, which -- unlike
 * RTFI_Flags etc. -- is documented as a rtChangeReqAttrA-only tag, not
 * something rtFileRequestA's own taglist accepts directly. */
static int ask_file(const char *title, int forSave, const char *initialDir, char *out, size_t outsz)
{
    struct rtFileRequester *filereq;
    char filename[108];
    struct TagItem tags[2];
    LONG ok;

    filereq = (struct rtFileRequester *)rtAllocRequestA(RT_FILEREQ, NULL);
    if (!filereq)
        return 0;

    if (initialDir && initialDir[0]) {
        struct TagItem dirTags[2];
        dirTags[0].ti_Tag = RTFI_Dir;  dirTags[0].ti_Data = (ULONG)initialDir;
        dirTags[1].ti_Tag = TAG_DONE;  dirTags[1].ti_Data = 0;
        rtChangeReqAttrA(filereq, dirTags);
    }

    filename[0] = '\0';
    tags[0].ti_Tag = RTFI_Flags; tags[0].ti_Data = forSave ? FREQF_SAVE : 0;
    tags[1].ti_Tag = TAG_DONE;   tags[1].ti_Data = 0;

    ok = rtFileRequestA(filereq, (STRPTR)filename, (CONST_STRPTR)title, tags);
    if (ok) {
        /* AddPart(), not a hand-rolled "%s/%s": a plain slash-join breaks
         * for a volume root ("RAM:" + "/" + "test" -> "RAM:/test", which
         * AmigaDOS rejects) -- AddPart() knows a trailing ':' needs no
         * separator. */
        strncpy(out, (char *)filereq->Dir, outsz - 1);
        out[outsz - 1] = '\0';
        AddPart((STRPTR)out, (STRPTR)filename, (ULONG)outsz);
    }

    rtFreeRequest(filereq);
    return ok ? 1 : 0;
}

/* Directory-only file requester (FREQF_NOFILES) -- used to pick a new
 * project's location. */
static int ask_directory(const char *title, char *out, size_t outsz)
{
    struct rtFileRequester *filereq;
    char filename[108];
    struct TagItem tags[2];
    LONG ok;

    filereq = (struct rtFileRequester *)rtAllocRequestA(RT_FILEREQ, NULL);
    if (!filereq)
        return 0;

    filename[0] = '\0';
    tags[0].ti_Tag = RTFI_Flags; tags[0].ti_Data = FREQF_NOFILES;
    tags[1].ti_Tag = TAG_DONE;   tags[1].ti_Data = 0;

    ok = rtFileRequestA(filereq, (STRPTR)filename, (CONST_STRPTR)title, tags);
    if (ok) {
        strncpy(out, (char *)filereq->Dir, outsz - 1);
        out[outsz - 1] = '\0';
        /* an empty drawer is reqtools' way of saying "the current
         * directory" -- callers need a real path (a clone's parent, a
         * new project's root), so ask DOS for its full name */
        if (!out[0]) {
            BPTR lock = Lock((STRPTR)"", ACCESS_READ);
            if (!lock || !NameFromLock(lock, (STRPTR)out, (LONG)outsz)) out[0] = '\0';
            if (lock) UnLock(lock);
            if (!out[0]) ok = 0;
        }
    }

    rtFreeRequest(filereq);
    return ok ? 1 : 0;
}

/* A new project's directory: an existing drawer, or a new one. The
 * system's ASL requester, drawers only, in save mode: it hands back a
 * drawer typed into its field even if that does not exist yet (created
 * here), and offers its own context menu with "new drawer". reqtools
 * cannot do this -- in drawer mode it answers a missing drawer with
 * "Falsche Schublade!", in save mode it reports an empty name as Cancel.
 *
 * *created tells whether the drawer is new, made by the requester or
 * here, so Cancel in the project settings can remove it again: it is
 * empty and dated after the requester opened. Returns 1 with `out` set,
 * 0 if cancelled or failed (and said so). */
static int ask_new_project_dir(char *out, size_t outsz, int *created)
{
    struct Library *AslBase;
    struct FileRequester *fr;
    struct TagItem tags[6];
    struct Window *win = NULL;
    struct DateStamp before;
    BOOL ok = FALSE;
    BPTR lock;

    *created = 0;
    out[0] = '\0';
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    if (!AslBase) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "asl.library fehlt.");
        return 0;
    }
    get(g_window, MUIA_Window_Window, &win);

    tags[0].ti_Tag = ASLFR_TitleText;    tags[0].ti_Data = (ULONG)"Neues Projekt: Ordner";
    tags[1].ti_Tag = ASLFR_DrawersOnly;  tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = ASLFR_DoSaveMode;   tags[2].ti_Data = TRUE;
    tags[3].ti_Tag = ASLFR_Window;       tags[3].ti_Data = (ULONG)win;
    tags[4].ti_Tag = ASLFR_SleepWindow;  tags[4].ti_Data = win ? TRUE : FALSE;
    tags[5].ti_Tag = TAG_DONE;           tags[5].ti_Data = 0;

    DateStamp(&before);
    fr = (struct FileRequester *)AllocAslRequest(ASL_FileRequest, tags);
    if (fr) {
        ok = AslRequest(fr, NULL);
        if (ok && fr->fr_Drawer) {
            strncpy(out, (char *)fr->fr_Drawer, outsz - 1);
            out[outsz - 1] = '\0';
        }
        FreeAslRequest(fr);
    }
    CloseLibrary(AslBase);
    if (!ok)
        return 0;
    if (!out[0]) {
        /* empty drawer = the current directory, by its full name */
        BPTR cur = Lock((STRPTR)"", ACCESS_READ);
        if (!cur || !NameFromLock(cur, (STRPTR)out, (LONG)outsz)) out[0] = '\0';
        if (cur) UnLock(cur);
        if (!out[0])
            return 0;
    }

    lock = Lock((STRPTR)out, ACCESS_READ);
    if (lock) {
        struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        int isDir = 0;
        if (fib && Examine(lock, fib)) {
            isDir = fib->fib_DirEntryType > 0;
            /* new = dated after the requester opened (CompareDates() <= 0:
             * the first date is not the earlier one) and still empty */
            if (isDir && CompareDates(&fib->fib_Date, &before) <= 0)
                *created = !ExNext(lock, fib);
        }
        if (fib) FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        if (!isDir) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "%s\nist eine Datei, kein Ordner.", (ULONG)out);
            *created = 0;
            return 0;
        }
        return 1;
    }

    lock = CreateDir((STRPTR)out);
    if (!lock) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Ordner\n%s\nlie\337 sich nicht anlegen.", (ULONG)out);
        return 0;
    }
    UnLock(lock);
    *created = 1;
    return 1;
}

static void set_status(const char *text)
{
    if (g_statusText)
        set(g_statusText, MUIA_Text_Contents, text);
}

/* ------------------------------------------------------------------ */
/* Path helpers                                                          */
/* ------------------------------------------------------------------ */
/* AddPart(), not a hand-rolled "root/relative": a plain slash-join breaks
 * for a project stored directly at a volume root ("RAM:" + "/" + "x" ->
 * "RAM:/x", which AmigaDOS rejects) -- AddPart() knows a trailing ':'
 * needs no separator. See also ask_file(). */
static char *join_path(const char *root, const char *relative)
{
    char buf[AIDE_MAX_PATH];
    if (relative[0] == '/')
        return aide_strdup(relative);
    strncpy(buf, root, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    AddPart((STRPTR)buf, (STRPTR)relative, (ULONG)sizeof(buf));
    return aide_strdup(buf);
}

/* join_path() into a fixed-size caller buffer, for the many places that
 * build a plain local char[AIDE_MAX_PATH] rather than wanting a malloc'd
 * result (objDir/binDir/targetPath/... in do_build() and do_run()). */
static void join_path_into(char *out, size_t outsz, const char *root, const char *relative)
{
    char *joined = join_path(root, relative);
    strncpy(out, joined, outsz - 1);
    out[outsz - 1] = '\0';
    free(joined);
}

/* Fills *out with `absPath` relativized against `root` if it's actually
 * inside root, or with absPath unchanged otherwise (mirrors
 * Project::absolutePath()'s "already-absolute vs. relative" duality, see
 * resolve_tree_row() -- the tree/sources/headers lists accept either). */
static void relativize(const char *root, const char *absPath, char *out, size_t outsz)
{
    size_t rootLen = strlen(root);
    /* AddPart() -- what join_path() uses to build absPath in the first
     * place -- does NOT insert a '/' when `root` already ends in ':' or
     * '/' (an AmigaDOS volume/assign root, e.g. "RAM Disk:"); it only
     * inserts one when root is a plain directory name (e.g.
     * "Work:MyProject"). Missing this rule here meant a project living
     * directly at a volume root (no subdirectory) never matched the old
     * "must be followed by '/'" check below, silently falling through
     * to "absPath unchanged" -- storing an absolute path
     * ("RAM Disk:main.c") as if it were already project-relative,
     * confirmed via a real screenshot: the tree showed "RAM Disk:main.c"
     * as a source, and the space in the volume name then tripped
     * do_build()'s (correct, unrelated) space-in-filename guard. */
    int rootHasSep = rootLen > 0 && (root[rootLen - 1] == '/' || root[rootLen - 1] == ':');
    size_t skip = rootLen + (rootHasSep ? 0 : 1);

    if (strncmp(absPath, root, rootLen) == 0 &&
        (rootHasSep || absPath[rootLen] == '/')) {
        strncpy(out, absPath + skip, outsz - 1);
    } else {
        strncpy(out, absPath, outsz - 1);
    }
    out[outsz - 1] = '\0';
}

static void ensure_parent_dir(const char *absPath)
{
    const char *slash = strrchr(absPath, '/');
    if (slash) {
        char dir[AIDE_MAX_PATH];
        size_t len = (size_t)(slash - absPath);
        if (len >= sizeof(dir)) len = sizeof(dir) - 1;
        memcpy(dir, absPath, len);
        dir[len] = '\0';
        aide_fs_mkpath(dir);
    }
}

/* Minimal starter content for a brand-new project's main source, written
 * at first-save time (see ensure_project_source_files_exist()) -- until
 * then aide_project_create_minimal_c() only lists "src/main.c" in
 * g_project.sources without the file existing on disk. Verbatim from
 * qt/project/NewProjectWizard.cpp's kMinimalCSource/kMinimalAsmSource,
 * so a project created on either side starts from the same template. */
static const char kMinimalCSource[] =
    "#include <exec/types.h>\n"
    "#include <stdio.h>\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"Hello, Amiga!\\n\");\n"
    "    return 0;\n"
    "}\n";

static const char kMinimalAsmSource[] =
    "; Minimal minstart.o CLI program: prints a message via dos.library and exits.\n"
    "\tinclude\texec/types.i\n"
    "\tinclude\tdos/dos.i\n"
    "\txdef\t_main\n"
    "\txref\t_DOSBase\n"
    "\n"
    "_main:\n"
    "\tmovem.l\td2-d7/a2-a6,-(sp)\n"
    "\n"
    "\tmove.l\t_DOSBase,a6\n"
    "\tlea\tmessage(pc),a0\n"
    "\tmoveq\t#msgEnd-message,d1\n"
    "\tmove.l\ta0,d2\n"
    "\tjsr\t_LVOWrite(a6)\n"
    "\n"
    "\tmovem.l\t(sp)+,d2-d7/a2-a6\n"
    "\tmoveq\t#0,d0\n"
    "\trts\n"
    "\n"
    "message:\n"
    "\tdc.b\t\"Hello, Amiga!\", 10\n"
    "msgEnd:\n"
    "\teven\n";

static const char kMinimalBasicSource[] =
    "REM Minimal ACE BASIC program.\n"
    "DEFINT a-z\n"
    "\n"
    "PRINT \"Hello, Amiga!\"\n";

static void write_template_source_if_missing(const char *absPath)
{
    const char *dot;
    const char *content;
    FILE *f;

    if (aide_fs_exists(absPath))
        return;

    dot = strrchr(absPath, '.');
    if (dot && strcmp(dot, ".s") == 0)
        content = kMinimalAsmSource;
    else if (dot && (aide_str_ieq(dot, ".b") || aide_str_ieq(dot, ".bas")))
        content = kMinimalBasicSource;
    else
        content = kMinimalCSource;

    ensure_parent_dir(absPath);
    f = fopen(absPath, "wb");
    if (f) {
        fwrite(content, 1, strlen(content), f);
        fclose(f);
    }
}

static void ensure_project_source_files_exist(void)
{
    size_t i;
    if (!g_projectRoot[0])
        return;
    for (i = 0; i < g_project.sources.count; ++i) {
        char *abs = join_path(g_projectRoot, g_project.sources.items[i]);
        write_template_source_if_missing(abs);
        free(abs);
    }
}

/* ------------------------------------------------------------------ */
/* Project tree (NListtree -- real subfolder nodes, 4 groups: C-Quellen/ */
/* ASM-Quellen/C-Header/ASM-Include, per user feedback)                 */
/* ------------------------------------------------------------------ */
/* TRUE if the open file has unsaved edits (per TextEditor.mcc's own
 * modified-tracking) or the project itself has unsaved changes (a file
 * was added/removed, or settings were applied) since the last save. */
static int has_unsaved_changes(void)
{
    ULONG changed = FALSE;
    if (g_currentFile[0])
        get(g_editor, MUIA_TextEditor_HasChanged, &changed);
    return changed || g_projectDirty;
}

static void update_title(void)
{
    static char title[96];
    if (has_unsaved_changes())
        strcpy(title, AIDE_WINDOW_TITLE_BASE " *");
    else
        strcpy(title, AIDE_WINDOW_TITLE_BASE);
    set(g_window, MUIA_Window_Title, title);
}

/* The 4 fixed group roots -- a leaf's group is purely a display
 * classification (by extension), never stored on disk; only the group
 * roots themselves need remembering, so the double-click handler can
 * tell "reached a group heading, stop walking up" apart from "reached a
 * real project subfolder, keep walking" (see tree_relpath_of()). */
static struct MUI_NListtree_TreeNode *g_treeGroupCSrc, *g_treeGroupAsmSrc,
                                    *g_treeGroupCHdr, *g_treeGroupAsmInc;

#define TREE_FOLDER_CACHE_CAP 64
typedef struct {
    char path[128];
    struct MUI_NListtree_TreeNode *node;
} TreeFolderCacheEntry;

/* Finds (or creates, along with any missing intermediate folders) the
 * folder node for `dirPath` (relative to `groupRoot`, "" meaning the
 * group root itself) -- splits on '/' so "src/foo" reuses "src"'s node
 * if already created before recursing to make "foo" under it. `cache` is
 * scoped to a single group/call to refresh_tree(): the same relative
 * folder name in two different groups (or across a rebuild) must not
 * resolve to a stale node. */
static struct MUI_NListtree_TreeNode *tree_folder_node(struct MUI_NListtree_TreeNode *groupRoot,
                                                        TreeFolderCacheEntry *cache, size_t *cacheCount,
                                                        const char *dirPath)
{
    struct MUI_NListtree_TreeNode *parent;
    struct MUI_NListtree_TreeNode *node;
    const char *slash;
    const char *name;
    size_t i;

    if (!dirPath[0])
        return groupRoot;

    for (i = 0; i < *cacheCount; ++i)
        if (strcmp(cache[i].path, dirPath) == 0)
            return cache[i].node;

    slash = strrchr(dirPath, '/');
    if (!slash) {
        parent = groupRoot;
        name = dirPath;
    } else {
        char parentPath[128];
        size_t len = (size_t)(slash - dirPath);
        if (len >= sizeof(parentPath)) len = sizeof(parentPath) - 1;
        memcpy(parentPath, dirPath, len);
        parentPath[len] = '\0';
        parent = tree_folder_node(groupRoot, cache, cacheCount, parentPath);
        name = slash + 1;
    }

    node = (struct MUI_NListtree_TreeNode *)DoMethod(g_treeList, MUIM_NListtree_Insert,
        (ULONG)name, 0, (ULONG)parent, MUIV_NListtree_Insert_PrevNode_Tail, TNF_LIST | TNF_OPEN);

    if (*cacheCount < TREE_FOLDER_CACHE_CAP) {
        strncpy(cache[*cacheCount].path, dirPath, sizeof(cache[*cacheCount].path) - 1);
        cache[*cacheCount].path[sizeof(cache[*cacheCount].path) - 1] = '\0';
        cache[*cacheCount].node = node;
        ++*cacheCount;
    }
    return node;
}

/* Inserts `relPath` as a leaf under `groupRoot`, creating whatever
 * intermediate folder nodes its directory components need. */
/* Bold if `relPath` is the file currently open in g_editor, italic on
 * top of that if it has unsaved edits -- per user feedback: the "* "
 * flat-list marker got dropped in the NListtree rewrite and never
 * replaced, so unsaved changes stopped being signalled in the tree at
 * all (only the window title still showed it). NList/NListtree's text
 * engine recognizes "\033b"/"\033i" (ESC b / ESC i, MCC_NList.doc's
 * "soft style" escapes -- same convention as TextEditor.mcc) directly in
 * plain content strings, no DisplayHook needed; both combine (neither
 * resets the other -- only "\033n" does that, which we never emit). */
static void tree_leaf_display_name(char *out, size_t outsz, const char *base, int isActive, int isDirty)
{
    out[0] = '\0';
    if (isActive) strncat(out, "\033b", outsz - strlen(out) - 1);
    if (isDirty)  strncat(out, "\033i", outsz - strlen(out) - 1);
    strncat(out, base, outsz - strlen(out) - 1);
}

static void tree_add_file(struct MUI_NListtree_TreeNode *groupRoot, TreeFolderCacheEntry *cache,
                           size_t *cacheCount, const char *relPath)
{
    struct MUI_NListtree_TreeNode *parent;
    const char *slash = strrchr(relPath, '/');
    const char *base;
    char dir[128];
    char displayName[160];
    int isActive = 0, isDirty = 0;

    if (slash) {
        size_t len = (size_t)(slash - relPath);
        if (len >= sizeof(dir)) len = sizeof(dir) - 1;
        memcpy(dir, relPath, len);
        dir[len] = '\0';
        base = slash + 1;
    } else {
        dir[0] = '\0';
        base = relPath;
    }

    if (g_currentFile[0]) {
        char abs[AIDE_MAX_PATH];
        if (g_projectRoot[0]) {
            char *joined = join_path(g_projectRoot, relPath);
            strncpy(abs, joined, sizeof(abs) - 1);
            abs[sizeof(abs) - 1] = '\0';
            free(joined);
        } else {
            strncpy(abs, relPath, sizeof(abs) - 1);
            abs[sizeof(abs) - 1] = '\0';
        }
        if (strcmp(abs, g_currentFile) == 0) {
            ULONG changed = FALSE;
            isActive = 1;
            get(g_editor, MUIA_TextEditor_HasChanged, &changed);
            isDirty = changed;
        }
    }
    tree_leaf_display_name(displayName, sizeof(displayName), base, isActive, isDirty);

    parent = tree_folder_node(groupRoot, cache, cacheCount, dir);
    DoMethod(g_treeList, MUIM_NListtree_Insert, (ULONG)displayName, 0, (ULONG)parent,
             MUIV_NListtree_Insert_PrevNode_Tail, 0);
}

static struct MUI_NListtree_TreeNode *tree_group_node(const char *title)
{
    return (struct MUI_NListtree_TreeNode *)DoMethod(g_treeList, MUIM_NListtree_Insert,
        (ULONG)title, 0, MUIV_NListtree_Insert_ListNode_Root, MUIV_NListtree_Insert_PrevNode_Tail,
        TNF_LIST | TNF_OPEN);
}

static int has_ext(const char *path, const char *ext)
{
    const char *dot = strrchr(path, '.');
    return dot && aide_str_ieq(dot, ext);
}

static void refresh_tree(void)
{
    size_t i;
    TreeFolderCacheEntry cSrcCache[TREE_FOLDER_CACHE_CAP], asmSrcCache[TREE_FOLDER_CACHE_CAP];
    TreeFolderCacheEntry hdrCache[TREE_FOLDER_CACHE_CAP], incCache[TREE_FOLDER_CACHE_CAP];
    size_t cSrcCount = 0, asmSrcCount = 0, hdrCount = 0, incCount = 0;
    int basic, asmonly;

    DoMethod(g_treeList, MUIM_NListtree_Clear, NULL, 0);

    /* ACE BASIC uses ".h" for headers just like C, and the
    ** preprocessor-only ones are textually indistinguishable -- but the
    ** project's language settles it, since a project is either C or
    ** BASIC, never both. Only the group in use is shown. */
    basic = (g_project.language == AIDE_LANG_BASIC);
    asmonly = (g_project.language == AIDE_LANG_ASM);

    g_treeGroupCSrc   = tree_group_node(asmonly ? "Quellen"
                                                : (basic ? "BASIC-Quellen" : "C-Quellen"));
    g_treeGroupAsmSrc = tree_group_node("ASM-Quellen");
    g_treeGroupCHdr   = tree_group_node(asmonly ? "Header"
                                                : (basic ? "BASIC-Header" : "C-Header"));
    g_treeGroupAsmInc = tree_group_node("ASM-Include");

    for (i = 0; i < g_project.sources.count; ++i) {
        const char *path = g_project.sources.items[i];
        if (has_ext(path, ".s") || has_ext(path, ".asm"))
            tree_add_file(g_treeGroupAsmSrc, asmSrcCache, &asmSrcCount, path);
        else
            tree_add_file(g_treeGroupCSrc, cSrcCache, &cSrcCount, path);
    }

    for (i = 0; i < g_project.headers.count; ++i) {
        const char *path = g_project.headers.items[i];
        if (has_ext(path, ".i"))
            tree_add_file(g_treeGroupAsmInc, incCache, &incCount, path);
        else
            tree_add_file(g_treeGroupCHdr, hdrCache, &hdrCount, path);
    }

    update_title();
}

/* Reconstructs the project-relative path of a double-clicked leaf node by
 * walking up its parents (MUIM_NListtree_GetEntry .. Position_Parent)
 * collecting tn_Name, stopping at one of the 4 group roots (excluded from
 * the result) or a NULL parent. Returns 0 (leaves *out untouched) if
 * `node` is NULL, is itself a group root, or is a folder (TNF_LIST set --
 * only real files are leaves, see tree_add_file()/tree_folder_node()).
 * No tn_User/Construct/DestructHook needed this way -- see the comment
 * above g_treeGroupCSrc for why. */
static int tree_relpath_of(struct MUI_NListtree_TreeNode *node, char *out, size_t outsz)
{
    struct MUI_NListtree_TreeNode *comps[16];
    int n = 0;
    int i;

    if (!node || (node->tn_Flags & TNF_LIST))
        return 0;

    while (node && n < 16
           && node != g_treeGroupCSrc && node != g_treeGroupAsmSrc
           && node != g_treeGroupCHdr && node != g_treeGroupAsmInc) {
        comps[n++] = node;
        node = (struct MUI_NListtree_TreeNode *)DoMethod(g_treeList, MUIM_NListtree_GetEntry,
            node, MUIV_NListtree_GetEntry_Position_Parent, 0);
    }
    if (n == 0)
        return 0;

    out[0] = '\0';
    for (i = n - 1; i >= 0; --i) {
        /* tree_leaf_display_name() may have prefixed the leaf's own
         * tn_Name with "\033b"/"\033i" soft-style escapes (bold if
         * active, italic if also dirty) -- strip those back off so a
         * styled filename doesn't end up as a literal (nonexistent) path
         * component. Folder names never get styled, so this is a no-op
         * for every component except possibly the last. */
        const char *name = comps[i]->tn_Name;
        while (name[0] == '\033' && name[1] != '\0')
            name += 2;
        if (out[0])
            strncat(out, "/", outsz - strlen(out) - 1);
        strncat(out, name, outsz - strlen(out) - 1);
    }
    return 1;
}

/* Resolves a resolved-relative path (see tree_relpath_of()) to an
 * absolute one. Mirrors Project::absolutePath()'s "already-absolute vs.
 * relative-to-root" rule (core/src/aide_makegen.c's abs_path(), same
 * logic, kept local here since the Amiga UI layer doesn't otherwise need
 * core/aide_makegen.h). */
static int resolve_tree_row(const char *relative, char *out, size_t outsz)
{
    if (g_projectRoot[0]) {
        /* join_path() (AddPart()-based) handles both the plain-relative
         * and already-absolute cases -- see its own comment on why not
         * a hand-rolled "%s/%s". */
        char *joined = join_path(g_projectRoot, relative);
        strncpy(out, joined, outsz - 1);
        free(joined);
    } else {
        strncpy(out, relative, outsz - 1);
    }
    out[outsz - 1] = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* Symbol index (F3 go-to-definition, Shift+F3 references)               */
/* ------------------------------------------------------------------ */
static const char *symbol_kind_label(AideSymbolKind k)
{
    switch (k) {
    case AIDE_SYM_FUNCTION:       return "Fn";
    case AIDE_SYM_PROTOTYPE:      return "Proto";
    case AIDE_SYM_VARIABLE:       return "Var";
    case AIDE_SYM_DEFINE:         return "Def";
    case AIDE_SYM_TYPEDEF:        return "Type";
    case AIDE_SYM_STRUCT:         return "Struct";
    case AIDE_SYM_UNION:          return "Union";
    case AIDE_SYM_ENUM:           return "Enum";
    case AIDE_SYM_LABEL:          return "Label";
    case AIDE_SYM_LOCAL_LABEL:    return "LLabel";
    case AIDE_SYM_EQU_CONSTANT:   return "Equ";
    case AIDE_SYM_ASM_MACRO:      return "Macro";
    case AIDE_SYM_STRUCT_MEMBER:  return "Member";
    default:                      return "?";
    }
}

static void refresh_symbols_list(void)
{
    size_t i;

    DoMethod(g_symbolsList, MUIM_NList_Clear);
    for (i = 0; i < g_symbols.count; ++i) {
        const AideSymbol *s = &g_symbols.items[i];
        AideStr row;
        aide_str_init(&row);
        aide_str_append(&row, symbol_kind_label(s->kind));
        aide_str_append(&row, "  ");
        aide_str_append(&row, s->name);
        DoMethod(g_symbolsList, MUIM_NList_InsertSingle, (ULONG)row.data, MUIV_NList_Insert_Bottom);
        aide_str_free(&row);
    }
}

static void rebuild_symbol_index(void)
{
    AideStrList absSources, absHeaders;
    size_t i;

    aide_strlist_init(&absSources);
    aide_strlist_init(&absHeaders);

    if (g_projectRoot[0]) {
        for (i = 0; i < g_project.sources.count; ++i)
            aide_strlist_push_owned(&absSources, join_path(g_projectRoot, g_project.sources.items[i]));
        for (i = 0; i < g_project.headers.count; ++i)
            aide_strlist_push_owned(&absHeaders, join_path(g_projectRoot, g_project.headers.items[i]));
    }

    symbolindex_rebuild(&g_symbols, &absSources, &absHeaders);

    aide_strlist_free(&absSources);
    aide_strlist_free(&absHeaders);

    refresh_symbols_list();
}

/* ------------------------------------------------------------------ */
/* Hothelp/autodoc index (F1/Shift+F1/F2, see help/helpwindow.h)         */
/* ------------------------------------------------------------------ */
static void collect_active_sdk_descriptors(AideSdkDescriptor **outArr, size_t *outCount)
{
    AideSdkDescriptor *arr = NULL;
    size_t cap = 0, count = 0;
    AideStrList ids;
    size_t i;

    aide_strlist_init(&ids);
    for (i = 0; i < g_project.sdks.selected.count; ++i)
        aide_strlist_push(&ids, g_project.sdks.selected.items[i]);

    for (i = 0; i < ids.count; ++i) {
        const AideSdkDescriptor *sdk = aide_sdk_list_resolve(&g_sdkRegistry, ids.items[i]);
        if (!sdk)
            continue;
        if (count >= cap) {
            cap = cap > 0 ? cap * 2 : 4;
            arr = (AideSdkDescriptor *)realloc(arr, cap * sizeof(AideSdkDescriptor));
        }
        aide_sdk_descriptor_copy(&arr[count], sdk); /* copy() initializes arr[count] itself */
        count++;
    }
    aide_strlist_free(&ids);

    *outArr = arr;
    *outCount = count;
}

/* Autodoc index build has no other feedback otherwise -- on the first
 * scan of an uncached SDK (NDK 3.2 alone is ~2300 autodoc entries)
 * the app just sat there looking
 * hung until it finished, which per user feedback needed some visible
 * sign of life. */
static void autodoc_progress(void *ctx, const char *sdkId, size_t fileIndex, size_t fileCount)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "Baue Autodoc-Index (%s): %lu/%lu",
             sdkId, (unsigned long)fileIndex, (unsigned long)fileCount);
    set_status(msg);
}

static void rebuild_help_index(void)
{
    AideSdkDescriptor *arr;
    size_t count, i;

    collect_active_sdk_descriptors(&arr, &count);
    helpwindow_reload_index(arr, count, autodoc_progress, NULL);
    for (i = 0; i < count; ++i)
        aide_sdk_descriptor_free(&arr[i]);
    free(arr);
}

/* The autodoc/Hothelp index is built lazily, on first actual use (F1/F2 --
 * see ensure_help_index_current()), NOT after every project action. It
 * only depends on the active SDK set, not on the project's own files, and
 * scanning a real SDK's full autodoc corpus (NDK 3.2 alone is ~5.9 MB
 * across ~2300 entries) is slow
 * enough on real 68k hardware that doing it synchronously on something as
 * routine as "New Project" or "Datei hinzufuegen..." made the app look
 * hung for minutes on a project with a single empty file -- per user
 * feedback, a hard no-go. invalidate_help_index() just sets a flag; the
 * actual (slow, first-time) rebuild happens only where it's unavoidable.
 * The on-disk cache (see helpwindow_init()'s cacheDir) then makes repeat
 * builds for the same SDK set fast even across separate program runs. */
static int g_helpIndexDirty = 1;

static void invalidate_help_index(void)
{
    g_helpIndexDirty = 1;
}

static void ensure_help_index_current(void)
{
    if (!g_helpIndexDirty)
        return;
    set_status("Baue Autodoc-Index... (beim ersten Mal je SDK kann das dauern)");
    rebuild_help_index();
    g_helpIndexDirty = 0;
}

/* ------------------------------------------------------------------ */
/* Editor load/save (plain text -- no highlighting yet, see file header) */
/* ------------------------------------------------------------------ */
static void editor_clear(void)
{
    DoMethod(g_editor, MUIM_TextEditor_ClearText);
    /* Per MCC_TextEditor.doc, MUIA_TextEditor_HasChanged is untouched by
     * ClearText/InsertText/Export/Import -- the app is expected to set it
     * FALSE itself whenever it loads/discards/exports content, otherwise
     * it either never turns dirty at all or (worse) stays dirty forever
     * after the first edit, even across files/saves. */
    set(g_editor, MUIA_TextEditor_HasChanged, FALSE);
    g_currentFile[0] = '\0';
}

static int editor_open_file(const char *path)
{
    char *data;
    size_t size;

    if (!aide_fs_read_file(path, &data, &size))
        return 0;

    DoMethod(g_editor, MUIM_TextEditor_ClearText);
    DoMethod(g_editor, MUIM_TextEditor_InsertText, (ULONG)data, MUIV_TextEditor_InsertText_Top);
    free(data);
    set(g_editor, MUIA_TextEditor_HasChanged, FALSE);

    strncpy(g_currentFile, path, sizeof(g_currentFile) - 1);
    g_currentFile[sizeof(g_currentFile) - 1] = '\0';
    return 1;
}

static int editor_save_current(void)
{
    STRPTR text;
    FILE *f;

    if (!g_currentFile[0])
        return 0;

    text = (STRPTR)DoMethod(g_editor, MUIM_TextEditor_ExportText);
    if (!text)
        return 0;

    f = fopen(g_currentFile, "wb");
    if (f) {
        fwrite(text, 1, strlen((char *)text), f);
        fclose(f);
    }
    FreeVec(text);
    if (f)
        set(g_editor, MUIA_TextEditor_HasChanged, FALSE);
    return f != NULL;
}

/* Opens `absPath` (if it isn't already the current file) and moves the
 * cursor to `line` (1-based; 0/negative leaves the cursor where the file
 * open put it). Used by go-to-definition, find-references/find-in-
 * project, and issue navigation -- see file header for the "doesn't
 * guard unsaved changes" caveat. */
static void goto_file_line(const char *absPath, int line)
{
    if (strcmp(absPath, g_currentFile) != 0) {
        if (!editor_open_file(absPath)) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte Datei nicht \366ffnen.");
            return;
        }
    }
    if (line > 0) {
        set(g_editor, MUIA_TextEditor_CursorY, (ULONG)(line - 1));
        set(g_editor, MUIA_TextEditor_CursorX, 0UL);
    }
}

static int is_ident_char(char c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* Fills *out with the identifier touching the cursor's current position
 * (MUIA_TextEditor_CursorX/Y are [ISG] -- get-only here), for F1/F3/
 * Shift+F1/Shift+F3. Returns 0 if the cursor isn't on/adjacent to an
 * identifier or nothing is loaded. */
/* Returns a freshly malloc'd copy of 0-based line `lineIndex`'s text (no
 * trailing newline), or NULL if the editor is empty or the line doesn't
 * exist. Shared by get_word_under_cursor() and
 * try_open_include_on_current_line(). */
static char *get_line_text(long lineIndex)
{
    STRPTR text;
    char *lineStart;
    char *result;
    long row;

    text = (STRPTR)DoMethod(g_editor, MUIM_TextEditor_ExportText);
    if (!text)
        return NULL;

    lineStart = (char *)text;
    for (row = 0; row < lineIndex; ++row) {
        char *nl = strchr(lineStart, '\n');
        if (!nl) { lineStart = NULL; break; }
        lineStart = nl + 1;
    }
    if (!lineStart) {
        FreeVec(text);
        return NULL;
    }

    {
        char *lineEnd = strchr(lineStart, '\n');
        size_t lineLen = lineEnd ? (size_t)(lineEnd - lineStart) : strlen(lineStart);
        result = aide_strndup(lineStart, lineLen);
    }
    FreeVec(text);
    return result;
}

static int get_word_under_cursor(char *out, size_t outsz)
{
    ULONG cx = 0, cy = 0;
    char *line;
    int start, end;
    size_t lineLen;

    get(g_editor, MUIA_TextEditor_CursorX, &cx);
    get(g_editor, MUIA_TextEditor_CursorY, &cy);

    line = get_line_text((long)cy);
    if (!line)
        return 0;

    lineLen = strlen(line);
    {
        int cxi = (int)cx;
        if ((size_t)cxi > lineLen) cxi = (int)lineLen;

        start = cxi;
        while (start > 0 && is_ident_char(line[start - 1])) start--;
        end = cxi;
        while ((size_t)end < lineLen && is_ident_char(line[end])) end++;
    }

    if (end <= start) {
        free(line);
        return 0;
    }

    {
        size_t len = (size_t)(end - start);
        if (len >= outsz) len = outsz - 1;
        memcpy(out, line + start, len);
        out[len] = '\0';
    }

    free(line);
    return 1;
}

static int ci_prefix_eq(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b || a == '\0') return 0;
        s++; prefix++;
    }
    return 1;
}

/* Recognizes "#include <foo.h>" / "#include \"foo.h\"" (C) or
 * "include foo.i" / "include \"foo.i\"" (asm, case-insensitive keyword,
 * closing quote optional) at the start of `line` (leading whitespace
 * allowed) -- hand-parsed rather than run through aide_regex, mirrors
 * Qt's tryOpenIncludeOnLine() two patterns. On a match, fills
 * *outDirective (malloc'd) and *outQuoted, returns 1. */
static int parse_include_directive(const char *line, int isAsm, char **outDirective, int *outQuoted)
{
    const char *p = line;
    const char *start, *end;

    while (*p == ' ' || *p == '\t') p++;

    if (!isAsm) {
        char closeChar;
        if (*p != '#') return 0;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (!ci_prefix_eq(p, "include")) return 0;
        p += 7;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '<') { *outQuoted = 0; closeChar = '>'; }
        else if (*p == '"') { *outQuoted = 1; closeChar = '"'; }
        else return 0;
        p++;
        start = p;
        end = strchr(p, closeChar);
        if (!end) return 0;
    } else {
        if (!ci_prefix_eq(p, "include")) return 0;
        p += 7;
        if (*p != ' ' && *p != '\t') return 0;
        while (*p == ' ' || *p == '\t') p++;
        *outQuoted = 0;
        if (*p == '"') {
            p++;
            start = p;
            end = strchr(p, '"');
            if (!end) return 0;
        } else {
            start = p;
            end = start;
            while (*end && *end != ' ' && *end != '\t' && *end != ';') end++;
            if (end == start) return 0;
        }
    }

    *outDirective = aide_strndup(start, (size_t)(end - start));
    return 1;
}

static int is_asm_current_file(void)
{
    const char *dot = strrchr(g_currentFile, '.');
    return dot && (strcmp(dot, ".s") == 0 || strcmp(dot, ".asm") == 0 || strcmp(dot, ".i") == 0);
}

/* Forward declaration -- defined down in the build-pipeline section
 * (needs g_project.sdks + g_sdkRegistry, already in scope everywhere in
 * this file), used here too since it's the exact same "active SDKs'
 * include dirs" logic the build itself uses. */
static void collect_active_sdk_includes(AideStrList *includeDirs, AideStrList *asmIncludeDirs, AideStrList *libDirs);

/* F3's first check, before symbol lookup (mirrors Qt's
 * tryOpenIncludeOnLine()): if the cursor's line is an #include/include
 * directive, resolve it against the project's own include paths plus
 * every active SDK's (and the toolchain's base) include dirs and jump
 * there -- this is why "F3 on #include <stdio.h>" looked like it "didn't
 * find the symbol": stdio.h was never going to be in the symbol index,
 * it needed this separate include-resolution path instead. Returns 1 if
 * the line WAS an include directive (whether or not resolution
 * succeeded) so the caller doesn't fall through to a confusing "symbol
 * not found" for something that's clearly an include line. */
static int try_open_include_on_current_line(void)
{
    ULONG cy = 0;
    char *line;
    char *directive = NULL;
    int quoted = 0;
    int matched;
    char fileDir[AIDE_MAX_PATH];
    AideStrList searchPaths;
    char *resolved;
    size_t i;

    get(g_editor, MUIA_TextEditor_CursorY, &cy);
    line = get_line_text((long)cy);
    if (!line)
        return 0;

    matched = parse_include_directive(line, is_asm_current_file(), &directive, &quoted);
    free(line);
    if (!matched)
        return 0;

    {
        const char *base = g_currentFile[0] ? g_currentFile : g_projectRoot;
        const char *lastSlash = strrchr(base, '/');
        size_t len = lastSlash ? (size_t)(lastSlash - base) : 0;
        if (len >= sizeof(fileDir)) len = sizeof(fileDir) - 1;
        memcpy(fileDir, base, len);
        fileDir[len] = '\0';
    }

    aide_strlist_init(&searchPaths);
    for (i = 0; i < g_project.includePaths.count; ++i)
        aide_strlist_push_owned(&searchPaths, join_path(g_projectRoot, g_project.includePaths.items[i]));
    {
        char *toolchainInc = aide_toolchain_include_dir(&g_toolchain);
        aide_strlist_push_owned(&searchPaths, toolchainInc);
    }
    {
        AideStrList asmDirs, libDirs;
        aide_strlist_init(&asmDirs);
        aide_strlist_init(&libDirs);
        collect_active_sdk_includes(&searchPaths, &asmDirs, &libDirs);
        aide_strlist_free(&asmDirs);
        aide_strlist_free(&libDirs);
    }

    resolved = aide_includegraph_resolve(directive, quoted, fileDir, &searchPaths);
    aide_strlist_free(&searchPaths);

    if (!resolved) {
        char msg[192];
        snprintf(msg, sizeof(msg), "\"%s\" nicht gefunden (Include-Pfade/SDKs pr\374fen).", directive);
        set_status(msg);
    } else {
        goto_file_line(resolved, 0);
        free(resolved);
    }
    free(directive);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Project actions                                                       */
/* ------------------------------------------------------------------ */
/* Asks for both a name AND a location up front (a file requester, not
 * just a name -- per user feedback) and saves the new project
 * immediately, rather than leaving it unsaved until the first
 * "Speichern": that way the project's source template(s) exist on disk
 * right away and there's no ambiguous in-between "created but nowhere"
 * state. */

/* Builds used to route every build-generated path through a private
 * "AIDEBUILD:" assign pointing at g_projectRoot, because vc's own
 * frontend re-splits its command line on whitespace INTERNALLY --
 * a project living on a volume with a space in its name (like
 * "RAM Disk:", the *default* ram-disk name on stock Workbench) could
 * therefore never be passed to vc directly, no matter how it was
 * quoted. That assign is gone: the async build launcher (do_build(),
 * below) now starts `make` with its current directory set to
 * g_projectRoot directly, via SystemTagList()'s NP_CurrentDir tag --
 * see launch_build_script()'s comment. Every path *inside* the project
 * (sources, headers, OBJDIR/BINDIR/TARGET, vc.cfg) is then simply
 * relative, so the volume name -- space or not -- never appears on
 * vc's/vasm's/vlink's command line at all. SDK/toolchain paths ("SDK:",
 * "vbcc:") stay absolute, as before; those are chosen to be space-free
 * by convention, not because of any assign. */

/* FilePart(), not strrchr(aprojPath, '/'): a project saved directly at a
 * volume root ("RAM:test.aproj") has no '/' at all, so strrchr() found
 * nothing and left g_projectRoot empty -- breaking every join_path() call
 * for that project's sources. FilePart() returns a pointer to the
 * filename part regardless of whether the path ends in '/' or ':'. */
/* Every SDK the project selects that the registry lacks -- neither
 * built in nor described by an AmiDevSDK.json under SDK: -- one per line
 * into *out. Returns how many. The Makefile leaves such an SDK out, so a
 * build would fail on its headers with nothing saying why. */
static size_t missing_sdks(AideStr *out)
{
    AideStrList missing;
    size_t i, n;

    aide_strlist_init(&missing);
    aide_sdk_list_missing(&g_sdkRegistry, &g_project.sdks.selected, &missing);
    for (i = 0; i < missing.count; ++i) {
        aide_str_append(out, "  ");
        aide_str_append(out, missing.items[i]);
        aide_str_append_char(out, '\n');
    }
    n = missing.count;
    aide_strlist_free(&missing);
    return n;
}

/* ---- SDK completeness (core/aide_sdk_check(), worded here) ---- */
static const char *sdk_part_label(AideSdkPartKind k)
{
    switch (k) {
    case AIDE_SDK_PART_ROOT:         return "Verzeichnis";
    case AIDE_SDK_PART_INCLUDE:      return "C-Includes";
    case AIDE_SDK_PART_ASM_INCLUDE:  return "ASM-Includes";
    case AIDE_SDK_PART_LIB:          return "Libraries";
    case AIDE_SDK_PART_AUTODOC_DIR:  return "Autodocs";
    case AIDE_SDK_PART_AUTODOC_FILE: return "Autodoc";
    case AIDE_SDK_PART_SFD:          return "SFD";
    }
    return "Teil";
}

static void sdk_name_into(AideStr *out, const AideSdkDescriptor *d)
{
    size_t n = strlen(d->displayName), v = strlen(d->version);
    aide_str_append(out, d->displayName[0] ? d->displayName : d->id);
    if (v && !(n >= v && strcmp(d->displayName + n - v, d->version) == 0)) {
        aide_str_append_char(out, ' ');
        aide_str_append(out, d->version);
    }
}

static int sdk_selected_by_project(const AideSdkDescriptor *d)
{
    size_t i;
    for (i = 0; i < g_project.sdks.selected.count; ++i)
        if (aide_sdk_list_resolve(&g_sdkRegistry, g_project.sdks.selected.items[i]) == d)
            return 1;
    return 0;
}

/* One SDK's verdict: a headline, and with `details` every missing part
 * plus any look-alike found next to it (a renamed directory is the
 * usual reason). */
static void sdk_report_into(AideStr *out, const AideSdkDescriptor *d, int details)
{
    AideSdkCheck c;
    size_t i;
    char num[32];

    aide_sdk_check_init(&c);
    aide_sdk_check(d, &c);
    aide_str_append(out, "  ");
    sdk_name_into(out, d);
    if (sdk_selected_by_project(d))
        aide_str_append(out, " [Projekt]");
    if (c.missing == 0) {
        aide_str_append(out, ": vollst\344ndig\n");
    } else if (c.count > 0 && !c.items[0].present && c.items[0].kind == AIDE_SDK_PART_ROOT) {
        aide_str_append(out, ": nicht gefunden\n");
    } else {
        sprintf(num, ": %lu von %lu Teilen fehlen\n", (unsigned long)c.missing, (unsigned long)c.count);
        aide_str_append(out, num);
    }
    for (i = 0; i < c.count; ++i) {
        const AideSdkPart *p = &c.items[i];
        if (p->present)
            continue;
        /* below a missing root, only the root itself is worth a line */
        if (i > 0 && !c.items[0].present && c.items[0].kind == AIDE_SDK_PART_ROOT)
            break;
        if (!details && !p->similar)
            continue;
        aide_str_append(out, "      fehlt ");
        aide_str_append(out, sdk_part_label(p->kind));
        aide_str_append(out, ": ");
        aide_str_append(out, p->path);
        aide_str_append_char(out, '\n');
        if (p->similar) {
            aide_str_append(out, "        vorhanden ist: ");
            aide_str_append(out, p->similar);
            aide_str_append(out, "  (umbenennen?)\n");
        }
    }
    aide_sdk_check_free(&c);
}

/* Versionen -> "AmiVCS-GUI oeffnen": the stand-alone amivcs-gui on the
 * project directory, started detached -- it has its own window and the
 * IDE carries on. Program name from Einstellungen (AmiVCS-GUI). */
static void do_open_vcs_gui(void)
{
    char cmd[AIDE_MAX_PATH + 160];
    struct TagItem tags[5];
    BPTR nilIn, nilOut;
    LONG rc;

    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Kein Projekt geladen.");
        return;
    }
    sprintf(cmd, "%s \"%s\"", prefs_amivcs_gui_program(), g_projectRoot);
    nilIn = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    nilOut = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    tags[0].ti_Tag = SYS_Input;    tags[0].ti_Data = (ULONG)nilIn;
    tags[1].ti_Tag = SYS_Output;   tags[1].ti_Data = (ULONG)nilOut;
    tags[2].ti_Tag = SYS_Asynch;   tags[2].ti_Data = TRUE;
    tags[3].ti_Tag = NP_StackSize; tags[3].ti_Data = 65536;
    tags[4].ti_Tag = TAG_DONE;     tags[4].ti_Data = 0;
    rc = SystemTagList((CONST_STRPTR)cmd, tags);
    if (rc == -1) {
        if (nilIn) Close(nilIn);
        if (nilOut) Close(nilOut);
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "\"%s\" konnte nicht gestartet werden.\n\n"
                    "Das Programm l\344sst sich unter Projekt > Einstellungen\n"
                    "(AmiVCS-GUI) eintragen.", prefs_amivcs_gui_program());
        return;
    }
    set_status("AmiVCS-GUI gestartet");
}

/* Projekt -> "Installation pr\374fen...": drawers, compiler and tools,
 * make, libraries and SDKs with their versions, in a window of its own
 * (checkwindow.c) -- no longer as text in the Ausgabe page. */
static void do_check_sdks(void)
{
    CheckContext ctx;
    ctx.toolchainRoot    = prefs_toolchain_root();
    ctx.sdkRoot          = prefs_sdk_root();
    ctx.makeProgram      = prefs_make_program();
    ctx.amivcsProgram    = prefs_amivcs_program();
    ctx.amivcsGuiProgram = prefs_amivcs_gui_program();
    ctx.sdks             = &g_sdkRegistry;
    ctx.projectSdks      = g_projectFile[0] ? &g_project.sdks.selected : NULL;
    set_status("Pr\374fe Installation...");
    checkwindow_show(&ctx);
    set_status("Installation gepr\374ft");
}

/* The project's own SDKs that are installed but incomplete, as short
 * lines (look-alikes included) -- for the note after loading. */
static size_t incomplete_project_sdks(AideStr *out)
{
    size_t i, n = 0;
    for (i = 0; i < g_sdkRegistry.count; ++i) {
        const AideSdkDescriptor *d = &g_sdkRegistry.items[i];
        AideSdkCheck c;
        if (!sdk_selected_by_project(d))
            continue;
        aide_sdk_check_init(&c);
        aide_sdk_check(d, &c);
        if (c.missing > 0) {
            sdk_report_into(out, d, 0);
            n++;
        }
        aide_sdk_check_free(&c);
    }
    return n;
}

static void set_project_paths(const char *aprojPath)
{
    STRPTR namePart;
    size_t rootLen;

    strncpy(g_projectFile, aprojPath, sizeof(g_projectFile) - 1);
    g_projectFile[sizeof(g_projectFile) - 1] = '\0';

    namePart = FilePart((STRPTR)aprojPath);
    rootLen = (size_t)(namePart - aprojPath);
    if (rootLen > 0 && aprojPath[rootLen - 1] == '/')
        rootLen--; /* trim the separator, but keep a volume's ':' */
    if (rootLen >= sizeof(g_projectRoot)) rootLen = sizeof(g_projectRoot) - 1;
    memcpy(g_projectRoot, aprojPath, rootLen);
    g_projectRoot[rootLen] = '\0';
}

/* Deferred tail of do_new_project() -- runs only once the Project
 * Settings window (opened by do_new_project() itself) closes via OK;
 * settingswindow_handle_id() itself already refused to let OK close the
 * window with an empty name/binary, so both are guaranteed non-empty
 * here. Builds "<name>.aproj" in g_newProjectDir now that the name is
 * finally known. */
static void finish_new_project_save(void)
{
    char aprojName[160];
    char aprojPath[AIDE_MAX_PATH];

    snprintf(aprojName, sizeof(aprojName), "%s.aproj", g_project.name);
    strncpy(aprojPath, g_newProjectDir, sizeof(aprojPath) - 1);
    aprojPath[sizeof(aprojPath) - 1] = '\0';
    AddPart((STRPTR)aprojPath, (STRPTR)aprojName, (ULONG)sizeof(aprojPath));

    if (!aide_project_save(&g_project, aprojPath)) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Konnte Projekt nicht anlegen:\n%s", aprojPath);
        set_status("Anlegen fehlgeschlagen");
        return;
    }

    set_project_paths(aprojPath);
    g_projectDirty = 0;
    refresh_tree();
    rebuild_symbol_index();
    invalidate_help_index();
    set_status("Projekt angelegt");
    vcspanel_project_changed(g_projectRoot, g_projectFile);
}

/* "Neu": directory first, then Project Settings -- no separate name
 * prompt, since Name/Binary are already fields on the Settings dialog's
 * General tab. Cancel there creates nothing at all (not even an empty
 * in-memory project sticks around as "the open project" -- g_projectRoot
 * stays empty, so every action still gated on "project must be saved
 * first" keeps refusing); OK creates and saves. Per user feedback. */
static void do_new_project(void)
{
    char dir[AIDE_MAX_PATH];
    int created;

    if (!ask_new_project_dir(dir, sizeof(dir), &created))
        return;
    g_newProjectDirCreated = created;

    set_status("Neues Projekt...");

    /* A genuinely empty project -- no auto-created src/main.c, per user
     * feedback: "Neu" doesn't ask what kind of project (C? asm?) this is
     * going to be, so it has no business assuming a C Hello World and
     * silently creating a file for it. Files come from "Neue Datei..."/
     * "Bestehende Datei hinzufuegen..." instead. */
    aide_project_free(&g_project);
    aide_project_init(&g_project);
    /* Not "assuming a project type" (see comment above) -- this is
     * vc.lib, vbcc's own C runtime support library (___stack,
     * __math_init, ___main, ___v0printf, ...), needed by virtually any
     * vbcc-linked Amiga program regardless of what it's actually made
     * of; aide_project_create_minimal_c()/_asm() both added it too.
     * Without it, linking failed with "undefined symbol" for all four
     * (confirmed via a real build log) the moment there was any source
     * at all to link. */
    aide_strlist_push(&g_project.linker.libraries, "vc");
    editor_clear();
    g_projectRoot[0] = '\0';
    g_projectFile[0] = '\0';

    strncpy(g_newProjectDir, dir, sizeof(g_newProjectDir) - 1);
    g_newProjectDir[sizeof(g_newProjectDir) - 1] = '\0';
    g_newProjectAwaitingSave = 1;

    /* Save happens only once this closes via OK -- see
     * finish_new_project_save() and its dispatch in the main loop's
     * default case; Cancel just drops g_newProjectAwaitingSave. */
    settingswindow_set_long_names(aide_project_long_names(g_newProjectDir));
    settingswindow_show(&g_project);
    set_status("Projekteinstellungen: Name/Binary festlegen, dann OK");
}

static void open_project_at(const char *path);

static void do_open_project(void)
{
    char path[AIDE_MAX_PATH];

    if (!ask_file("Projekt \366ffnen (.aproj)", 0, NULL, path, sizeof(path)))
        return;
    open_project_at(path);
}

static void open_project_at(const char *path)
{
    AideProject loaded;

    set_status("Lade Projekt...");

    aide_project_init(&loaded);
    if (!aide_project_load(&loaded, path)) {
        aide_project_free(&loaded);
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte Projekt nicht laden.");
        set_status("Laden fehlgeschlagen");
        return;
    }

    aide_project_free(&g_project);
    g_project = loaded;
    set_project_paths(path);
    editor_clear();
    g_projectDirty = 0;
    refresh_tree();
    rebuild_symbol_index();
    invalidate_help_index();
    set_status("Projekt geladen");
    vcspanel_project_changed(g_projectRoot, g_projectFile);

    {
        AideStr list;
        aide_str_init(&list);
        if (missing_sdks(&list) > 0) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Dieses Projekt benutzt SDKs, die hier nicht\n"
                        "installiert sind:\n\n%s\n"
                        "Der Build findet ihre Header und Libraries nicht.\n"
                        "Abhilfe: ein SDK mit gleichem Namen in dieser oder\n"
                        "neuerer Version unter %s ablegen (mit AmiDevSDK.json).",
                        list.data, prefs_sdk_root());
        }
        aide_str_clear(&list);
        if (incomplete_project_sdks(&list) > 0) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Dieses Projekt benutzt SDKs, denen Teile fehlen:\n\n%s\n"
                        "Was genau fehlt: Projekt -> Installation pr\374fen...",
                        list.data);
        }
        aide_str_free(&list);
    }

    {
        AideStrList upgrades;
        size_t i;

        aide_strlist_init(&upgrades);
        aide_sdk_list_major_upgrades(&g_sdkRegistry, &g_project.sdks.selected, &upgrades);
        if (upgrades.count > 0) {
            AideStr list;
            aide_str_init(&list);
            for (i = 0; i < upgrades.count; ++i) {
                const AideSdkDescriptor *d = aide_sdk_list_resolve(&g_sdkRegistry, upgrades.items[i]);
                aide_str_append(&list, "  ");
                aide_str_append(&list, upgrades.items[i]);
                aide_str_append(&list, "  ->  ");
                aide_str_append(&list, d->displayName);
                {
                    /* "NDK 3.2" already says its version */
                    size_t n = strlen(d->displayName), v = strlen(d->version);
                    if (!(n >= v && strcmp(d->displayName + n - v, d->version) == 0)) {
                        aide_str_append_char(&list, ' ');
                        aide_str_append(&list, d->version);
                    }
                }
                aide_str_append_char(&list, '\n');
            }
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Dieses Projekt wurde f\374r \344ltere Hauptversionen\n"
                        "dieser SDKs geschrieben und baut mit den\n"
                        "installierten:\n\n%s\n"
                        "Ihre Schnittstellen k\366nnen sich ge\344ndert haben.\n"
                        "Die Projektdatei beh\344lt die verlangten Versionen.",
                        list.data);
            aide_str_free(&list);
        }
        aide_strlist_free(&upgrades);
    }
}

/* Unified save: writes the open editor file (if any) and the project --
 * asks for a location only the first time (no separate "save file"
 * command, per user feedback). Also the point where a brand-new
 * project's template source(s) actually get written to disk (see
 * ensure_project_source_files_exist() -- do_new_project() can't do this
 * itself, since g_projectRoot isn't known until here). */
/* First-save target: what ask_file() returned, plus a ".aproj" suffix if
 * the user didn't type one (reqtools has no "force this suffix" tag,
 * unlike Qt's QFileDialog::setDefaultSuffix()). g_projectFile/g_projectRoot
 * are deliberately NOT updated here -- only after aide_project_save()
 * actually succeeds (see do_save()), so a failed first save doesn't wedge
 * every later "Speichern" into silently retrying the same bad path. */
static void build_first_save_target(char *path, size_t pathsz)
{
    size_t len = strlen(path);
    if (len < 6 || !aide_str_ieq(path + len - 6, ".aproj"))
        strncat(path, ".aproj", pathsz - len - 1);
}

static void do_save(void)
{
    char path[AIDE_MAX_PATH];
    const char *target;
    int firstSave = !g_projectFile[0];

    set_status("Speichere...");

    if (g_currentFile[0])
        editor_save_current();

    if (firstSave) {
        if (!ask_file("Projekt speichern unter (.aproj)", 1, NULL, path, sizeof(path))) {
            set_status("Abgebrochen");
            return;
        }
        build_first_save_target(path, sizeof(path));
        target = path;
    } else {
        target = g_projectFile;
    }

    if (!aide_project_save(&g_project, target)) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Konnte Projekt nicht speichern:\n%s", target);
        set_status("Speichern fehlgeschlagen");
        return;
    }
    if (firstSave) {
        set_project_paths(target);
        ensure_project_source_files_exist();
        rebuild_symbol_index();
    }
    g_projectDirty = 0;
    refresh_tree(); /* clears the "*" markers -- must run even on a non-first save */
    set_status("Gespeichert");
    if (firstSave) vcspanel_project_changed(g_projectRoot, g_projectFile);
    else vcspanel_file_saved();
}

/* Asks Quelldatei/Header BEFORE the file requester (not guessed from the
 * extension afterwards, per user feedback), then appends the matching
 * default extension only if the chosen name has none at all -- a file
 * picked or typed with an existing extension (recognized or not) is left
 * exactly as the user named it. */
/* Source vs. header, purely from the extension, no question asked --
 * used by do_add_existing_file() (per user feedback: a file picked off
 * disk already has a real extension, so just trust it). .c/.s/.asm are
 * sources, .h/.i are headers, anything else defaults to source. */
static int classify_by_extension(const char *relPath)
{
    const char *dot = strrchr(relPath, '.');
    if (dot && (strcmp(dot, ".h") == 0 || strcmp(dot, ".i") == 0))
        return 2;
    return 1;
}

/* The 5 "Neue Datei" submenu entries -- see MenuData[] -- each map to one
 * of these instead of a MUI_Request question after a single menu item. */
enum {
    NEWFILE_C_SOURCE,
    NEWFILE_C_HEADER,
    NEWFILE_ASM_SOURCE,
    NEWFILE_ASM_INCLUDE,
    NEWFILE_BASIC_SOURCE
};

static const char *newfile_default_ext(int kind)
{
    switch (kind) {
    case NEWFILE_C_HEADER:    return ".h";
    case NEWFILE_ASM_SOURCE:  return ".s";
    case NEWFILE_ASM_INCLUDE: return ".i";
    case NEWFILE_BASIC_SOURCE: return ".b";
    default:                  return ".c";
    }
}

/* Does `ext` (including the leading '.') make sense for `kind`? ASM
 * sources accept both ".s" and ".asm" (both are used in the wild and
 * treated as equivalent everywhere an ASM source's extension is
 * checked, e.g. aide_makegen.c's isAsm test); everything else has
 * exactly one expected extension. */
static int newfile_ext_matches_kind(int kind, const char *ext)
{
    if (!ext)
        return 0;
    switch (kind) {
    case NEWFILE_C_SOURCE:    return aide_str_ieq(ext, ".c");
    case NEWFILE_C_HEADER:    return aide_str_ieq(ext, ".h");
    case NEWFILE_ASM_SOURCE:  return aide_str_ieq(ext, ".s") || aide_str_ieq(ext, ".asm");
    case NEWFILE_ASM_INCLUDE: return aide_str_ieq(ext, ".i");
    /* ACE accepts .b and .bas alike (lex.c open_files) */
    case NEWFILE_BASIC_SOURCE: return aide_str_ieq(ext, ".b") || aide_str_ieq(ext, ".bas");
    default:                  return 0;
    }
}

/* "Neue Datei": one of 4 fixed types picked directly from the menu (no
 * requester question anymore -- see MenuData[]), then just a filename
 * (ask_string, not a file requester -- there's nothing to browse for a
 * file that doesn't exist yet). Missing extension gets the type's
 * default appended; a present-but-wrong-for-this-type extension gets a
 * confirmation warning instead of being silently accepted. Split from
 * "Bestehende Datei..." per user feedback: the two cases have genuinely
 * different needs (pick a name vs. pick a file). */
static void do_add_new_file(int kind)
{
    char name[128];
    char relPath[AIDE_MAX_PATH];
    char absPath[AIDE_MAX_PATH];
    const char *defaultExt = newfile_default_ext(kind);
    const char *dot;

    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Bitte zuerst Projekt speichern.");
        return;
    }

    if (!ask_string("Neue Datei (Name, relativ zum Projekt)", "", name, sizeof(name)))
        return;

    strncpy(relPath, name, sizeof(relPath) - 1);
    relPath[sizeof(relPath) - 1] = '\0';
    dot = strrchr(relPath, '.');
    if (!dot) {
        strncat(relPath, defaultExt, sizeof(relPath) - strlen(relPath) - 1);
    } else if (!newfile_ext_matches_kind(kind, dot)) {
        LONG proceed = MUI_Request(g_app, g_window, 0, "Neue Datei",
                                    "_Trotzdem anlegen|_Abbrechen",
                                    "\"%s\" passt nicht zum gew\344hlten Dateityp\n(erwartet: %s).\n\nTrotzdem anlegen?",
                                    relPath, defaultExt);
        if (proceed != 1) {
            set_status("Abgebrochen");
            return;
        }
    }

    /* classic FFS keeps 30 characters and cuts longer names silently */
    if (!aide_project_long_names(g_projectRoot) && !aide_names_fit(relPath, AIDE_AMIGA_NAME_MAX)) {
        MUI_Request(g_app, g_window, 0, "Neue Datei", "_OK",
                    "Ein Datei- oder Ordnername darf hier h\366chstens %ld Zeichen\n"
                    "haben -- FFS speichert nicht mehr und k\374rzt still.\n"
                    "Lange Namen lassen sich in den Projekteinstellungen erlauben.",
                    (long)AIDE_AMIGA_NAME_MAX);
        set_status("Abgebrochen");
        return;
    }

    join_path_into(absPath, sizeof(absPath), g_projectRoot, relPath);

    /* "Neu" means new -- if a file with that name already exists, ask
     * before truncating it, rather than silently keeping (effectively
     * "loading") its existing content. Declining aborts the whole
     * add-file action; nothing is added to the project and the existing
     * file is left untouched. */
    if (aide_fs_exists(absPath)) {
        LONG overwrite = MUI_Request(g_app, g_window, 0, "Neue Datei",
                                      "_Ueberschreiben|_Abbrechen",
                                      "\"%s\" existiert bereits.\n\nWirklich leeren und \374berschreiben?",
                                      relPath);
        if (overwrite != 1) {
            set_status("Abgebrochen");
            return;
        }
    }

    set_status("Lege Datei an...");
    ensure_parent_dir(absPath);
    {
        FILE *f = fopen(absPath, "wb");
        if (!f) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Konnte Datei nicht anlegen:\n%s", absPath);
            set_status("Fehlgeschlagen");
            return;
        }
        fclose(f);
    }

    if (kind == NEWFILE_C_HEADER || kind == NEWFILE_ASM_INCLUDE)
        aide_strlist_push(&g_project.headers, relPath);
    else
        aide_strlist_push(&g_project.sources, relPath);
    g_projectDirty = 1;

    /* Without this, the new (empty) file sits on disk and in the project
     * list, but the editor keeps showing whatever it showed before --
     * g_currentFile never points at the new file, so anything typed
     * afterwards goes nowhere near it, and Speichern/Build silently write
     * or compile the wrong (or no) file. */
    editor_open_file(absPath);

    refresh_tree();
    rebuild_symbol_index();
    set_status("Datei angelegt -- noch nicht gespeichert");
}

/* "Bestehende Datei...": a real file requester, type inferred purely
 * from the extension, no question asked (per user feedback). */
static void do_add_existing_file(void)
{
    char absPath[AIDE_MAX_PATH];
    char relPath[AIDE_MAX_PATH];
    int kind;

    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Bitte zuerst Projekt speichern.");
        return;
    }
    if (!ask_file("Bestehende Datei hinzuf\374gen", 0, g_projectRoot, absPath, sizeof(absPath)))
        return;

    relativize(g_projectRoot, absPath, relPath, sizeof(relPath));
    kind = classify_by_extension(relPath);

    set_status("F\374ge Datei hinzu...");
    if (kind == 2)
        aide_strlist_push(&g_project.headers, relPath);
    else
        aide_strlist_push(&g_project.sources, relPath);
    g_projectDirty = 1;

    editor_open_file(absPath);

    refresh_tree();
    rebuild_symbol_index();
    set_status("Datei hinzugef\374gt -- noch nicht gespeichert");
}

/* Removes every entry equal to `value` from `l` (there's normally at
 * most one, but a project could in principle list the same relative
 * path twice -- no reason to leave a duplicate behind). Returns 1 if
 * anything was actually removed. */
static int strlist_remove_matching(AideStrList *l, const char *value)
{
    size_t i = 0;
    int removedAny = 0;
    while (i < l->count) {
        if (strcmp(l->items[i], value) == 0) {
            free(l->items[i]);
            memmove(&l->items[i], &l->items[i + 1], (l->count - i - 1) * sizeof(char *));
            l->count--;
            removedAny = 1;
        } else {
            ++i;
        }
    }
    return removedAny;
}

/* "Datei aus Projekt entfernen...": operates on whichever row is active
 * in the project tree, same lookup ID_TREE_DCLICK already uses
 * (tree_relpath_of() returns 0 for group/folder nodes, so this quietly
 * refuses on anything that isn't an actual file leaf). Three-way choice
 * mirrors Qt's MainWindow::removeFileFromProject(): remove from the
 * project only, remove and delete the file from disk, or cancel. Marks
 * the project dirty rather than auto-saving -- consistent with
 * do_add_new_file()/do_add_existing_file() above, both of which also
 * leave the actual disk write to an explicit "Speichern". */
static void do_remove_file_from_project(void)
{
    struct MUI_NListtree_TreeNode *active = NULL;
    char relPath[AIDE_MAX_PATH];
    char absPath[AIDE_MAX_PATH];
    LONG choice;
    int alsoDelete;
    int removedAny;

    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Kein Projekt geladen.");
        return;
    }

    get(g_treeList, MUIA_NListtree_Active, &active);
    if (!active || !tree_relpath_of(active, relPath, sizeof(relPath))) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Bitte zuerst eine Datei im Projektbaum ausw\344hlen.");
        return;
    }
    resolve_tree_row(relPath, absPath, sizeof(absPath));

    choice = MUI_Request(g_app, g_window, 0, "Datei entfernen",
                          "_Nur entfernen|Entfernen und _loeschen|_Abbrechen",
                          "\"%s\" aus dem Projekt entfernen?\n\n"
                          "Die Datei kann unangetastet auf der Platte bleiben\n"
                          "oder zus\344tzlich gel\366scht werden.", relPath);
    if (choice != 1 && choice != 2)
        return; /* Abbrechen */
    alsoDelete = (choice == 2);

    removedAny = strlist_remove_matching(&g_project.sources, relPath);
    if (strlist_remove_matching(&g_project.headers, relPath))
        removedAny = 1;
    if (!removedAny)
        return;

    /* If the removed (or about-to-be-deleted) file is the one currently
     * open, clear the editor first -- otherwise Speichern/Build would
     * still write to or reference a file that's no longer part of the
     * project (or no longer exists at all, once deleted below). */
    if (g_currentFile[0] && strcmp(g_currentFile, absPath) == 0)
        editor_clear();

    if (alsoDelete) {
        if (!DeleteFile((CONST_STRPTR)absPath)) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Aus dem Projekt entfernt, aber Datei konnte nicht gel\366scht werden:\n%s",
                        absPath);
        }
    }

    g_projectDirty = 1;
    refresh_tree();
    rebuild_symbol_index();
    set_status(alsoDelete ? "Entfernt und gel\366scht -- noch nicht gespeichert"
                          : "Aus Projekt entfernt -- noch nicht gespeichert");
}

/* ------------------------------------------------------------------ */
/* Go-to-definition (F3), references (Shift+F3), find in project         */
/* ------------------------------------------------------------------ */
static void do_goto_definition(void)
{
    char word[128];
    const AideSymbol *sym;

    if (try_open_include_on_current_line())
        return;

    if (!get_word_under_cursor(word, sizeof(word)))
        return;
    sym = symbolindex_find_definition(&g_symbols, word);
    if (!sym) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Symbol nicht gefunden.");
        return;
    }
    goto_file_line(sym->file, sym->line);
}

static void populate_search_results(void)
{
    size_t i;
    char status[64];

    DoMethod(g_searchList, MUIM_NList_Clear);
    for (i = 0; i < g_searchMatches.count; ++i) {
        const AideSearchMatch *m = &g_searchMatches.items[i];
        const char *base = strrchr(m->file, '/');
        AideStr row;

        base = base ? base + 1 : m->file;
        aide_str_init(&row);
        aide_str_appendf(&row, "%s:%d: ", base, m->line);
        aide_str_append(&row, m->lineText);
        DoMethod(g_searchList, MUIM_NList_InsertSingle, (ULONG)row.data, MUIV_NList_Insert_Bottom);
        aide_str_free(&row);
    }
    set(g_bottomRegister, MUIA_Group_ActivePage, 2UL); /* 0=Ausgabe 1=Issues 2=Suche 3=MemoryMap */
    snprintf(status, sizeof(status), "%lu Treffer", (unsigned long)g_searchMatches.count);
    set_status(status);
}

static void collect_project_files(AideStrList *out)
{
    size_t i;
    if (!g_projectRoot[0])
        return;
    for (i = 0; i < g_project.sources.count; ++i)
        aide_strlist_push_owned(out, join_path(g_projectRoot, g_project.sources.items[i]));
    for (i = 0; i < g_project.headers.count; ++i)
        aide_strlist_push_owned(out, join_path(g_projectRoot, g_project.headers.items[i]));
}

static void do_find_references(void)
{
    char word[128];
    AideStrList files;

    if (!get_word_under_cursor(word, sizeof(word)))
        return;

    set_status("Suche Referenzen...");
    aide_strlist_init(&files);
    collect_project_files(&files);
    aide_search_search(&files, word, 0, 1, 0, &g_searchMatches);
    aide_strlist_free(&files);

    populate_search_results();
}

static void do_find_in_project(void)
{
    char pattern[128];
    AideStrList files;

    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Kein Projekt geladen.");
        return;
    }
    if (!ask_string("Im Projekt suchen", "", pattern, sizeof(pattern)))
        return;

    set_status("Suche...");
    aide_strlist_init(&files);
    collect_project_files(&files);
    aide_search_search(&files, pattern, 0, 0, 0, &g_searchMatches);
    aide_strlist_free(&files);

    populate_search_results();
}

/* ------------------------------------------------------------------ */
/* Hothelp (F1), struct layout (Shift+F1), autodoc search (F2)           */
/* ------------------------------------------------------------------ */
static void do_help_f1(void)
{
    char word[128];
    if (!get_word_under_cursor(word, sizeof(word)))
        return;
    ensure_help_index_current();
    helpwindow_show_function(word);
}

static void do_help_shift_f1(void)
{
    char word[128];
    AideStr text;
    int found = 0;
    size_t i;
    char title[160];

    if (!get_word_under_cursor(word, sizeof(word)))
        return;

    aide_str_init(&text);
    aide_str_append(&text, word);
    aide_str_append(&text, "\n\n");

    for (i = 0; i < g_symbols.count; ++i) {
        const AideSymbol *s = &g_symbols.items[i];
        if (s->kind != AIDE_SYM_STRUCT_MEMBER || strcmp(s->scope, word) != 0)
            continue;
        found = 1;
        aide_str_appendf(&text, "  +%-4d %-24s %s\n", s->offset, s->signature, s->name);
    }
    if (!found)
        aide_str_append(&text, "(kein Struct/Union mit diesem Namen im Symbol-Index)\n");

    snprintf(title, sizeof(title), "AmigaIDE - Struct-Layout: %s", word);
    helpwindow_show_text(title, text.data);
    aide_str_free(&text);
}

static void do_help_f2(void)
{
    char pattern[128];
    if (!ask_string("Autodoc-Suche (Wildcard: * ?)", "", pattern, sizeof(pattern)))
        return;
    ensure_help_index_current();
    helpwindow_search_wildcard(pattern);
}

/* ------------------------------------------------------------------ */
/* Issue navigation (F4/Shift+F4, double-click)                          */
/* ------------------------------------------------------------------ */
static void push_issue(AideIssue *issue)
{
    if (g_issueCount >= g_issueCap) {
        size_t newCap = g_issueCap > 0 ? g_issueCap * 2 : 16;
        g_issues = (AideIssue *)realloc(g_issues, newCap * sizeof(AideIssue));
        g_issueCap = newCap;
    }
    g_issues[g_issueCount++] = *issue; /* transfers ownership of file/message */
}

static void clear_issues(void)
{
    size_t i;
    for (i = 0; i < g_issueCount; ++i)
        aide_issue_release(&g_issues[i]);
    g_issueCount = 0;
    g_issueActive = -1;
}

static void do_issue_navigate(int direction)
{
    if (g_issueCount == 0)
        return;
    if (g_issueActive < 0) {
        g_issueActive = 0;
    } else {
        g_issueActive += direction;
        if (g_issueActive < 0) g_issueActive = (long)g_issueCount - 1;
        if (g_issueActive >= (long)g_issueCount) g_issueActive = 0;
    }
    set(g_issuesList, MUIA_NList_Active, (ULONG)g_issueActive);
    goto_file_line(g_issues[g_issueActive].file, g_issues[g_issueActive].line);
}

/* ------------------------------------------------------------------ */
/* Build pipeline: async `make`, launched via SystemTagList()/Execute,   */
/* with a timer.device-driven tailer polling its growing log -- see     */
/* build_tick() below. Replaces the old always-rebuild-everything,      */
/* one-SystemTagList()-per-source-file, blocks-the-whole-GUI design.     */
/* ------------------------------------------------------------------ */

static void collect_active_sdk_includes(AideStrList *includeDirs, AideStrList *asmIncludeDirs, AideStrList *libDirs)
{
    AideStrList ids;
    size_t i, j;

    aide_strlist_init(&ids);
    for (i = 0; i < g_project.sdks.selected.count; ++i)
        aide_strlist_push(&ids, g_project.sdks.selected.items[i]);

    for (i = 0; i < ids.count; ++i) {
        const AideSdkDescriptor *sdk = aide_sdk_list_resolve(&g_sdkRegistry, ids.items[i]);
        if (!sdk)
            continue;
        for (j = 0; j < sdk->includeDirs.count; ++j)
            aide_strlist_push(includeDirs, sdk->includeDirs.items[j]);
        for (j = 0; j < sdk->asmIncludeDirs.count; ++j)
            aide_strlist_push(asmIncludeDirs, sdk->asmIncludeDirs.items[j]);
        for (j = 0; j < sdk->libDirs.count; ++j)
            aide_strlist_push(libDirs, sdk->libDirs.items[j]);
    }
    aide_strlist_free(&ids);
}

/* Parses one build-output line for a compiler/linker diagnostic and, if
 * found, inserts it into g_issuesList. Called once per complete line as
 * the build log grows (see build_tick()). */
static void append_output_line(const char *line)
{
    AideIssue issue;
    if (!aide_diag_parse_line(line, &issue))
        return;

    {
        AideStr row;
        /* vc/vasm/vlink report whatever path they were invoked with on
         * the generated Makefile's command line: relative ("src/main.c")
         * for project sources -- `make` now runs with its current
         * directory set to g_projectRoot, see launch_build_script() --
         * or absolute ("SDK:NDK_3.2/Include_H/foo.h") for an SDK header.
         * A ':' before the first '/' is the AmigaDOS convention for
         * "already absolute" (a volume/assign name); only the relative
         * case needs joining against g_projectRoot, so double-clicking
         * an issue reliably jumps to the right file either way. */
        const char *firstSlash = strchr(issue.file, '/');
        const char *firstColon = strchr(issue.file, ':');
        int alreadyAbsolute = firstColon != NULL && (!firstSlash || firstColon < firstSlash);
        if (!alreadyAbsolute) {
            char *real = join_path(g_projectRoot, issue.file);
            free(issue.file);
            issue.file = real;
        }
        aide_str_init(&row);
        aide_str_append(&row, issue.severity == AIDE_ISSUE_ERROR ? "ERROR " : "WARN  ");
        aide_str_append(&row, issue.file);
        if (issue.line > 0)
            aide_str_appendf(&row, ":%d", issue.line);
        aide_str_append(&row, "  ");
        aide_str_append(&row, issue.message);
        DoMethod(g_issuesList, MUIM_NList_InsertSingle, (ULONG)row.data, MUIV_NList_Insert_Bottom);
        aide_str_free(&row);
        push_issue(&issue); /* transfers ownership -- no aide_issue_release() here */
    }
}

static void populate_memory_map(const char *binaryPath)
{
    char *data;
    size_t size;
    AideHunkReader reader;
    size_t i;

    DoMethod(g_memmapList, MUIM_NList_Clear);

    if (!aide_fs_read_file(binaryPath, &data, &size))
        return;

    aide_hunkreader_init(&reader);
    if (aide_hunkreader_load_bytes(&reader, (const unsigned char *)data, size)) {
        for (i = 0; i < reader.sectionCount; ++i) {
            const AideHunkSection *sec = &reader.sections[i];
            const char *kind = sec->kind == AIDE_HUNK_CODE ? "CODE" : sec->kind == AIDE_HUNK_DATA ? "DATA" : "BSS";
            AideStr row;
            aide_str_init(&row);
            aide_str_appendf(&row, "Hunk %d  %-4s  %lu Bytes", sec->index, kind, sec->sizeBytes);
            DoMethod(g_memmapList, MUIM_NList_InsertSingle, (ULONG)row.data, MUIV_NList_Insert_Bottom);
            aide_str_free(&row);
        }
    }
    aide_hunkreader_free(&reader);
    free(data);
}

/* ------------------------------------------------------------------ */
/* Async build state                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    BUILD_IDLE = 0,
    BUILD_RUNNING,
    BUILD_STOPPING
} BuildState;

static struct {
    BuildState  state;
    long        offset;              /* bytes of logPath already consumed */
    AideLogTail tail;
    char        logPath[64];
    char        targetPath[AIDE_MAX_PATH];
    int         sawOk;                /* "--AIDE-BUILD-OK--" seen this build */
    int         isBuildTarget;         /* 1 = "all", 0 = "clean" -- gates post-build steps */
} g_build;

/* Make program name -- user-configurable in prefswindow.c (global
 * settings, not per-project: see its own header comment), read here via
 * prefs_make_program(). invalidate_make_availability() lets that window
 * force a re-check after the user changes it. */
static int g_makeAvailabilityChecked;
static int g_makeAvailable;

static void invalidate_make_availability(void)
{
    g_makeAvailabilityChecked = 0;
}

/* timer.device: opened once at startup (mainwindow_run()) and closed
 * once at shutdown; only *armed* (SendIO()'d) while a build is running.
 * Not a periodic/repeating request -- timer.device has none; every tick
 * is its own one-shot TR_ADDREQUEST, re-armed by build_tick() itself.
 * g_timerArmed tracks whether a request is currently outstanding --
 * AbortIO()/WaitIO() on a request that was never actually SendIO()'d
 * (e.g. the app quit without the user ever having built anything) is
 * NOT safe to assume a no-op, unlike aborting one that's genuinely
 * in-flight -- so build_timer_close() must only call them when this is
 * set. */
static struct MsgPort     *g_timerPort;
static struct timerequest *g_timerReq;
static ULONG                g_timerSig;
static int                  g_timerOpen;
static int                  g_timerArmed;

static int build_timer_open(void)
{
    g_timerPort = CreateMsgPort();
    if (!g_timerPort)
        return 0;
    g_timerReq = (struct timerequest *)CreateIORequest(g_timerPort, sizeof(struct timerequest));
    if (!g_timerReq) {
        DeleteMsgPort(g_timerPort);
        g_timerPort = NULL;
        return 0;
    }
    if (OpenDevice((CONST_STRPTR)"timer.device", UNIT_VBLANK, (struct IORequest *)g_timerReq, 0) != 0) {
        DeleteIORequest(g_timerReq);
        DeleteMsgPort(g_timerPort);
        g_timerReq = NULL;
        g_timerPort = NULL;
        return 0;
    }
    g_timerSig = 1UL << g_timerPort->mp_SigBit;
    g_timerOpen = 1;
    return 1;
}

static void build_timer_close(void)
{
    if (!g_timerOpen)
        return;
    if (g_timerArmed) {
        AbortIO((struct IORequest *)g_timerReq);
        WaitIO((struct IORequest *)g_timerReq);
        g_timerArmed = 0;
    }
    CloseDevice((struct IORequest *)g_timerReq);
    DeleteIORequest(g_timerReq);
    DeleteMsgPort(g_timerPort);
    g_timerReq = NULL;
    g_timerPort = NULL;
    g_timerSig = 0;
    g_timerOpen = 0;
}

static void build_timer_arm(void)
{
    if (!g_timerOpen || g_timerArmed)
        return;
    g_timerReq->tr_node.io_Command = TR_ADDREQUEST;
    g_timerReq->tr_time.tv_secs  = 0;
    g_timerReq->tr_time.tv_micro = 250000; /* 250ms tick */
    SendIO((struct IORequest *)g_timerReq);
    g_timerArmed = 1;
}

/* Writes <project>/build/vc.cfg -- the vc profile the generated
 * Makefile's CFLAGS references as "+build/vc.cfg" (see aide_makegen.c).
 * A real absolute path via join_path()/fopen(), like every other file
 * *we* write -- unlike vc's own command line, our own file I/O has never
 * had a problem with a space in g_projectRoot (AddPart()-based
 * join_path() is a normal AmigaDOS API, not an ad-hoc parser). */
static int write_vc_cfg(void)
{
    char vccfgPath[AIDE_MAX_PATH];
    char *text;
    FILE *f;
    int ok;

    join_path_into(vccfgPath, sizeof(vccfgPath), g_projectRoot, "build/vc.cfg");
    ensure_parent_dir(vccfgPath);

    /* Explicit AMIGADOS, not aide_vccfg_generate()'s __M68K__-inferred
     * default: this file only ever runs compiled for and executing on
     * AmigaOS, so there's no reason to leave that to a macro guess --
     * see aide_vccfg_generate_dialect()'s own doc comment. */
    text = aide_vccfg_generate_dialect(&g_toolchain, &g_project, AIDE_MAKE_DIALECT_AMIGADOS);
    f = fopen(vccfgPath, "wb");
    ok = f != NULL;
    if (f) {
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }
    free(text);
    return ok;
}

/* Writes <project>/build/Makefile (and build/__stack.c, if the project
 * needs one -- see aide_makegen_needs_stack_object(), previously never
 * generated on the Amiga side at all) via the shared core/ generator,
 * mirroring MakefileGenerator::writeToProject() on the Qt side. Always
 * AIDE_MAKE_DIALECT_AMIGADOS explicitly -- NOT aide_makegen_generate()'s
 * __M68K__-inferred default: this file only ever runs compiled for and
 * executing on AmigaOS, so the dialect is already known here and
 * doesn't need guessing from a macro (see aide_makegen.h's dialect doc
 * comment). `project_root` is passed for API symmetry with the Qt call
 * site but unused by the AMIGADOS branch -- every project-internal path
 * comes out relative, not joined against it at all. */
static int write_build_makefile(void)
{
    char makefilePath[AIDE_MAX_PATH];
    char *text;
    FILE *f;
    int ok;

    if (aide_makegen_needs_stack_object(&g_project)) {
        char stackPath[AIDE_MAX_PATH];
        char *stackText = aide_makegen_stack_object_source(g_project.program.stackSize);
        FILE *sf;
        join_path_into(stackPath, sizeof(stackPath), g_projectRoot, "build/__stack.c");
        ensure_parent_dir(stackPath);
        sf = fopen(stackPath, "wb");
        if (sf) { fwrite(stackText, 1, strlen(stackText), sf); fclose(sf); }
        free(stackText);
    }

    join_path_into(makefilePath, sizeof(makefilePath), g_projectRoot, "build/Makefile");
    ensure_parent_dir(makefilePath);

    text = aide_makegen_generate_dialect(&g_toolchain, &g_project, g_projectRoot, &g_sdkRegistry,
                                          AIDE_MAKE_DIALECT_AMIGADOS);
    f = fopen(makefilePath, "wb");
    ok = f != NULL;
    if (f) {
        fwrite(text, 1, strlen(text), f);
        fclose(f);
    }
    free(text);
    return ok;
}

/* Writes T:aide-build.script -- see launch_build_script(). FailAt 21 is
 * not optional: default FAILAT is 10, and a tool returning an RC in
 * [10,21) would abort the script before the sentinel Echo ever runs,
 * hanging the tailer forever (every vbcc-tool RC seen in practice stays
 * below 21). The "--AIDE-BUILD-DONE--" sentinel's "RC=$RC" is the
 * primary success signal (build_tick() parses it); the Makefile's own
 * "--AIDE-BUILD-OK--" (aide_makegen.c) is the fallback for if $RC
 * somehow didn't expand -- unverified on real hardware either way, see
 * the plan doc's "Ungeprueft" list. */
static int write_make_script(void)
{
    const char *scriptPath = "T:aide-build.script";
    AideStr s;
    BPTR fh;

    aide_str_init(&s);
    aide_str_append(&s, "FailAt 21\n");
    aide_str_append(&s, prefs_make_program());
    aide_str_append(&s, " -f build/Makefile \"VBCC=");
    aide_str_append(&s, g_toolchain.root);
    aide_str_append(&s, "\" \"E=");
    aide_str_append(&s, aide_toolchain_exe_suffix());
    aide_str_append(&s, "\" ");
    /* VBCC/E passed explicitly on the command line, not left to the
     * Makefile's own "VBCC ?= ..." default -- a real AmigaDOS "VBCC"
     * environment variable (global or local, empty or otherwise) would
     * make GNU make treat that variable as already set and skip the
     * Makefile's own default entirely, silently turning every
     * $(VBCC)-prefixed tool path into a bare, unqualified "bin/vc" that
     * AmigaDOS can't find ("Unbekannter Befehl") -- confirmed via a real
     * build log. A command-line-set variable always wins over both
     * "?=" and a plain "=" in the Makefile, so this is unconditionally
     * correct regardless of whatever "VBCC" the environment happens to
     * already define. Mirrors the Qt side's BuildManager::startMake(),
     * which has always passed these the same way. */
    aide_str_append(&s, g_build.isBuildTarget ? "all" : "clean");
    aide_str_append_char(&s, '\n');
    aide_str_append(&s, "Echo \"--AIDE-BUILD-DONE-- RC=$RC\"\n");

    fh = Open((CONST_STRPTR)scriptPath, MODE_NEWFILE);
    if (!fh) {
        aide_str_free(&s);
        return 0;
    }
    Write(fh, (APTR)s.data, (LONG)s.len);
    Close(fh);
    aide_str_free(&s);
    return 1;
}

/* Cached per session (not per build) -- checked once, reused after
 * that; prefswindow.c's Make-Programm field calls
 * invalidate_make_availability() when the user changes it. System()
 * returning -1 is dos.library's documented way of saying "the command
 * could not be found or loaded", distinct from "found but exited
 * non-zero". */
static int make_is_available(void)
{
    BPTR nilIn, nilOut;
    struct TagItem tags[5];
    AideStr cmd;
    LONG rc;

    if (g_makeAvailabilityChecked)
        return g_makeAvailable;
    g_makeAvailabilityChecked = 1;

    nilIn  = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    nilOut = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);

    aide_str_init(&cmd);
    aide_str_append(&cmd, prefs_make_program());
    aide_str_append(&cmd, " -v");

    tags[0].ti_Tag = SYS_Input;  tags[0].ti_Data = (ULONG)nilIn;
    tags[1].ti_Tag = SYS_Output; tags[1].ti_Data = (ULONG)nilOut;
    tags[2].ti_Tag = SYS_Asynch; tags[2].ti_Data = FALSE;
    tags[3].ti_Tag = NP_WindowPtr; tags[3].ti_Data = (ULONG)-1; /* see launch_build_script() */
    tags[4].ti_Tag = TAG_DONE;   tags[4].ti_Data = 0;

    rc = SystemTagList((CONST_STRPTR)cmd.data, tags);
    aide_str_free(&cmd);

    /* A synchronous SystemTagList() does not close SYS_Input/SYS_Output
     * itself (dos.library/SystemTagList autodoc) -- ours to close
     * either way. */
    if (nilIn)  Close(nilIn);
    if (nilOut) Close(nilOut);

    g_makeAvailable = (rc != -1);
    return g_makeAvailable;
}

/* The one SystemTagList() call per build: launches "Execute
 * T:aide-build.script" asynchronously, with NP_CurrentDir set to
 * g_projectRoot so the AmigaDOS-dialect Makefile's project-relative
 * paths resolve without ever needing an absolute, possibly
 * space-containing project path on vc's/vasm's/vlink's command line
 * (see aide_makegen.h's dialect doc comment -- this replaces the old
 * AIDEBUILD: assign entirely). NP_CurrentDir is a documented
 * CreateNewProc() tag that SystemTagList() passes straight through (it
 * only filters NP_Seglist/NP_FreeSeglist/NP_Entry/NP_Input/NP_Output/
 * NP_CloseInput/NP_CloseOutput/NP_HomeDir/NP_Cli -- dos.library
 * autodocs); ownership of the lock transfers to the new process on
 * success as usual (freed when it exits), same as nilIn/logOut below --
 * only on failure (rc == -1) is any of it still ours to release. */
static int launch_build_script(void)
{
    BPTR nilIn, logOut, cwd;
    struct TagItem tags[6];
    LONG rc;

    nilIn = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);

    /* MODE_READWRITE + seek-to-end, NOT MODE_NEWFILE (exclusive) -- our
     * own tailer opens the same file MODE_OLDFILE (shared) while this
     * is still being written to; MODE_NEWFILE would lock it out for the
     * entire build. */
    logOut = Open((CONST_STRPTR)g_build.logPath, MODE_READWRITE);
    if (logOut)
        Seek(logOut, 0, OFFSET_END);

    cwd = Lock((CONST_STRPTR)g_projectRoot, ACCESS_READ);

    tags[0].ti_Tag = SYS_Input;     tags[0].ti_Data = (ULONG)nilIn;
    tags[1].ti_Tag = SYS_Output;    tags[1].ti_Data = (ULONG)logOut;
    tags[2].ti_Tag = SYS_Asynch;    tags[2].ti_Data = TRUE;
    tags[3].ti_Tag = NP_CurrentDir; tags[3].ti_Data = (ULONG)cwd;
    /* No "please insert volume" requesters from the build: both Amiga
     * ports of GNU make look for a default include drawer at startup
     * (make 3.76.1 from dev/c/make_bin.lha for "mkinclude:", make 4.4.1
     * from dev/c/make-4.4.1.lha for "include:"), which put up a
     * requester on every single build where that assign is missing.
     * With pr_WindowPtr -1 such a lookup just fails, as it should; a
     * missing SDK volume then shows up as a compiler error in the log
     * instead of a requester. */
    tags[4].ti_Tag = NP_WindowPtr;  tags[4].ti_Data = (ULONG)-1;
    tags[5].ti_Tag = TAG_DONE;      tags[5].ti_Data = 0;

    rc = SystemTagList((CONST_STRPTR)"Execute T:aide-build.script", tags);
    if (rc == -1) {
        /* Launch failed outright -- nothing was ever handed off to the
         * (nonexistent) child, so all three are still ours. */
        if (nilIn) Close(nilIn);
        if (logOut) Close(logOut);
        if (cwd) UnLock(cwd);
        return 0;
    }
    return 1;
}

static size_t count_error_issues(void)
{
    size_t i, n = 0;
    for (i = 0; i < g_issueCount; ++i)
        if (g_issues[i].severity == AIDE_ISSUE_ERROR)
            ++n;
    return n;
}

/* Greys out Build/Clean/Run while a build is running (enabled=FALSE) and
 * ungreys them again once it finishes (enabled=TRUE); "Build abbrechen"
 * is the mirror image of that. The reentry guard at the top of
 * do_build()/do_clean() (g_build.state != BUILD_IDLE) is the actual
 * correctness mechanism -- this is purely so the menu doesn't invite a
 * click it would just reject with a status-bar message. */
static void set_build_menus_enabled(int enabled)
{
    Object *mi;

    mi = (Object *)DoMethod(g_menustrip, MUIM_FindUData, ID_BUILD);
    if (mi) set(mi, MUIA_Menuitem_Enabled, enabled ? TRUE : FALSE);
    mi = (Object *)DoMethod(g_menustrip, MUIM_FindUData, ID_CLEAN);
    if (mi) set(mi, MUIA_Menuitem_Enabled, enabled ? TRUE : FALSE);
    mi = (Object *)DoMethod(g_menustrip, MUIM_FindUData, ID_RUN);
    if (mi) set(mi, MUIA_Menuitem_Enabled, enabled ? TRUE : FALSE);
    mi = (Object *)DoMethod(g_menustrip, MUIM_FindUData, ID_BUILD_STOP);
    if (mi) set(mi, MUIA_Menuitem_Enabled, enabled ? FALSE : TRUE);
}

static void build_finished(int ok)
{
    int stopped = (g_build.state == BUILD_STOPPING);
    g_build.state = BUILD_IDLE;
    aide_logtail_free(&g_build.tail);
    set_build_menus_enabled(1);

    if (stopped) {
        set_status("Build abgebrochen");
        return;
    }

    if (!g_build.isBuildTarget) {
        /* "clean" -- no post-build steps at all. */
        set_status(ok ? "Aufger\344umt" : "Aufr\344umen fehlgeschlagen");
        return;
    }

    if (ok) {
        populate_memory_map(g_build.targetPath);
        rebuild_symbol_index();

        /* Workbench-launched programs benefit from a real .info icon so
         * they can be double-clicked from Workbench; CLI programs are
         * started from a Shell/prompt and have no meaningful icon, so
         * this is deliberately skipped for those -- per user feedback
         * ("CLI Programme haben KEIN Icon"). */
        if (g_project.program.type == AIDE_PROGRAM_WB) {
            char iconPath[AIDE_MAX_PATH];
            int iconOk;
            snprintf(iconPath, sizeof(iconPath), "%s.info", g_build.targetPath);
            if (g_project.program.iconMode == AIDE_ICON_FILE && g_project.program.iconPath[0]) {
                char sourceIconPath[AIDE_MAX_PATH];
                join_path_into(sourceIconPath, sizeof(sourceIconPath), g_projectRoot, g_project.program.iconPath);
                iconOk = aide_icon_copy(sourceIconPath, iconPath, 1, g_project.program.stackSize);
            } else {
                iconOk = aide_icon_write_default(iconPath, g_project.program.stackSize);
            }
            if (!iconOk) {
                MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                            "Konnte Icon nicht erzeugen:\n%s", iconPath);
            }
        }
    }
    set_status(ok ? "Build erfolgreich" : "Build fehlgeschlagen -- siehe Issues");
}

typedef struct {
    AideStr uiBuf;       /* complete lines from this tick, newline-joined */
    int     sawOk;
    int     sawDone;
    int     doneRcValid;
    int     doneRc;
} BuildTickCtx;

static void build_tick_line_cb(const char *line, void *userdata)
{
    BuildTickCtx *ctx = (BuildTickCtx *)userdata;

    aide_str_append(&ctx->uiBuf, line);
    aide_str_append_char(&ctx->uiBuf, '\n');

    if (strncmp(line, "--AIDE-BUILD-OK--", 17) == 0) {
        ctx->sawOk = 1;
        return; /* our own sentinel, not a compiler diagnostic */
    }
    if (strncmp(line, "--AIDE-BUILD-DONE--", 19) == 0) {
        const char *rcPos = strstr(line, "RC=");
        ctx->sawDone = 1;
        if (rcPos && rcPos[3] >= '0' && rcPos[3] <= '9') {
            ctx->doneRcValid = 1;
            ctx->doneRc = atoi(rcPos + 3);
        }
        return;
    }

    append_output_line(line);
}

/* Fired off timer.device's periodic signal (see the main loop in
 * mainwindow_run()) while g_build.state != BUILD_IDLE. Reads whatever's
 * newly appended to the log since the last tick, splits it into
 * complete lines (aide_logtail -- holds back a trailing partial line,
 * see its own header), batches them into ONE
 * MUIM_TextEditor_InsertText call (not one per line -- each call forces
 * a redraw) and scrolls to the bottom, then checks for the
 * "--AIDE-BUILD-DONE--" sentinel to end the build.
 *
 * Effective granularity is "one source file at a time", not truly
 * line-by-line: vbcc-tool stdio is block-buffered once Output() is a
 * file rather than a console, so a tool's own diagnostics all appear
 * together when it exits; only `make`'s own recipe-echo lines (which it
 * fflushes before spawning each one) arrive promptly. Still far better
 * than the old "nothing until the whole build ends". */
static void build_tick(void)
{
    char *chunk;
    size_t n;
    BuildTickCtx ctx;

    if (g_build.state == BUILD_IDLE)
        return; /* stray tick after teardown -- nothing to do */

    if (!aide_fs_read_range(g_build.logPath, g_build.offset, 16384, &chunk, &n)) {
        build_timer_arm();
        return;
    }

    aide_str_init(&ctx.uiBuf);
    ctx.sawOk = 0;
    ctx.sawDone = 0;
    ctx.doneRcValid = 0;
    ctx.doneRc = 0;

    if (n > 0) {
        g_build.offset += (long)n;
        aide_logtail_feed(&g_build.tail, chunk, n, build_tick_line_cb, &ctx);
    }
    free(chunk);

    if (ctx.sawOk)
        g_build.sawOk = 1;

    if (ctx.uiBuf.len > 0) {
        DoMethod(g_outputEditor, MUIM_TextEditor_InsertText, (ULONG)ctx.uiBuf.data, MUIV_TextEditor_InsertText_Bottom);
        {
            ULONG entries = 0, visible = 0;
            get(g_outputEditor, MUIA_TextEditor_Prop_Entries, &entries);
            get(g_outputEditor, MUIA_TextEditor_Prop_Visible, &visible);
            set(g_outputEditor, MUIA_TextEditor_Prop_First, entries > visible ? entries - visible : 0);
        }
    }
    aide_str_free(&ctx.uiBuf);

    if (ctx.sawDone) {
        int ok;
        if (ctx.doneRcValid) {
            ok = (ctx.doneRc == 0);
        } else if (g_build.sawOk) {
            /* $RC didn't expand in the script -- fall back to the
             * Makefile's own "all" recipe sentinel, which needs no
             * shell variables at all. */
            ok = 1;
        } else {
            /* Neither sentinel fired (e.g. a script-level failure
             * before make even ran) -- last-resort corroboration: does
             * the target exist and did we see zero ERROR-severity
             * issues? */
            long size; long long mtime;
            ok = count_error_issues() == 0
                && (!g_build.isBuildTarget || aide_fs_stat(g_build.targetPath, &size, &mtime));
        }
        build_finished(ok);
        return; /* do not re-arm -- the tailer's job is done */
    }

    build_timer_arm();
}

static void do_build(void)
{
    if (g_build.state != BUILD_IDLE) {
        set_status("Build l\344uft bereits");
        return;
    }
    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Bitte zuerst Projekt speichern.");
        return;
    }
    if (!g_timerOpen) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Timer-Ger\344t konnte nicht ge\366ffnet werden -- Build nicht m\366glich.");
        return;
    }
    if (!make_is_available()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "\"%s\" wurde nicht gefunden.\n\n"
                    "AmigaIDE braucht GNU make im Pfad (z.B. C:make).\n"
                    "Aminet: dev/c/make_bin.lha (GNU make 3.76.1)\n"
                    "oder dev/c/make-4.4.1.lha (GNU make 4.4.1);\n"
                    "die Datei make daraus nach C: kopieren.\n"
                    "Ein anderer Name/Pfad: Projekt > Einstellungen.",
                    prefs_make_program());
        return;
    }

    /* A space in the project ROOT is no longer a problem (NP_CurrentDir,
     * see launch_build_script()), but a space in an individual source
     * FILENAME still is -- no make dialect can represent that as a
     * target/prerequisite token (see aide_makegen.h). Caught up front,
     * with the exact offending files named, instead of silently
     * mis-generating a Makefile that then fails in some confusing way. */
    {
        AideStr offenders;
        size_t i;
        int haveOffenders = 0;
        aide_str_init(&offenders);
        for (i = 0; i < g_project.sources.count; ++i) {
            const char *relSrc = g_project.sources.items[i];
            const char *base = strrchr(relSrc, '/');
            base = base ? base + 1 : relSrc;
            if (strchr(base, ' ')) {
                if (haveOffenders) aide_str_append_char(&offenders, '\n');
                aide_str_append(&offenders, relSrc);
                haveOffenders = 1;
            }
        }
        if (haveOffenders) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Diese Quelldateien enthalten ein Leerzeichen im Dateinamen "
                        "und k\366nnen nicht gebaut werden -- bitte umbenennen:\n\n%s",
                        offenders.data);
            aide_str_free(&offenders);
            return;
        }
        aide_str_free(&offenders);
    }

    set_status("Baue...");

    /* Clear Ausgabe/Issues/Memory-Map right away, not just after the
     * build finishes -- otherwise stale results from the *previous*
     * build stay on screen for the whole (possibly slow, on real
     * hardware) duration of this one, making it look like nothing
     * happened yet or, worse, like the new build produced the old
     * results. */
    clear_issues();
    DoMethod(g_issuesList, MUIM_NList_Clear);
    DoMethod(g_outputEditor, MUIM_TextEditor_ClearText);
    DoMethod(g_memmapList, MUIM_NList_Clear);

    {
        AideStr list, msg;
        aide_str_init(&list);
        if (missing_sdks(&list) > 0) {
            aide_str_init(&msg);
            aide_str_append(&msg, "Warnung: nicht installierte SDKs, ihre Pfade fehlen im Build:\n");
            aide_str_append(&msg, list.data);
            DoMethod(g_outputEditor, MUIM_TextEditor_InsertText, (ULONG)msg.data, MUIV_TextEditor_InsertText_Bottom);
            aide_str_free(&msg);
        }
        aide_str_free(&list);
    }

    /* Build must always compile what's actually in the editor, not
     * whatever was last written to disk -- otherwise editing a file and
     * hitting Build without an explicit Speichern first silently
     * compiles the stale (possibly still-empty) on-disk version. */
    if (g_currentFile[0])
        editor_save_current();

    {
        char objDir[AIDE_MAX_PATH], binDir[AIDE_MAX_PATH];
        join_path_into(objDir, sizeof(objDir), g_projectRoot, g_project.output.objDir);
        join_path_into(binDir, sizeof(binDir), g_projectRoot, g_project.output.targetDir);
        aide_fs_mkpath(objDir);
        aide_fs_mkpath(binDir);
        join_path_into(g_build.targetPath, sizeof(g_build.targetPath), binDir, g_project.output.binary);
    }

    if (!write_vc_cfg()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte vc.cfg nicht anlegen.");
        set_status("Build fehlgeschlagen");
        return;
    }

    g_build.isBuildTarget = 1;
    if (!write_build_makefile() || !write_make_script()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte Build-Dateien nicht anlegen.");
        set_status("Build fehlgeschlagen");
        return;
    }

    /* MODE_NEWFILE always creates a fresh file for writing regardless of
     * whether one already existed (dos.library/Open), so an explicit
     * DeleteFile() first is redundant. T:, not RAM: root -- T: is
     * AmigaDOS's own standard assign for exactly this kind of throwaway
     * temp file, per user feedback ("temporaeres Zeug gehoert nach T:"). */
    strcpy(g_build.logPath, "T:aide-build.out");
    {
        BPTR truncFh = Open((CONST_STRPTR)g_build.logPath, MODE_NEWFILE);
        if (!truncFh) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Konnte Log nicht neu anlegen (IoErr %ld):\n%s",
                        (long)IoErr(), g_build.logPath);
            set_status("Build fehlgeschlagen");
            return;
        }
        Close(truncFh);
    }

    g_build.offset = 0;
    g_build.sawOk = 0;
    aide_logtail_init(&g_build.tail);

    if (!launch_build_script()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Build konnte nicht gestartet werden.");
        aide_logtail_free(&g_build.tail);
        set_status("Build fehlgeschlagen");
        return;
    }

    g_build.state = BUILD_RUNNING;
    set_build_menus_enabled(0);
    build_timer_arm();
}

/* `make clean` -- shares the whole launcher machinery with do_build()
 * (same script/log/timer/tailer), just with isBuildTarget=0 so
 * build_finished() skips the post-build steps (memory map, symbol
 * reindex, icon) that only make sense after an actual link. No vc.cfg
 * needed: clean only deletes files, it never invokes vc/vasm. */
static void do_clean(void)
{
    if (g_build.state != BUILD_IDLE) {
        set_status("Build l\344uft bereits");
        return;
    }
    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Bitte zuerst Projekt speichern.");
        return;
    }
    if (!g_timerOpen) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Timer-Ger\344t konnte nicht ge\366ffnet werden -- Aufr\344umen nicht m\366glich.");
        return;
    }
    if (!make_is_available()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "\"%s\" wurde nicht gefunden.\n\n"
                    "AmigaIDE braucht GNU make im Pfad (z.B. C:make).\n"
                    "Aminet: dev/c/make_bin.lha (GNU make 3.76.1)\n"
                    "oder dev/c/make-4.4.1.lha (GNU make 4.4.1);\n"
                    "die Datei make daraus nach C: kopieren.\n"
                    "Ein anderer Name/Pfad: Projekt > Einstellungen.",
                    prefs_make_program());
        return;
    }

    set_status("R\344ume auf...");
    clear_issues();
    DoMethod(g_issuesList, MUIM_NList_Clear);
    DoMethod(g_outputEditor, MUIM_TextEditor_ClearText);
    DoMethod(g_memmapList, MUIM_NList_Clear);

    g_build.isBuildTarget = 0;
    if (!write_build_makefile() || !write_make_script()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte Build-Dateien nicht anlegen.");
        set_status("Aufr\344umen fehlgeschlagen");
        return;
    }

    strcpy(g_build.logPath, "T:aide-build.out");
    {
        BPTR truncFh = Open((CONST_STRPTR)g_build.logPath, MODE_NEWFILE);
        if (!truncFh) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Konnte Log nicht neu anlegen (IoErr %ld):\n%s",
                        (long)IoErr(), g_build.logPath);
            set_status("Aufr\344umen fehlgeschlagen");
            return;
        }
        Close(truncFh);
    }

    g_build.offset = 0;
    g_build.sawOk = 0;
    aide_logtail_init(&g_build.tail);

    if (!launch_build_script()) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Aufr\344umen konnte nicht gestartet werden.");
        aide_logtail_free(&g_build.tail);
        set_status("Aufr\344umen fehlgeschlagen");
        return;
    }

    g_build.state = BUILD_RUNNING;
    set_build_menus_enabled(0);
    build_timer_arm();
}

/* Stufe 1 (the only tier implemented): flags the running build as
 * "please stop" and lets build_finished() report it that way once the
 * sentinel arrives -- the tailer keeps consuming the log until then, so
 * g_build.state doesn't go stale and a new build isn't allowed to start
 * out from under the still-running one. The child process itself is
 * NOT killed (SystemTagList() gives no handle to do that with; a
 * best-effort Status/Break-based Stufe 2 is a possible future addition,
 * not implemented here) -- it keeps compiling/linking to completion in
 * the background, same as if the user had quit the whole app instead. */
static void do_build_stop(void)
{
    AideStr msg;

    if (g_build.state != BUILD_RUNNING)
        return;

    g_build.state = BUILD_STOPPING;
    aide_str_init(&msg);
    aide_str_append(&msg, "-- Abbruch angefordert --\n");
    DoMethod(g_outputEditor, MUIM_TextEditor_InsertText, (ULONG)msg.data, MUIV_TextEditor_InsertText_Bottom);
    aide_str_free(&msg);
    set_status("Abbruch angefordert...");
}

static void do_run(void)
{
    char targetPath[AIDE_MAX_PATH];
    AideStr cmd;
    BPTR con = 0;
    LONG rc;
    struct TagItem tags[4];
    int nTags = 0;

    if (!g_projectRoot[0]) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Kein Projekt geladen.");
        return;
    }
    {
        char binDir[AIDE_MAX_PATH];
        join_path_into(binDir, sizeof(binDir), g_projectRoot, g_project.output.targetDir);
        join_path_into(targetPath, sizeof(targetPath), binDir, g_project.output.binary);
    }

    if (!aide_fs_exists(targetPath)) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Programm nicht gefunden -- bitte zuerst bauen:\n%s", targetPath);
        return;
    }

    aide_str_init(&cmd);
    aide_str_appendf(&cmd, "\"%s\"", targetPath);

    if (g_project.program.type == AIDE_PROGRAM_CLI) {
        /* CLI programs read/write stdio -- give them a fresh console
         * window so their output is actually visible, regardless of
         * whether AmigaIDE itself has one (Workbench-launched apps
         * don't). SYS_Output NULL makes the shell default Output() to
         * the same window passed as SYS_Input (dos.library/
         * SystemTagList). /WAIT keeps the window open (waiting for a
         * keypress/close) after the program exits and we Close() our
         * own handle on it below -- without it, a program that just
         * prints one line and returns closes its console again
         * immediately, too fast to actually read (per user feedback);
         * confirmed working exactly this way in AmiSSL's own
         * autoinit_amissl_main.c ("CON://///AUTO/CLOSE/WAIT"). */
        con = Open((CONST_STRPTR)"CON:20/20/640/200/AmigaIDE Run/CLOSE/WAIT", MODE_NEWFILE);
        if (!con) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte Konsolenfenster nicht \366ffnen.");
            aide_str_free(&cmd);
            return;
        }
        tags[nTags].ti_Tag = SYS_Input;  tags[nTags].ti_Data = (ULONG)con;  ++nTags;
        tags[nTags].ti_Tag = SYS_Output; tags[nTags].ti_Data = (ULONG)NULL; ++nTags;
    }
    /* Workbench-type programs open their own screen/window via
     * Intuition and don't touch CLI stdio at all -- forcing a console on
     * them would just be a pointless flash of an empty window. Omitting
     * the SYS_Input/SYS_Output tags entirely (not passing them as NULL --
     * a real, documented difference) makes the new process inherit
     * AmigaIDE's own Input()/Output(), whatever those are, exactly as if
     * "Run" had never touched stdio in the first place. */
    tags[nTags].ti_Tag = SYS_Asynch; tags[nTags].ti_Data = FALSE; ++nTags;
    tags[nTags].ti_Tag = TAG_DONE;   tags[nTags].ti_Data = 0;    ++nTags;

    rc = SystemTagList((CONST_STRPTR)cmd.data, tags);
    aide_str_free(&cmd);

    /* A synchronous (non-SYS_Asynch) SystemTagList() does NOT close
     * SYS_Input/SYS_Output itself -- "you must close them (if needed)
     * after System returns" (dos.library/SystemTagList). SYS_Output was
     * NULL, so `con` (only opened for the CLI case) is the only handle
     * left to close. */
    if (con)
        Close(con);

    if (rc != 0) {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Programm beendet mit R\374ckgabewert %ld.", (long)rc);
    }
}

static void do_disassemble(void)
{
    char path[AIDE_MAX_PATH];
    char binDir[AIDE_MAX_PATH];
    const char *initialDir = NULL;

    if (g_projectRoot[0]) {
        join_path_into(binDir, sizeof(binDir), g_projectRoot, g_project.output.targetDir);
        initialDir = binDir;
    }
    if (!ask_file("Zu disassemblierende Datei", 0, initialDir, path, sizeof(path)))
        return;
    set_status("Disassembliere...");
    disasmwindow_show(path);
    set_status("Bereit");
}

/* Exports the Ausgabe pane's current text (everything appended from the
 * last build's log so far, see build_tick()) to a file the user picks --
 * so a build log can actually be kept/copied elsewhere instead of only
 * ever being visible in the scrollback. */
static void do_save_output(void)
{
    char path[AIDE_MAX_PATH];
    STRPTR text;
    FILE *f;

    if (!ask_file("Ausgabe speichern unter", 1, NULL, path, sizeof(path)))
        return;

    text = (STRPTR)DoMethod(g_outputEditor, MUIM_TextEditor_ExportText);
    if (!text) {
        set_status("Nichts zu speichern");
        return;
    }

    f = fopen(path, "wb");
    if (f) {
        fwrite(text, 1, strlen((char *)text), f);
        fclose(f);
        set_status("Ausgabe gespeichert");
    } else {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Konnte Ausgabe nicht speichern:\n%s", path);
        set_status("Fehlgeschlagen");
    }
    FreeVec(text);
}

/* "Formatieren": batch brace-depth reindent of the whole open file --
 * see aide_reindent.h for why this is a batch menu action rather than
 * live-while-typing on this platform (TextEditor.mcc has no hook to
 * intercept/override what Enter does), per user feedback. */
static void do_format_current_file(void)
{
    STRPTR text;
    char *reindented;

    if (!g_currentFile[0]) {
        set_status("Keine Datei offen");
        return;
    }

    text = (STRPTR)DoMethod(g_editor, MUIM_TextEditor_ExportText);
    if (!text) {
        set_status("Nichts zu formatieren");
        return;
    }

    reindented = aide_reindent_braces((char *)text, "    ");
    FreeVec(text);

    DoMethod(g_editor, MUIM_TextEditor_ClearText);
    DoMethod(g_editor, MUIM_TextEditor_InsertText, (ULONG)reindented, MUIV_TextEditor_InsertText_Top);
    free(reindented);

    set(g_editor, MUIA_TextEditor_HasChanged, TRUE);
    refresh_tree();
    set_status("Formatiert -- noch nicht gespeichert");
}

/* ------------------------------------------------------------------ */
/* Menu                                                                   */
/* ------------------------------------------------------------------ */
static struct NewMenu MenuData[] = {
    { NM_TITLE, "Projekt", 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Neu...",     "N", 0, 0, (APTR)ID_NEW_PROJECT },
    { NM_ITEM,  "\326ffnen...", "O", 0, 0, (APTR)ID_OPEN_PROJECT },
    { NM_ITEM,  "Speichern",  "S", 0, 0, (APTR)ID_SAVE },
    { NM_ITEM,  "Neue Datei", 0, 0, 0, (APTR)0 },
    { NM_SUB,   "C-Quelle...",      0, 0, 0, (APTR)ID_ADD_NEW_C_SOURCE },
    { NM_SUB,   "C-Header...",      0, 0, 0, (APTR)ID_ADD_NEW_C_HEADER },
    { NM_SUB,   "ASM-Quelle...",    0, 0, 0, (APTR)ID_ADD_NEW_ASM_SOURCE },
    { NM_SUB,   "ASM-Include...",   0, 0, 0, (APTR)ID_ADD_NEW_ASM_INCLUDE },
    { NM_SUB,   "BASIC-Quelle...",  0, 0, 0, (APTR)ID_ADD_NEW_BASIC_SOURCE },
    { NM_ITEM,  "Bestehende Datei hinzuf\374gen...", "E", 0, 0, (APTR)ID_ADD_EXISTING_FILE },
    { NM_ITEM,  "Datei aus Projekt entfernen...", 0, 0, 0, (APTR)ID_REMOVE_FILE },
    { NM_ITEM,  "Projekteinstellungen...", "I", 0, 0, (APTR)ID_PROJECT_SETTINGS },
    { NM_ITEM,  "Einstellungen...", 0, 0, 0, (APTR)ID_PREFS },
    { NM_ITEM,  "Installation pr\374fen...", 0, 0, 0, (APTR)ID_CHECK_SDKS },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Im Projekt suchen...", "F", 0, 0, (APTR)ID_FIND_IN_PROJECT },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Beenden", "Q", 0, 0, (APTR)ID_QUIT },
    { NM_TITLE, "Build", 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Build",  "B", 0, 0, (APTR)ID_BUILD },
    { NM_ITEM,  "Clean",  0, 0, 0, (APTR)ID_CLEAN },
    { NM_ITEM,  "Build abbrechen", 0, 0, 0, (APTR)ID_BUILD_STOP },
    { NM_ITEM,  "Run",    "R", 0, 0, (APTR)ID_RUN },
    { NM_ITEM,  "Disassemblieren...", "D", 0, 0, (APTR)ID_DISASSEMBLE },
    { NM_ITEM,  "Ausgabe speichern...", 0, 0, 0, (APTR)ID_SAVE_OUTPUT },
    { NM_ITEM,  "Formatieren (Einr\374ckung)", 0, 0, 0, (APTR)ID_FORMAT },
    { NM_TITLE, "Gehe zu", 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Definition (F3)",         0, 0, 0, (APTR)ID_GOTO_DEF },
    { NM_ITEM,  "Referenzen (Shift+F3)",   0, 0, 0, (APTR)ID_FIND_REFS },
    { NM_ITEM,  "N\344chster Issue (F4)",    0, 0, 0, (APTR)ID_ISSUE_NEXT },
    { NM_ITEM,  "Voriger Issue (Shift+F4)",0, 0, 0, (APTR)ID_ISSUE_PREV },
    { NM_TITLE, "Versionen", 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Repository anlegen",          0, 0, 0, (APTR)ID_VCS_INIT },
    { NM_ITEM,  "Aktualisieren",               0, 0, 0, (APTR)ID_VCS_REFRESH },
    { NM_ITEM,  "Alles bereitstellen",         0, 0, 0, (APTR)ID_VCS_STAGE_ALL },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Neuer Branch...",             0, 0, 0, (APTR)ID_VCS_NEWBRANCH },
    { NM_ITEM,  "Branch wechseln...",          0, 0, 0, (APTR)ID_VCS_SWITCH },
    { NM_ITEM,  "Branch zusammenf\374hren...", 0, 0, 0, (APTR)ID_VCS_MERGE },
    { NM_ITEM,  "Konflikt: unsere \374bernehmen", 0, 0, 0, (APTR)ID_VCS_TAKE_OURS },
    { NM_ITEM,  "Konflikt: ihre \374bernehmen",   0, 0, 0, (APTR)ID_VCS_TAKE_THEIRS },
    { NM_ITEM,  "Merge abbrechen",             0, 0, 0, (APTR)ID_VCS_MERGE_ABORT },
    { NM_ITEM,  "Diff der Auswahl",            0, 0, 0, (APTR)ID_VCS_DIFF },
    { NM_ITEM,  "Gel\366schte Dateien zur\374ckholen", 0, 0, 0, (APTR)ID_VCS_RESTORE },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Pull",                        0, 0, 0, (APTR)ID_VCS_PULL },
    { NM_ITEM,  "Push",                        0, 0, 0, (APTR)ID_VCS_PUSH },
    { NM_ITEM,  "Clone...",                    0, 0, 0, (APTR)ID_VCS_CLONE },
    { NM_ITEM,  "Einstellungen...",            0, 0, 0, (APTR)ID_VCS_SETTINGS },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "AmiVCS-GUI \366ffnen",          "G", 0, 0, (APTR)ID_VCS_GUI },
    { NM_TITLE, "Hilfe", 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "Hothelp (F1)",             0, 0, 0, (APTR)ID_HELP_F1 },
    { NM_ITEM,  "Struct-Layout (Shift+F1)", 0, 0, 0, (APTR)ID_HELP_SHIFT_F1 },
    { NM_ITEM,  "Autodoc-Suche... (F2)",    0, 0, 0, (APTR)ID_HELP_F2 },
    { NM_ITEM,  NM_BARLABEL, 0, 0, 0, (APTR)0 },
    { NM_ITEM,  "\334ber AmigaIDE...", "?", 0, 0, (APTR)ID_ABOUT },
    { NM_END, NULL, 0, 0, 0, (APTR)0 },
};

/* ------------------------------------------------------------------ */
/* Host for vcs/vcspanel.c (AmiVCS as an external tool)                 */
/* ------------------------------------------------------------------ */
static void open_project_at(const char *path);

static int vcs_host_building(void)
{
    return g_build.state != BUILD_IDLE;
}

/* the editor's file and the project; 0 if something could not be saved */
static int vcs_host_save_all(void)
{
    if (g_currentFile[0]) {
        ULONG changed = FALSE;
        get(g_editor, MUIA_TextEditor_HasChanged, &changed);
        if (changed && !editor_save_current())
            return 0;
    }
    if (!g_projectFile[0])
        return 0;
    if (g_projectDirty) {
        if (!aide_project_save(&g_project, g_projectFile))
            return 0;
        g_projectDirty = 0;
    }
    refresh_tree();
    return 1;
}

/* after amivcs rewrote files: the project (its sources may differ per
 * branch) and the open file come back from disk */
static void vcs_host_reload_project(void)
{
    char file[AIDE_MAX_PATH];
    strncpy(file, g_currentFile, sizeof(file) - 1);
    file[sizeof(file) - 1] = '\0';
    if (g_projectFile[0] && aide_fs_exists(g_projectFile)) {
        char aproj[AIDE_MAX_PATH];
        strncpy(aproj, g_projectFile, sizeof(aproj) - 1);
        aproj[sizeof(aproj) - 1] = '\0';
        open_project_at(aproj);
        if (file[0] && aide_fs_exists(file))
            editor_open_file(file);
    } else {
        editor_clear();
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                    "Die Projektdatei geh\366rt nicht zu diesem Branch.");
    }
}

static void vcs_host_append_output(const char *text)
{
    DoMethod(g_outputEditor, MUIM_TextEditor_InsertText, (ULONG)text, MUIV_TextEditor_InsertText_Bottom);
}

/* a string requester with an explanation above the field; `out` holds
 * the initial text */
static int vcs_host_ask_string(const char *title, const char *prompt, char *out, unsigned long outsz)
{
    char buffer[256];
    struct TagItem tags[2];
    LONG ok;
    strncpy(buffer, out ? out : "", sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    tags[0].ti_Tag = RTGS_TextFmt; tags[0].ti_Data = (ULONG)prompt;
    tags[1].ti_Tag = TAG_DONE;     tags[1].ti_Data = 0;
    ok = rtGetStringA((STRPTR)buffer, (LONG)(sizeof(buffer) - 1), (CONST_STRPTR)title, NULL, tags);
    if (!ok)
        return 0;
    strncpy(out, buffer, outsz - 1);
    out[outsz - 1] = '\0';
    return 1;
}

/* AmiVCS login: user, password (typed invisibly, RTGS_Invisible V38),
 * then whether amivcs should remember the password */
static int vcs_host_ask_login(const char *url, int refused, char *user, unsigned long usz,
                              char *pass, unsigned long psz, int *remember)
{
    char prompt[400], buffer[128];
    struct TagItem tags[3];
    LONG ok;

    sprintf(prompt, "%sAnmeldung f\374r\n%.300s\n\nBenutzer:",
            refused ? "Anmeldung abgelehnt -- falscher\nBenutzer oder falsches Passwort?\n\n" : "", url);
    if (!vcs_host_ask_string("AmiVCS-Login", prompt, user, usz) || !user[0])
        return 0;
    buffer[0] = '\0';
    sprintf(prompt, "Passwort f\374r %.60s:", user);
    tags[0].ti_Tag = RTGS_TextFmt;   tags[0].ti_Data = (ULONG)prompt;
    tags[1].ti_Tag = RTGS_Invisible; tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = TAG_DONE;       tags[2].ti_Data = 0;
    ok = rtGetStringA((STRPTR)buffer, (LONG)(sizeof(buffer) - 1), (CONST_STRPTR)"AmiVCS-Login", NULL, tags);
    if (!ok || !buffer[0])
        return 0;
    strncpy(pass, buffer, psz - 1);
    pass[psz - 1] = '\0';
    memset(buffer, 0, sizeof(buffer));
    *remember = MUI_Request(g_app, g_window, 0, "AmiVCS-Login", "_Merken|_Nicht merken",
                            "Passwort f\374r dieses Repository merken?\n"
                            "(im Klartext in .amivcs/config)") == 1;
    return 1;
}

static int vcs_host_ask_dir(const char *title, char *out, unsigned long outsz)
{
    return ask_directory(title, out, outsz);
}

/* no saving and no build while amivcs rewrites the working tree */
static void vcs_host_set_locked(int locked, int touchesFiles)
{
    set(g_editor, MUIA_TextEditor_ReadOnly, (locked && touchesFiles) ? TRUE : FALSE);
    set_build_menus_enabled(!locked && g_build.state == BUILD_IDLE);
    if (locked && touchesFiles)
        set_status("Versionsverwaltung \344ndert Dateien...");
    else if (!locked)
        set_status("");
}

int mainwindow_run(void)
{
    Object *leftRegister;
    Object *editorVScroll, *editorHScroll, *outputVScroll, *outputHScroll;
    ULONG waitMask = 0, got = 0;
    BOOL running = TRUE;

    /* Opened once here, closed once at shutdown (below); only *armed*
     * while a build is actually running -- see build_timer_arm()/
     * do_build()/build_tick(). A failure here isn't fatal to the whole
     * app, just to building (do_build() checks g_timerOpen itself and
     * refuses to build with a clear message instead of silently having
     * no live output). */
    build_timer_open();

    splash_status("Lade Einstellungen ...", 45);
    /* Loads make-program/SDK-root/toolchain-root from ENVARC: (or
     * defaults) -- must happen before aide_toolchain_init()/
     * aide_sdk_append_builtins() below, which read them via
     * prefs_toolchain_root()/prefs_sdk_root(). Widget construction
     * (prefswindow_init(), which needs g_app) happens later, alongside
     * helpwindow_init()/disasmwindow_init()/settingswindow_init(). */
    prefswindow_load();

    /* A genuinely empty in-memory project (no phantom "src/main.c" --
     * aide_project_create_minimal_c() is for "Neu", not for the app's
     * idle-at-startup state, see file header: the user hadn't created or
     * opened anything yet and was confused to see a source file listed
     * regardless). g_project must still be a valid AideProject at all
     * times (every function here assumes that), just an empty one. */
    aide_project_init(&g_project);
    g_projectFile[0] = '\0';
    g_projectRoot[0] = '\0';
    g_currentFile[0] = '\0';

    splash_status("Suche SDKs ...", 50);
    aide_toolchain_init(&g_toolchain, prefs_toolchain_root());
    aide_sdk_list_init(&g_sdkRegistry);
    /* SDKs live under their own "SDK:" assign, not under "vbcc:sdk/" --
     * aide_sdk_append_builtins() takes the SDK root directly (the dir
     * containing NDK_3.2/, MUI_3.8/, ...), unlike the Qt side which
     * derives it from the toolchain root (SdkRegistry.cpp appends
     * "/sdk" there instead). */
    aide_sdk_append_builtins(prefs_sdk_root(), &g_sdkRegistry);
    prefs_apply_sdk_roots(&g_sdkRegistry);
    aide_sdk_append_from_manifests(prefs_sdk_root(), &g_sdkRegistry);

    splash_status("Baue Oberfl\344che auf ...", 60);
    /* A real tree (subfolders as actual expandable nodes), not a flat
     * "  path" string list -- per user feedback. NListtree.mcc, not
     * NList.mcc/NListObject; wired to its view via MUIA_NListview_NList
     * (NOT MUIA_Listview_List, which is what the plain-NList widgets
     * below use) and double-click via its own MUIA_NListtree_DoubleClick
     * (NOT the view-level MUIA_Listview_DoubleClick) -- confirmed against
     * the real, working NListtree-Demo.c SDK example, same
     * "find a proven working example instead of guessing MUI semantics"
     * approach that fixed the editor/scrollbar issues earlier. */
    g_treeList = NListtreeObject,
        MUIA_NListtree_DoubleClick, MUIV_NListtree_DoubleClick_Tree,
        MUIA_NListtree_Title, FALSE,
    End;
    g_treeListview = NListviewObject, MUIA_NListview_NList, g_treeList, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;

    g_symbolsList = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_Title, FALSE,
    End;
    g_symbolsListview = NListviewObject, MUIA_Listview_List, g_symbolsList, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;

    g_issuesList = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_Title, FALSE,
    End;
    g_issuesListview = NListviewObject, MUIA_Listview_List, g_issuesList, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;

    g_searchList = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_Title, FALSE,
    End;
    g_searchListview = NListviewObject, MUIA_Listview_List, g_searchList, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;

    g_memmapList = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_Title, FALSE,
    End;
    g_memmapListview = NListviewObject, MUIA_Listview_List, g_memmapList, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;

    /* Scrollbars: built as plain siblings, connected via set() AFTER the
     * application object exists (see below, right after "if (!g_app)"),
     * NOT via MUIA_TextEditor_Slider/HorizontalSlider as construction
     * tags. Two earlier attempts wiring them as construction tags both
     * collapsed the editor to a tiny box and (once) broke the Balance
     * splitter -- root cause never pinned down blind. Found the correct,
     * proven pattern in the TextEditor.mcc SDK's own example
     * (MCC_TextEditor/Developer/C/Examples/TextEditor-Demo.c): it builds
     * editor+slider as siblings in an HGroup with no Slider tag at
     * construction time at all, then does "set(editorgad,
     * MUIA_TextEditor_Slider, slider)" once the app is up. Horizontal
     * orientation is MUIA_Group_Horiz (confirmed from MCC_NList's own
     * demo, NList-Demo.c's "PR_Horiz = ScrollbarObject, MUIA_Group_Horiz,
     * TRUE, End") -- MUIA_Prop_Horiz, used in the two earlier attempts,
     * was simply the wrong tag, which is why that scrollbar rendered
     * vertically instead of horizontally. */
    editorVScroll = ScrollbarObject, End;
    editorHScroll = ScrollbarObject, MUIA_Group_Horiz, TRUE, End;
    g_editor = TextEditorObject,
        MUIA_TextEditor_FixedFont, TRUE,
        MUIA_TextEditor_WrapMode,  MUIV_TextEditor_WrapMode_NoWrap,
    End;

    outputVScroll = ScrollbarObject, End;
    outputHScroll = ScrollbarObject, MUIA_Group_Horiz, TRUE, End;
    g_outputEditor = TextEditorObject,
        MUIA_TextEditor_FixedFont, TRUE,
        MUIA_TextEditor_ReadOnly,  TRUE,
        MUIA_TextEditor_WrapMode,  MUIV_TextEditor_WrapMode_NoWrap,
    End;

    g_statusText = TextObject, MUIA_Text_Contents, "", End;
    /* No line-number gutter (TextEditor.mcc has none built in, and a
     * custom Area-subclass gutter is a real chunk of new MUI/BOOPSI code
     * -- CursorX/Y aren't even notify-capable per MCC_TextEditor.doc, so
     * it would need its own AskMinMax/Draw dispatcher synced off
     * MUIA_TextEditor_Prop_First/Visible instead of a simple attribute
     * watch). This is the fallback the user explicitly offered instead:
     * a status-bar field, checked every main-loop iteration (cheap: two
     * get()s + a strcmp()) but only actually set() when the text changes
     * -- see the main loop's own comment on why the "only set() on
     * change" part matters, not just the checking. */
    g_cursorPosText = TextObject, MUIA_Text_Contents, "", End;

    leftRegister = RegisterObject,
        MUIA_Register_Titles, (ULONG)g_leftRegisterTitles,
        Child, g_treeListview,
        Child, g_symbolsListview,
        Child, vcspanel_create_page(),
    End;

    g_bottomRegister = RegisterObject,
        MUIA_Register_Titles, (ULONG)g_bottomRegisterTitles,
        MUIA_VertWeight, 33,
        Child, VGroup,
            Child, HGroup,
                MUIA_Group_Spacing, 0,
                Child, g_outputEditor,
                Child, outputVScroll,
            End,
            Child, outputHScroll,
        End,
        Child, g_issuesListview,
        Child, g_searchListview,
        Child, g_memmapListview,
        Child, vcspanel_create_history_page(),
    End;

    /* Built separately (not inline as MUIA_Window_Menustrip's own tag
     * value below) so set_build_menus_enabled() can find items in it by
     * ID later via MUIM_FindUData -- MUIO_MenustripNM copies each
     * NewMenu.UserData (the ID_* constants in MenuData[]) into every
     * item's MUIA_UserData, which is exactly what MUIM_FindUData
     * matches against. */
    g_menustrip = MUI_MakeObject(MUIO_MenustripNM, MenuData, 0);

    g_app = ApplicationObject,
        MUIA_Application_Title,       "AmigaIDE",
        MUIA_Application_Version,     amiga_ver,
        MUIA_Application_Copyright,   "AmigaIDE",
        MUIA_Application_Author,      "AmigaIDE",
        MUIA_Application_Description, "vbcc/vasm/vlink IDE for AmigaOS",
        MUIA_Application_Base,        "AMIGAIDE",

        SubWindow, g_window = WindowObject,
            MUIA_Window_ID,    MAKE_ID('M','A','I','N'),
            /* Build timestamp in the title bar -- __DATE__/__TIME__
             * resolve at compile time, so this changes on every rebuild
             * with no manual counter to remember to bump. Exists purely
             * so "are you sure you copied the latest binary?" has a
             * one-glance answer instead of being an annoying question to
             * ask the person testing it. */
            MUIA_Window_Title, AIDE_WINDOW_TITLE_BASE,
            MUIA_Window_Menustrip, g_menustrip,
            WindowContents, VGroup,
                Child, HGroup,
                    Child, VGroup,
                        /* Reverted to the original 25 -- the 35 bump was
                         * made in the same change as the scrollbar
                         * wiring that had to be reverted (see g_editor
                         * above) and was never actually validated on its
                         * own; restoring the last known-good value
                         * rather than leaving an unverified change in
                         * place. */
                        MUIA_HorizWeight, 25,
                        Child, leftRegister,
                    End,
                    /* Balance.mui -- a draggable splitter between the
                     * project tree and the editor (and another one below,
                     * between that row and the Ausgabe/Issues/... area).
                     * Drag the thin bar to resize either side; MUI
                     * persists the position per MUIA_Window_ID across
                     * runs once MUIA_Application_Base's ENV: prefs exist. */
                    Child, BalanceObject, End,
                    Child, VGroup,
                        Child, HGroup,
                            MUIA_Group_Spacing, 0,
                            Child, g_editor,
                            Child, editorVScroll,
                        End,
                        Child, editorHScroll,
                    End,
                End,
                Child, BalanceObject, End,
                Child, g_bottomRegister,
                Child, HGroup,
                    MUIA_Group_Spacing, 4,
                    Child, g_statusText,
                    Child, g_cursorPosText,
                End,
            End,
        End,
    End;

    if (!g_app)
        return 20;

    /* Connect the editors to their scrollbars now that the whole object
     * tree exists -- see the scrollbar-construction comment above for
     * why this has to be a post-construction set() rather than a
     * construction-time tag. */
    set(g_editor, MUIA_TextEditor_Slider, editorVScroll);
    set(g_editor, MUIA_TextEditor_HorizontalSlider, editorHScroll);
    set(g_outputEditor, MUIA_TextEditor_Slider, outputVScroll);
    set(g_outputEditor, MUIA_TextEditor_HorizontalSlider, outputHScroll);

    /* Secondary windows (Hilfe/Disassembler) -- added as OM_ADDMEMBER
     * children of g_app after it exists, see help/helpwindow.c and
     * disasm/disasmwindow.c. PROGDIR:cache (not T:, which is typically
     * RAM-backed and wiped on reboot) so the autodoc index survives
     * across program runs -- see ensure_help_index_current(): the first
     * build per SDK is the only slow one. */
    AIDE_REMEMBER_WINDOW(g_window);
    splash_status("Richte Fenster ein ...", 85);
    helpwindow_init(g_app, "PROGDIR:cache", ensure_help_index_current);
    disasmwindow_init(g_app);
    settingswindow_init(g_app, &g_sdkRegistry);
    prefswindow_init(g_app);
    checkwindow_init(g_app);
    {
        VcsHost host;
        memset(&host, 0, sizeof(host));
        host.building = vcs_host_building;
        host.save_all = vcs_host_save_all;
        host.reload_project = vcs_host_reload_project;
        host.open_project = open_project_at;
        host.set_status = set_status;
        host.append_output = vcs_host_append_output;
        host.ask_string = vcs_host_ask_string;
        host.ask_dir = vcs_host_ask_dir;
        host.set_locked = vcs_host_set_locked;
        host.ask_login = vcs_host_ask_login;
        vcspanel_init(g_app, g_window, &host);
    }

    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_QUIT);

    DoMethod(g_treeList, MUIM_Notify, MUIA_NListtree_DoubleClick, MUIV_EveryTime,
            g_app, 2, MUIM_Application_ReturnID, ID_TREE_DCLICK);
    DoMethod(g_symbolsListview, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_SYMBOL_DCLICK);
    DoMethod(g_issuesListview, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_ISSUES_DCLICK);
    DoMethod(g_searchListview, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_SEARCH_DCLICK);

    /* Real F-key shortcuts: MUIA_Window_InputEvent notifications match a
     * commodities.library input description string, NOT MUIV_EveryTime
     * -- "-fN" is "fN, up-transition", fires once per keypress (verified
     * pattern). The "-shift fN" qualifier form for
     * the Shift+F combos is unverified -- see file header. */
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-f1",
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_F1);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-shift f1",
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_SHIFT_F1);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-f2",
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_F2);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-f3",
            g_app, 2, MUIM_Application_ReturnID, ID_GOTO_DEF);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-shift f3",
            g_app, 2, MUIM_Application_ReturnID, ID_FIND_REFS);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-f4",
            g_app, 2, MUIM_Application_ReturnID, ID_ISSUE_NEXT);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_InputEvent, "-shift f4",
            g_app, 2, MUIM_Application_ReturnID, ID_ISSUE_PREV);
    /* Fires once when the editor flips clean -> dirty (HasChanged only
     * ever transitions on an actual edit, and stays TRUE afterwards --
     * see editor_save_current()/editor_open_file()/editor_clear() for
     * where it gets reset back to FALSE), so this is cheap: it doesn't
     * refresh the tree/title on every keystroke, just the one time an
     * unsaved marker needs to appear. */
    DoMethod(g_editor, MUIM_Notify, MUIA_TextEditor_HasChanged, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_EDITOR_DIRTY);

    refresh_tree();
    splash_status("\326ffne Hauptfenster ...", 100);
    set(g_window, MUIA_Window_Open, TRUE);
    splash_close();

    while (running) {
        LONG id = DoMethod(g_app, MUIM_Application_Input, &waitMask);

        /* Timer signal from the build tailer -- see build_tick(). Must
         * run every iteration this bit is set, not just when `id` is
         * some specific value: the timer signal and an MUI event can
         * both be pending in the same `got` mask at once. */
        vcspanel_handle_signals(got);
        if (got & g_timerSig) {
            GetMsg(g_timerPort);
            g_timerArmed = 0; /* retrieved -- no longer in flight until re-armed below */
            build_tick();
            got &= ~g_timerSig;
        }

        /* Cursor position field -- see g_cursorPosText's own comment for
         * why this (not a real gutter). The loop cycles on MUI's own
         * periodic idle/blink ticks too, not just real keystrokes, and
         * MUIA_Text_Contents redraws unconditionally on every set() even
         * when the value hasn't changed -- calling set() every single
         * iteration regardless caused a visible ~1s flicker (the idle
         * tick rate), per user feedback. Only actually setting it when
         * the text differs from what's already showing fixes that while
         * staying just as responsive to real cursor movement. */
        {
            static char lastPosText[32];
            char posBuf[32];
            if (g_currentFile[0]) {
                ULONG cx = 0, cy = 0;
                get(g_editor, MUIA_TextEditor_CursorX, &cx);
                get(g_editor, MUIA_TextEditor_CursorY, &cy);
                snprintf(posBuf, sizeof(posBuf), "Zeile %lu, Spalte %lu", (unsigned long)cy + 1, (unsigned long)cx + 1);
            } else {
                posBuf[0] = '\0';
            }
            if (strcmp(posBuf, lastPosText) != 0) {
                set(g_cursorPosText, MUIA_Text_Contents, posBuf);
                strcpy(lastPosText, posBuf);
            }
        }

        switch (id) {
        case ID_QUIT:
            if (has_unsaved_changes()) {
                LONG choice = MUI_Request(g_app, g_window, 0, "AmigaIDE",
                                           "_Beenden|_Abbrechen",
                                           "Es gibt ungespeicherte \304nderungen.\n\nTrotzdem beenden?");
                if (choice != 1)
                    break;
            }
            /* Deliberately just a notice, not a "wait/abort?" choice --
             * the running build owns its own resources (see
             * mainwindow_run()'s teardown comment) and finishing it is
             * never a reason to keep the whole IDE open. */
            if (g_build.state != BUILD_IDLE) {
                MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                            "Ein Build l\344uft noch im Hintergrund weiter.");
            }
            running = FALSE;
            break;
        case ID_EDITOR_DIRTY:
            refresh_tree();
            break;
        case ID_NEW_PROJECT:
            do_new_project();
            break;
        case ID_OPEN_PROJECT:
            do_open_project();
            break;
        case ID_SAVE:
            do_save();
            break;
        case ID_ADD_NEW_C_SOURCE:
            do_add_new_file(NEWFILE_C_SOURCE);
            break;
        case ID_ADD_NEW_C_HEADER:
            do_add_new_file(NEWFILE_C_HEADER);
            break;
        case ID_ADD_NEW_ASM_SOURCE:
            do_add_new_file(NEWFILE_ASM_SOURCE);
            break;
        case ID_ADD_NEW_ASM_INCLUDE:
            do_add_new_file(NEWFILE_ASM_INCLUDE);
            break;
        case ID_ADD_NEW_BASIC_SOURCE:
            do_add_new_file(NEWFILE_BASIC_SOURCE);
            break;
        case ID_ADD_EXISTING_FILE:
            do_add_existing_file();
            break;
        case ID_REMOVE_FILE:
            do_remove_file_from_project();
            break;
        case ID_PROJECT_SETTINGS:
            settingswindow_set_long_names(g_projectRoot[0] ? aide_project_long_names(g_projectRoot) : 0);
            settingswindow_show(&g_project);
            break;
        case ID_VCS_GUI:
            do_open_vcs_gui();
            break;
        case ID_CHECK_SDKS:
            do_check_sdks();
            break;
        case ID_PREFS:
            prefswindow_show();
            break;
        case ID_BUILD:
            if (vcspanel_writing()) set_status("Versionsverwaltung arbeitet -- bitte gleich noch einmal.");
            else do_build();
            break;
        case ID_CLEAN:
            if (vcspanel_writing()) set_status("Versionsverwaltung arbeitet -- bitte gleich noch einmal.");
            else do_clean();
            break;
        case ID_BUILD_STOP:
            do_build_stop();
            break;
        case ID_RUN:
            if (vcspanel_writing()) set_status("Versionsverwaltung arbeitet -- bitte gleich noch einmal.");
            else do_run();
            break;
        case ID_DISASSEMBLE:
            do_disassemble();
            break;
        case ID_SAVE_OUTPUT:
            do_save_output();
            break;
        case ID_FORMAT:
            do_format_current_file();
            break;
        case ID_FIND_IN_PROJECT:
            do_find_in_project();
            break;
        case ID_GOTO_DEF:
            do_goto_definition();
            break;
        case ID_FIND_REFS:
            do_find_references();
            break;
        case ID_HELP_F1:
            do_help_f1();
            break;
        case ID_HELP_SHIFT_F1:
            do_help_shift_f1();
            break;
        case ID_HELP_F2:
            do_help_f2();
            break;
        case ID_ISSUE_NEXT:
            do_issue_navigate(1);
            break;
        case ID_ISSUE_PREV:
            do_issue_navigate(-1);
            break;
        case ID_ABOUT:
            {
                char about[160];
                sprintf(about,
                        "AmigaIDE %s build %s (%s)\n"
                        "vbcc/vasm/vlink IDE f\374r AmigaOS 3.x",
                        AMIGAIDE_VERSION_STRING, AMIGAIDE_BUILD_NUMBER,
                        AMIGAIDE_BUILD_DATE);
                MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", about);
            }
            break;
        case ID_TREE_DCLICK: {
            struct MUI_NListtree_TreeNode *active = NULL;
            get(g_treeList, MUIA_NListtree_Active, &active);
            if (active) {
                char relPath[AIDE_MAX_PATH];
                if (tree_relpath_of(active, relPath, sizeof(relPath))) {
                    char path[AIDE_MAX_PATH];
                    if (resolve_tree_row(relPath, path, sizeof(path)))
                        editor_open_file(path);
                }
            }
            break;
        }
        case ID_SYMBOL_DCLICK: {
            LONG active = MUIV_NList_Active_Off;
            get(g_symbolsList, MUIA_NList_Active, &active);
            if (active != MUIV_NList_Active_Off && (size_t)active < g_symbols.count)
                goto_file_line(g_symbols.items[active].file, g_symbols.items[active].line);
            break;
        }
        case ID_SEARCH_DCLICK: {
            LONG active = MUIV_NList_Active_Off;
            get(g_searchList, MUIA_NList_Active, &active);
            if (active != MUIV_NList_Active_Off && (size_t)active < g_searchMatches.count)
                goto_file_line(g_searchMatches.items[active].file, g_searchMatches.items[active].line);
            break;
        }
        case ID_ISSUES_DCLICK: {
            LONG active = MUIV_NList_Active_Off;
            get(g_issuesList, MUIA_NList_Active, &active);
            if (active != MUIV_NList_Active_Off && (size_t)active < g_issueCount) {
                g_issueActive = active;
                goto_file_line(g_issues[active].file, g_issues[active].line);
            }
            break;
        }
        default: {
            int settingsResult;
            if (vcspanel_handle_id(id))
                break;
            settingsResult = settingswindow_handle_id(id);
            if (settingsResult == 2) {
                /* the long-name switch lives in the project folder's
                 * .amivcsproject (aide_project.h), not in the .aproj */
                const char *root = g_newProjectAwaitingSave ? g_newProjectDir : g_projectRoot;
                if (root[0] && settingswindow_long_names() != aide_project_long_names(root))
                    aide_project_set_long_names(root, settingswindow_long_names());
                settingswindow_take_result(&g_project);
                if (g_newProjectAwaitingSave) {
                    g_newProjectAwaitingSave = 0;
                    finish_new_project_save();
                } else {
                    g_projectDirty = 1;
                    refresh_tree();
                    rebuild_symbol_index();
                    invalidate_help_index();
                    set_status("Einstellungen \374bernommen -- noch nicht gespeichert");
                }
            } else if (settingsResult == 1) {
                /* Cancel -- for the "Neu" flow this creates NOTHING at
                 * all (per user feedback): no .aproj, no open project.
                 * g_project is already the fresh empty one do_new_project()
                 * built and g_projectRoot/g_projectFile are already "",
                 * so there's nothing further to undo here. */
                if (g_newProjectAwaitingSave) {
                    g_newProjectAwaitingSave = 0;
                    if (g_newProjectDirCreated)
                        DeleteFile((STRPTR)g_newProjectDir);
                    set_status("Abgebrochen");
                }
                g_newProjectDirCreated = 0;
            } else if (settingsResult == 0) {
                int prefsResult = prefswindow_handle_id(id);
                if (prefsResult == 2) {
                    /* Re-init everything derived from the values
                     * prefswindow.c just persisted -- toolchain root and
                     * SDK root are otherwise only read once, at
                     * mainwindow_run() startup. */
                    aide_toolchain_free(&g_toolchain);
                    aide_toolchain_init(&g_toolchain, prefs_toolchain_root());
                    aide_sdk_list_free(&g_sdkRegistry);
                    aide_sdk_append_builtins(prefs_sdk_root(), &g_sdkRegistry);
                    prefs_apply_sdk_roots(&g_sdkRegistry);
                    aide_sdk_append_from_manifests(prefs_sdk_root(), &g_sdkRegistry);
                    invalidate_make_availability();
                    vcspanel_tool_changed();
                    set_status("Einstellungen \374bernommen");
                } else if (prefsResult == 0) {
                    if (!helpwindow_handle_id(id) && !checkwindow_handle_id(id))
                        disasmwindow_handle_id(id);
                }
                /* prefsResult == 1 (Cancel): nothing to do. */
            }
            /* settingsResult == 3 (an internal Add/Remove-library click,
             * window still open): nothing to do here, deliberately. */
            break;
        }
        }
        /* waitMask/got are deliberately two separate variables, not one
         * reused across both calls like the original single-`sigs`
         * version: MUIM_Application_Input sets waitMask to 0 to mean
         * "call me again immediately, don't Wait()" (a queued event is
         * still pending), and reusing the same variable for Wait()'s
         * result would have let a stale `got` re-trigger the timer
         * branch above on that next, non-waiting iteration. A timer
         * signal arriving during such a burst isn't lost either way --
         * only Wait() itself clears signal bits, so it's still pending
         * next time this actually calls Wait(). */
        /* After handling an ID, ask MUI once more before sleeping: what
         * the handler did (a window opened after a requester, as in
         * "Neu...") may only show once MUI gets control again. Waiting
         * right away left the project settings unseen on a real A1200
         * until the next input event, e.g. a menu click. */
        if (waitMask && !id) got = Wait(waitMask | SIGBREAKF_CTRL_C | g_timerSig | vcspanel_signal_mask());
        else                 got = 0;
    }

    helpwindow_free();
    disasmwindow_free();
    settingswindow_free();
    prefswindow_free();
    checkwindow_free();
    clear_issues();
    free(g_issues);
    aide_search_matchlist_free(&g_searchMatches);
    aide_symbollist_free(&g_symbols);
    aide_sdk_list_free(&g_sdkRegistry);
    aide_toolchain_free(&g_toolchain);
    aide_project_free(&g_project);

    /* If a build is still running at this point, it is deliberately
     * NOT waited for or aborted: launch_build_script()'s NP_CurrentDir
     * lock and SYS_Output log handle already belong to that process
     * tree, not to us, so it keeps compiling/linking to completion in
     * the background regardless of whether AmigaIDE itself is still
     * around -- exactly like a program started via "Run" already
     * survives the IDE quitting. Only OUR OWN resources need releasing
     * here: the timer (armed or not -- see build_timer_close()'s own
     * comment) and the tailer's held-back partial line. There is no
     * "AIDEBUILD:"-style shared assign anymore that a still-running
     * build could be left resolving into nothing after we exit. */
    if (g_build.state != BUILD_IDLE)
        aide_logtail_free(&g_build.tail);
    build_timer_close();
    vcspanel_cleanup();

    if (g_app)
        MUI_DisposeObject(g_app);
    return 0;
}
