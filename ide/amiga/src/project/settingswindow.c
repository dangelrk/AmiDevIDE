#include "project/settingswindow.h"

#include <libraries/iffparse.h> /* MAKE_ID() */
#include <libraries/reqtools.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include "mui/mui_stubs.h"
#include "mui/reqtools_stubs.h"
#include "mui/NList_mcc.h"
#include "mui/NListview_mcc.h"
#include "mui/TextEditor_mcc.h"

#include "aide/aide_sdk.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ID_SETTINGS_OK        3001
#define ID_SETTINGS_CANCEL    3002
#define ID_SETTINGS_LIB_ADD   3003
#define ID_SETTINGS_LIB_REMOVE 3004
#define ID_SETTINGS_LIB_UP     3005
#define ID_SETTINGS_LIB_DOWN   3006

static Object *g_app;
static Object *g_window;

/* General */
static Object *g_nameStr, *g_binaryStr, *g_targetDirStr, *g_objDirStr;
static Object *g_languageCycle, *g_outputKindCycle;
static Object *g_typeCycle, *g_stackStr, *g_iconModeCycle, *g_iconPathStr;

/* Compiler */
static Object *g_cpuCycle, *g_fpuCycle, *g_optCycle;
static Object *g_smallDataCheck, *g_debugSymbolsCheck;
static Object *g_includePathsEditor, *g_definesEditor, *g_extraCFlagsEditor;

/* Assembler */
static Object *g_kick1hunksCheck, *g_asmNoSymCheck;
static Object *g_extraAsmFlagsEditor;

/* Linker */
static Object *g_startupCycle, *g_customStartupStr;
static Object *g_librariesList, *g_librariesListview, *g_libAddBtn, *g_libRemoveBtn;
static Object *g_libUpBtn, *g_libDownBtn;
static Object *g_extraLibDirsEditor, *g_stripCheck, *g_extraLdFlagsEditor;

/* SDKs */
#define EXTRA_SDK_COUNT 9
typedef struct { const char *id; const char *label; } ExtraSdkDef;
/* German labels for the built-in SDKs, in the order
   aide_sdk_append_builtins() registers them. Every other SDK is labelled
   with its own name and version. */
static const ExtraSdkDef g_extraSdkDefs[EXTRA_SDK_COUNT] = {
    { "ndk-3.2",  "NDK 3.2" },
    { "mui-3.8",  "MUI 3.8" },
    { "mcc",      "MUI-Zusatzklassen (MCC)" },
    { "regex",    "regex.library" },
    { "reqtools", "reqtools.library" },
    { "roadshow", "Roadshow (TCP/IP)" },
    { "amissl",   "AmiSSL v5" },
    { "sqlite3",  "SQLite3" },
    { "posixlib", "PosixLib" },
};
/* One checkmark row per SDK in the registry (include-search order), then
 * one per id the project names that the registry lacks -- ticked, labelled
 * "nicht installiert", so it survives OK and can be dropped on purpose.
 * Rebuilt by rebuild_sdk_rows() each time the window is shown, since the
 * missing ones depend on the project and the registry can change with the
 * preferences. */
#define SDK_ROWS_MAX 64
static const AideSdkList *g_registry;
static Object *g_sdkGroup;
static Object *g_sdkLabels[SDK_ROWS_MAX];
static Object *g_sdkChecks[SDK_ROWS_MAX];
static char *g_sdkRowIds[SDK_ROWS_MAX];
static char *g_sdkRowTexts[SDK_ROWS_MAX]; /* kept alive for the label objects */
static int g_sdkRowCount;

static Object *g_okBtn, *g_cancelBtn;

static AideProject g_editing;
static int g_haveEditing;

/* Cycle label tables. AideStartup/AideProgramType/AideIconMode enum
 * values already equal their cycle index (see aide_project.h), so no
 * separate values table is needed for those. */
static const char *g_cpuLabels[]      = { "68000", "68010", "68020", "68030", "68040", "68060", NULL };
static const char *g_fpuLabels[]      = { "none", "68881", "68882", "68040", "68060", NULL };
static const char *g_optLabels[]      = { "0", "1", "2", "3", NULL };
static const char *g_languageLabels[] = { "C", "ACE BASIC", "Assembler", NULL };
static const char *g_outputKindLabels[] = { "Programm", "Link-Library", "Shared Library", NULL };
static const char *g_typeLabels[]     = { "CLI", "Workbench", NULL };
static const char *g_iconModeLabels[] = { "Generieren", "Datei", NULL };
static const char *g_startupLabels[]  = { "Standard (startup.o)", "MinStart", "MinRes", "Custom", NULL };

/* MUIA_Register_Titles only stores the pointer it's given, not a copy --
 * this MUST be static (or file-scope), never a compound literal built
 * inside settingswindow_init(): that function returns right after
 * building the window (it's only called once, at startup), so a
 * compound literal there would have automatic (stack) storage duration
 * and become a dangling pointer the moment the function returned, long
 * before the window is ever actually shown. Same reasoning applies to
 * every *_Titles/*_Entries tag anywhere in this app. */
static const char *g_tabTitles[] = { "General", "Compiler", "Assembler", "Linker", "SDKs", NULL };

/* ------------------------------------------------------------------ */
/* Small widget <-> value helpers                                        */
/* ------------------------------------------------------------------ */
static void set_str(Object *obj, const char *value) { set(obj, MUIA_String_Contents, value); }

static char *get_str_dup(Object *obj)
{
    STRPTR v = NULL;
    get(obj, MUIA_String_Contents, &v);
    return aide_strdup(v ? (char *)v : "");
}

static void set_cycle(Object *obj, int index) { set(obj, MUIA_Cycle_Active, (ULONG)index); }

static int get_cycle(Object *obj)
{
    ULONG v = 0;
    get(obj, MUIA_Cycle_Active, &v);
    return (int)v;
}

static void set_check(Object *obj, int value) { set(obj, MUIA_Selected, value ? (ULONG)TRUE : (ULONG)FALSE); }

static int get_check(Object *obj)
{
    ULONG v = 0;
    get(obj, MUIA_Selected, &v);
    return v ? 1 : 0;
}

static int index_of_string(const char *const *values, int count, const char *value)
{
    int i;
    for (i = 0; i < count; ++i)
        if (strcmp(values[i], value) == 0) return i;
    return 0;
}

static void strlist_to_editor(Object *editor, const AideStrList *list)
{
    AideStr joined;
    size_t i;
    aide_str_init(&joined);
    for (i = 0; i < list->count; ++i) {
        if (i > 0) aide_str_append_char(&joined, '\n');
        aide_str_append(&joined, list->items[i]);
    }
    DoMethod(editor, MUIM_TextEditor_ClearText);
    DoMethod(editor, MUIM_TextEditor_InsertText, (ULONG)joined.data, MUIV_TextEditor_InsertText_Top);
    aide_str_free(&joined);
}

/* Splits the editor's text on '\n', trims each line, drops empty ones. */
static void editor_to_strlist(Object *editor, AideStrList *out)
{
    STRPTR text = (STRPTR)DoMethod(editor, MUIM_TextEditor_ExportText);
    aide_strlist_free(out);
    aide_strlist_init(out);
    if (text) {
        char *line = (char *)text;
        while (line) {
            char *nl = strchr(line, '\n');
            size_t len = nl ? (size_t)(nl - line) : strlen(line);
            char *raw = aide_strndup(line, len);
            char *trimmed = aide_str_trim_dup(raw);
            free(raw);
            if (trimmed[0]) aide_strlist_push(out, trimmed);
            free(trimmed);
            line = nl ? nl + 1 : NULL;
        }
        FreeVec(text);
    }
}

static void strlist_remove_at(AideStrList *l, size_t index)
{
    if (index >= l->count) return;
    free(l->items[index]);
    memmove(&l->items[index], &l->items[index + 1], (l->count - index - 1) * sizeof(char *));
    l->count--;
}

/* Swaps two adjacent entries -- used by the Linker tab's Hoch/Runter
 * buttons. Library link order matters (a library can only resolve
 * symbols against ones already seen by vlink), so being stuck with
 * whatever order Hinzufuegen happened to create them in wasn't enough. */
static void strlist_swap(AideStrList *l, size_t a, size_t b)
{
    char *tmp = l->items[a];
    l->items[a] = l->items[b];
    l->items[b] = tmp;
}

static void refresh_libraries_list(void)
{
    size_t i;
    DoMethod(g_librariesList, MUIM_NList_Clear);
    for (i = 0; i < g_editing.linker.libraries.count; ++i)
        DoMethod(g_librariesList, MUIM_NList_InsertSingle, (ULONG)g_editing.linker.libraries.items[i], MUIV_NList_Insert_Bottom);
}

static int ask_lib_name(char *out, size_t outsz)
{
    char buffer[64];
    LONG ok;
    buffer[0] = '\0';
    ok = rtGetStringA((STRPTR)buffer, (LONG)(sizeof(buffer) - 1),
                       (CONST_STRPTR)"Bibliothek (ohne \"-l\", z.B. \"mui\")", NULL, NULL);
    if (!ok)
        return 0;
    strncpy(out, buffer, outsz - 1);
    out[outsz - 1] = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* Populate widgets from / apply widgets back to g_editing                */
/* ------------------------------------------------------------------ */
static void populate_widgets(void)
{
    char stackBuf[16];

    set_cycle(g_languageCycle, (int)g_editing.language);
    set_cycle(g_outputKindCycle, (int)g_editing.output.kind);
    set_str(g_nameStr, g_editing.name);
    set_str(g_binaryStr, g_editing.output.binary);
    set_str(g_targetDirStr, g_editing.output.targetDir);
    set_str(g_objDirStr, g_editing.output.objDir);
    set_cycle(g_typeCycle, (int)g_editing.program.type);
    snprintf(stackBuf, sizeof(stackBuf), "%d", g_editing.program.stackSize);
    set_str(g_stackStr, stackBuf);
    set_cycle(g_iconModeCycle, (int)g_editing.program.iconMode);
    set_str(g_iconPathStr, g_editing.program.iconPath);

    set_cycle(g_cpuCycle, index_of_string(g_cpuLabels, 6, g_editing.compiler.cpu));
    set_cycle(g_fpuCycle, index_of_string(g_fpuLabels, 5, g_editing.compiler.fpu));
    set_cycle(g_optCycle, g_editing.compiler.optLevel >= 0 && g_editing.compiler.optLevel <= 3 ? g_editing.compiler.optLevel : 2);
    set_check(g_smallDataCheck, g_editing.compiler.smallData);
    set_check(g_debugSymbolsCheck, g_editing.compiler.debugSymbols);
    strlist_to_editor(g_includePathsEditor, &g_editing.includePaths);
    strlist_to_editor(g_definesEditor, &g_editing.defines);
    strlist_to_editor(g_extraCFlagsEditor, &g_editing.compiler.extraCFlags);

    set_check(g_kick1hunksCheck, g_editing.compiler.kick1hunks);
    set_check(g_asmNoSymCheck, g_editing.compiler.asmNoSym);
    strlist_to_editor(g_extraAsmFlagsEditor, &g_editing.compiler.extraAsmFlags);

    set_cycle(g_startupCycle, (int)g_editing.linker.startup);
    set_str(g_customStartupStr, g_editing.linker.customStartupPath);
    refresh_libraries_list();
    strlist_to_editor(g_extraLibDirsEditor, &g_editing.linker.extraLibDirs);
    set_check(g_stripCheck, g_editing.linker.strip);
    strlist_to_editor(g_extraLdFlagsEditor, &g_editing.linker.extraLdFlags);
    /* The SDK rows are set up by rebuild_sdk_rows(). */
}

static void apply_widgets(void)
{
    char *s;
    int i;

    g_editing.language = (AideProjectLanguage)get_cycle(g_languageCycle);
    g_editing.output.kind = (AideOutputKind)get_cycle(g_outputKindCycle);
    s = get_str_dup(g_nameStr); free(g_editing.name); g_editing.name = s;
    s = get_str_dup(g_binaryStr); free(g_editing.output.binary); g_editing.output.binary = s;
    s = get_str_dup(g_targetDirStr); free(g_editing.output.targetDir); g_editing.output.targetDir = s;
    s = get_str_dup(g_objDirStr); free(g_editing.output.objDir); g_editing.output.objDir = s;
    g_editing.program.type = (AideProgramType)get_cycle(g_typeCycle);
    {
        char *stackStr = get_str_dup(g_stackStr);
        long v = strtol(stackStr, NULL, 10);
        g_editing.program.stackSize = v > 0 ? (int)v : g_editing.program.stackSize;
        free(stackStr);
    }
    g_editing.program.iconMode = (AideIconMode)get_cycle(g_iconModeCycle);
    s = get_str_dup(g_iconPathStr); free(g_editing.program.iconPath); g_editing.program.iconPath = s;

    s = aide_strdup(g_cpuLabels[get_cycle(g_cpuCycle)]); free(g_editing.compiler.cpu); g_editing.compiler.cpu = s;
    s = aide_strdup(g_fpuLabels[get_cycle(g_fpuCycle)]); free(g_editing.compiler.fpu); g_editing.compiler.fpu = s;
    g_editing.compiler.optLevel = get_cycle(g_optCycle);
    g_editing.compiler.smallData = get_check(g_smallDataCheck);
    g_editing.compiler.debugSymbols = get_check(g_debugSymbolsCheck);
    editor_to_strlist(g_includePathsEditor, &g_editing.includePaths);
    editor_to_strlist(g_definesEditor, &g_editing.defines);
    editor_to_strlist(g_extraCFlagsEditor, &g_editing.compiler.extraCFlags);

    g_editing.compiler.kick1hunks = get_check(g_kick1hunksCheck);
    g_editing.compiler.asmNoSym = get_check(g_asmNoSymCheck);
    editor_to_strlist(g_extraAsmFlagsEditor, &g_editing.compiler.extraAsmFlags);

    g_editing.linker.startup = (AideStartup)get_cycle(g_startupCycle);
    s = get_str_dup(g_customStartupStr); free(g_editing.linker.customStartupPath); g_editing.linker.customStartupPath = s;
    /* g_editing.linker.libraries is already kept live by Add/Remove --
     * nothing to read back from a widget for it. */
    editor_to_strlist(g_extraLibDirsEditor, &g_editing.linker.extraLibDirs);
    g_editing.linker.strip = get_check(g_stripCheck);
    editor_to_strlist(g_extraLdFlagsEditor, &g_editing.linker.extraLdFlags);

    aide_strlist_clear(&g_editing.sdks.selected);
    for (i = 0; i < g_sdkRowCount; ++i) {
        if (get_check(g_sdkChecks[i]))
            aide_strlist_push(&g_editing.sdks.selected, g_sdkRowIds[i]);
    }
}

/* ------------------------------------------------------------------ */
/* SDK rows                                                              */
/* ------------------------------------------------------------------ */
static int project_selects(const char *id)
{
    size_t j;
    for (j = 0; j < g_editing.sdks.selected.count; ++j) {
        if (aide_str_ieq(g_editing.sdks.selected.items[j], id))
            return 1;
    }
    return 0;
}

static char *sdk_label(const AideSdkDescriptor *d)
{
    AideStr s;
    size_t nameLen, verLen;
    int i;

    for (i = 0; i < EXTRA_SDK_COUNT; ++i) {
        if (aide_str_ieq(d->id, g_extraSdkDefs[i].id))
            return aide_strdup(g_extraSdkDefs[i].label);
    }
    aide_str_init(&s);
    aide_str_append(&s, d->displayName[0] ? d->displayName : d->id);
    nameLen = s.len;
    verLen = strlen(d->version);
    if (verLen > 0 && !(nameLen >= verLen && strcmp(s.data + nameLen - verLen, d->version) == 0)) {
        aide_str_append_char(&s, ' ');
        aide_str_append(&s, d->version);
    }
    return aide_str_take(&s);
}

/* Takes ownership of `text`. Once the group is part of the window, only
 * call this between MUIM_Group_InitChange and ExitChange. */
static void add_sdk_row(const char *id, char *text, int selected)
{
    int i = g_sdkRowCount;
    if (i >= SDK_ROWS_MAX) {
        free(text);
        return;
    }
    g_sdkRowIds[i] = aide_strdup(id);
    g_sdkRowTexts[i] = text;
    g_sdkLabels[i] = Label((STRPTR)text);
    g_sdkChecks[i] = CheckMark(selected ? TRUE : FALSE);
    DoMethod(g_sdkGroup, OM_ADDMEMBER, (ULONG)g_sdkLabels[i]);
    DoMethod(g_sdkGroup, OM_ADDMEMBER, (ULONG)g_sdkChecks[i]);
    g_sdkRowCount++;
}

static void free_sdk_row_strings(void)
{
    int i;
    for (i = 0; i < g_sdkRowCount; ++i) {
        free(g_sdkRowIds[i]);
        free(g_sdkRowTexts[i]);
    }
}

static void add_all_sdk_rows(void)
{
    AideStrList missing;
    size_t i;

    if (g_registry) {
        for (i = 0; i < g_registry->count; ++i) {
            const AideSdkDescriptor *d = &g_registry->items[i];
            add_sdk_row(d->id, sdk_label(d), project_selects(d->id));
        }
    }

    aide_strlist_init(&missing);
    if (g_registry) {
        aide_sdk_list_missing(g_registry, &g_editing.sdks.selected, &missing);
    } else {
        for (i = 0; i < g_editing.sdks.selected.count; ++i)
            aide_strlist_push(&missing, g_editing.sdks.selected.items[i]);
    }
    for (i = 0; i < missing.count; ++i) {
        AideStr text;
        aide_str_init(&text);
        aide_str_append(&text, missing.items[i]);
        aide_str_append(&text, " (nicht installiert)");
        add_sdk_row(missing.items[i], aide_str_take(&text), 1);
    }
    aide_strlist_free(&missing);
}

static void rebuild_sdk_rows(void)
{
    int i;

    DoMethod(g_sdkGroup, MUIM_Group_InitChange);
    for (i = 0; i < g_sdkRowCount; ++i) {
        DoMethod(g_sdkGroup, OM_REMMEMBER, (ULONG)g_sdkLabels[i]);
        DoMethod(g_sdkGroup, OM_REMMEMBER, (ULONG)g_sdkChecks[i]);
        MUI_DisposeObject(g_sdkLabels[i]);
        MUI_DisposeObject(g_sdkChecks[i]);
    }
    free_sdk_row_strings();
    g_sdkRowCount = 0;
    add_all_sdk_rows();
    DoMethod(g_sdkGroup, MUIM_Group_ExitChange);
}

/* ------------------------------------------------------------------ */
/* Window construction                                                   */
/* ------------------------------------------------------------------ */
static Object *make_editor(void)
{
    return TextEditorObject,
        MUIA_TextEditor_FixedFont, TRUE,
        MUIA_VertWeight, 25,
    End;
}

void settingswindow_init(Object *app, const AideSdkList *registry)
{
    Object *generalPage, *compilerPage, *assemblerPage, *linkerPage, *sdksPage;
    Object *libButtons;
    Object *register_;

    g_app = app;
    g_registry = registry;
    aide_project_init(&g_editing);
    g_haveEditing = 1;

    g_languageCycle = Cycle(g_languageLabels);
    g_outputKindCycle = Cycle(g_outputKindLabels);
    g_nameStr = StringObject, StringFrame, MUIA_String_MaxLen, 128, End;
    g_binaryStr = StringObject, StringFrame, MUIA_String_MaxLen, 64, End;
    g_targetDirStr = StringObject, StringFrame, MUIA_String_MaxLen, 128, End;
    g_objDirStr = StringObject, StringFrame, MUIA_String_MaxLen, 128, End;
    g_typeCycle = Cycle(g_typeLabels);
    g_stackStr = StringObject, StringFrame, MUIA_String_MaxLen, 16, MUIA_String_Accept, (ULONG)"0123456789", End;
    g_iconModeCycle = Cycle(g_iconModeLabels);
    g_iconPathStr = StringObject, StringFrame, MUIA_String_MaxLen, 128, End;

    generalPage = VGroup,
        Child, ColGroup(2),
            /* First row: it decides which toolchain builds the
             * project, and with it whether the Linker page applies at
             * all (an ACE program links ACE's own runtime). */
            Child, Label("Sprache:"),           Child, g_languageCycle,
            /* Eine Library wird nicht gelinkt, sondern aus ihren
            ** Objektdateien zusammengesetzt -- genau das ist eine
            ** Amiga-Link-Library (vc.lib, amiga.lib). */
            Child, Label("Ausgabe:"),           Child, g_outputKindCycle,
            Child, Label("Name:"),              Child, g_nameStr,
            Child, Label("Binary:"),            Child, g_binaryStr,
            Child, Label("Zielverzeichnis:"),   Child, g_targetDirStr,
            Child, Label("Obj-Verzeichnis:"),   Child, g_objDirStr,
            Child, Label("Typ:"),               Child, g_typeCycle,
            Child, Label("Stack-Groesse:"),     Child, g_stackStr,
            Child, Label("Icon:"),              Child, g_iconModeCycle,
            Child, Label("Icon-Datei:"),        Child, g_iconPathStr,
        End,
        Child, RectangleObject, End,
    End;

    g_cpuCycle = Cycle(g_cpuLabels);
    g_fpuCycle = Cycle(g_fpuLabels);
    g_optCycle = Cycle(g_optLabels);
    g_smallDataCheck = CheckMark(FALSE);
    g_debugSymbolsCheck = CheckMark(FALSE);
    g_includePathsEditor = make_editor();
    g_definesEditor = make_editor();
    g_extraCFlagsEditor = make_editor();

    compilerPage = VGroup,
        Child, ColGroup(2),
            Child, Label("CPU:"),            Child, g_cpuCycle,
            Child, Label("FPU:"),            Child, g_fpuCycle,
            Child, Label("Optimierung:"),    Child, g_optCycle,
            Child, Label("Small Data:"),     Child, g_smallDataCheck,
            Child, Label("Debug-Symbole:"),  Child, g_debugSymbolsCheck,
        End,
        Child, Label("Include-Pfade (eine pro Zeile):"), Child, g_includePathsEditor,
        Child, Label("Defines (eine pro Zeile):"),       Child, g_definesEditor,
        Child, Label("Extra C-Flags (eine pro Zeile):"), Child, g_extraCFlagsEditor,
    End;

    g_kick1hunksCheck = CheckMark(FALSE);
    g_asmNoSymCheck = CheckMark(FALSE);
    g_extraAsmFlagsEditor = make_editor();

    assemblerPage = VGroup,
        Child, ColGroup(2),
            Child, Label("kick1hunks:"),            Child, g_kick1hunksCheck,
            Child, Label("Keine Symbole (-nosym):"), Child, g_asmNoSymCheck,
        End,
        Child, Label("Extra Assembler-Flags (eine pro Zeile):"), Child, g_extraAsmFlagsEditor,
    End;

    g_startupCycle = Cycle(g_startupLabels);
    g_customStartupStr = StringObject, StringFrame, MUIA_String_MaxLen, 128, End;
    g_librariesList = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_Title, FALSE,
    End;
    g_librariesListview = NListviewObject, MUIA_Listview_List, g_librariesList, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;
    g_libAddBtn = SimpleButton("_Hinzufuegen");
    g_libRemoveBtn = SimpleButton("_Entfernen");
    g_libUpBtn = SimpleButton("_Hoch");
    g_libDownBtn = SimpleButton("R_unter");
    libButtons = VGroup,
        Child, g_libAddBtn, Child, g_libRemoveBtn,
        Child, g_libUpBtn, Child, g_libDownBtn,
        Child, RectangleObject, End,
    End;
    g_extraLibDirsEditor = make_editor();
    g_stripCheck = CheckMark(FALSE);
    g_extraLdFlagsEditor = make_editor();

    linkerPage = VGroup,
        Child, ColGroup(2),
            Child, Label("Startup:"),        Child, g_startupCycle,
            Child, Label("Custom-Startup:"), Child, g_customStartupStr,
            Child, Label("Strip:"),          Child, g_stripCheck,
        End,
        Child, Label("Bibliotheken (Linkreihenfolge):"),
        Child, HGroup, Child, g_librariesListview, Child, libButtons, End,
        Child, Label("Extra Lib-Verzeichnisse (eine pro Zeile):"), Child, g_extraLibDirsEditor,
        Child, Label("Extra Linker-Flags (eine pro Zeile):"),      Child, g_extraLdFlagsEditor,
    End;

    /* Filled here directly, before the group joins the window; from then
     * on rebuild_sdk_rows() replaces the rows on every show. */
    g_sdkGroup = ColGroup(2), End;
    add_all_sdk_rows();

    sdksPage = VGroup,
        Child, Label("Include- und Library-Pfade werden in dieser Reihenfolge durchsucht:"),
        Child, g_sdkGroup,
        Child, RectangleObject, End,
    End;

    register_ = RegisterObject,
        MUIA_Register_Titles, (ULONG)g_tabTitles,
        Child, generalPage,
        Child, compilerPage,
        Child, assemblerPage,
        Child, linkerPage,
        Child, sdksPage,
    End;

    g_okBtn = SimpleButton("_OK");
    g_cancelBtn = SimpleButton("_Abbrechen");

    g_window = WindowObject,
        MUIA_Window_ID,    MAKE_ID('S','E','T','T'),
        MUIA_Window_Title, "AmigaIDE - Projekteinstellungen",
        MUIA_Window_Menu,  MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, register_,
            Child, HGroup,
                Child, g_okBtn,
                Child, g_cancelBtn,
            End,
        End,
    End;

    DoMethod(g_app, OM_ADDMEMBER, g_window);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_CANCEL);
    DoMethod(g_okBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_OK);
    DoMethod(g_cancelBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_CANCEL);
    DoMethod(g_libAddBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_LIB_ADD);
    DoMethod(g_libRemoveBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_LIB_REMOVE);
    DoMethod(g_libUpBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_LIB_UP);
    DoMethod(g_libDownBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_SETTINGS_LIB_DOWN);
}

void settingswindow_free(void)
{
    /* The row objects go with g_app; only their strings are ours. */
    free_sdk_row_strings();
    g_sdkRowCount = 0;
    if (g_haveEditing) {
        aide_project_free(&g_editing);
        g_haveEditing = 0;
    }
}

void settingswindow_show(const AideProject *project)
{
    char *json = aide_project_to_json(project, NULL);
    aide_project_free(&g_editing);
    aide_project_init(&g_editing);
    aide_project_from_json(&g_editing, json);
    free(json);

    rebuild_sdk_rows();
    populate_widgets();
    set(g_window, MUIA_Window_Open, TRUE);
}

int settingswindow_handle_id(long id)
{
    switch (id) {
    case ID_SETTINGS_OK:
        apply_widgets();
        /* A project with no name or no binary name can't be saved
         * meaningfully (the "Neu" flow in particular needs a real name
         * to even know what .aproj file to write) -- reject and leave
         * the window open instead of closing/applying. */
        if (!g_editing.name[0] || !g_editing.output.binary[0]) {
            MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK",
                        "Name und Binary-Name duerfen nicht leer sein.");
            return 3; /* handled, window still open */
        }
        set(g_window, MUIA_Window_Open, FALSE);
        return 2;
    case ID_SETTINGS_CANCEL:
        set(g_window, MUIA_Window_Open, FALSE);
        return 1;
    case ID_SETTINGS_LIB_ADD: {
        char name[64];
        if (ask_lib_name(name, sizeof(name)) && name[0]) {
            aide_strlist_push(&g_editing.linker.libraries, name);
            refresh_libraries_list();
        }
        return 3; /* handled, window still open -- distinct from Cancel (1) */
    }
    case ID_SETTINGS_LIB_REMOVE: {
        LONG active = MUIV_NList_Active_Off;
        get(g_librariesList, MUIA_NList_Active, &active);
        if (active != MUIV_NList_Active_Off) {
            strlist_remove_at(&g_editing.linker.libraries, (size_t)active);
            refresh_libraries_list();
        }
        return 3; /* handled, window still open -- distinct from Cancel (1) */
    }
    case ID_SETTINGS_LIB_UP: {
        LONG active = MUIV_NList_Active_Off;
        get(g_librariesList, MUIA_NList_Active, &active);
        if (active != MUIV_NList_Active_Off && active > 0) {
            strlist_swap(&g_editing.linker.libraries, (size_t)active - 1, (size_t)active);
            refresh_libraries_list();
            set(g_librariesList, MUIA_NList_Active, (ULONG)(active - 1));
        }
        return 3;
    }
    case ID_SETTINGS_LIB_DOWN: {
        LONG active = MUIV_NList_Active_Off;
        get(g_librariesList, MUIA_NList_Active, &active);
        if (active != MUIV_NList_Active_Off && (size_t)(active + 1) < g_editing.linker.libraries.count) {
            strlist_swap(&g_editing.linker.libraries, (size_t)active, (size_t)active + 1);
            refresh_libraries_list();
            set(g_librariesList, MUIA_NList_Active, (ULONG)(active + 1));
        }
        return 3;
    }
    default:
        return 0;
    }
}

void settingswindow_take_result(AideProject *outProject)
{
    char *json = aide_project_to_json(&g_editing, NULL);
    aide_project_free(outProject);
    aide_project_init(outProject);
    aide_project_from_json(outProject, json);
    free(json);
}
