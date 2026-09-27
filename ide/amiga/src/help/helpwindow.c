#include "help/helpwindow.h"

#include <libraries/iffparse.h> /* MAKE_ID() */
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include "mui/mui_stubs.h"
#include "mui/NList_mcc.h"
#include "mui/NListview_mcc.h"
#include "mui/TextEditor_mcc.h"

#include "aide/aide_autodoc.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ID_HELP_CANDIDATE_DCLICK 2001
#define ID_HELP_BACK             2002
#define ID_HELP_FORWARD          2003
#define ID_HELP_SEARCH_SELECTION 2004

#define HELP_HISTORY_MAX 64

static Object *g_app;
static Object *g_window;
static Object *g_candidates, *g_candidatesView;
static Object *g_text;
static Object *g_backBtn, *g_forwardBtn, *g_searchSelBtn;

static AideAutodocIndex g_index;
static int g_indexReady;
static void (*g_ensureIndex)(void);

/* Back/Forward history of function names shown via helpwindow_show_function
 * (wildcard search results and helpwindow_show_text() don't participate --
 * there's no single "the" function name to remember for either). */
static char *g_history[HELP_HISTORY_MAX];
static int g_historyLen;
static int g_historyPos; /* index of the currently displayed entry, -1 if none */

static void set_text(const char *text)
{
    DoMethod(g_text, MUIM_TextEditor_ClearText);
    DoMethod(g_text, MUIM_TextEditor_InsertText, (ULONG)text, MUIV_TextEditor_InsertText_Top);
}

static void show_entry(const AideAutodocEntry *entry)
{
    char *text = aide_autodocindex_read_entry_text(entry);
    set_text(text);
    free(text);
}

/* Populates the candidate list from `entries` (aide_autodocindex_find()'s
 * result) and shows the first one, if any. */
static void show_entries(AideAutodocEntry *entries, size_t count)
{
    size_t i;

    DoMethod(g_candidates, MUIM_NList_Clear);
    for (i = 0; i < count; ++i) {
        AideStr row;
        aide_str_init(&row);
        aide_str_append(&row, entries[i].function);
        aide_str_append(&row, "  [");
        aide_str_append(&row, entries[i].library);
        aide_str_append_char(&row, ']');
        DoMethod(g_candidates, MUIM_NList_InsertSingle, (ULONG)row.data, MUIV_NList_Insert_Bottom);
        aide_str_free(&row);
    }

    if (count > 0)
        show_entry(&entries[0]);
    else
        set_text("Keine Treffer.");
}

static void lookup_and_show(const char *functionName)
{
    AideAutodocEntry *entries = NULL;
    size_t count = 0;

    if (g_indexReady)
        aide_autodocindex_find(&g_index, functionName, &entries, &count);

    show_entries(entries, count);

    if (entries) {
        size_t i;
        for (i = 0; i < count; ++i) aide_autodocentry_free(&entries[i]);
        free(entries);
    }
}

static void history_push(const char *functionName)
{
    int i;

    /* Drop any "forward" entries beyond the current position, then append. */
    for (i = g_historyPos + 1; i < g_historyLen; ++i)
        free(g_history[i]);
    g_historyLen = g_historyPos + 1;

    if (g_historyLen >= HELP_HISTORY_MAX) {
        free(g_history[0]);
        memmove(&g_history[0], &g_history[1], (size_t)(HELP_HISTORY_MAX - 1) * sizeof(char *));
        g_historyLen--;
    }

    g_history[g_historyLen++] = aide_strdup(functionName);
    g_historyPos = g_historyLen - 1;
}

void helpwindow_init(Object *app, const char *cacheDir, void (*ensureIndex)(void))
{
    g_app = app;
    g_ensureIndex = ensureIndex;
    aide_autodocindex_init(&g_index, cacheDir);
    g_indexReady = 0;
    g_historyLen = 0;
    g_historyPos = -1;

    g_candidates = NListObject,
        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
        MUIA_NList_DestructHook,  MUIV_NList_DestructHook_String,
        MUIA_NList_Title, FALSE,
    End;
    g_candidatesView = NListviewObject, MUIA_Listview_List, g_candidates, MUIA_NListview_Horiz_ScrollBar, MUIV_NListview_HSB_FullAuto, End;

    g_text = TextEditorObject,
        MUIA_TextEditor_FixedFont, TRUE,
        MUIA_TextEditor_ReadOnly,  TRUE,
    End;

    g_backBtn    = SimpleButton("_Zurueck");
    g_forwardBtn = SimpleButton("_Vor");
    g_searchSelBtn = SimpleButton("Markierung _suchen");

    g_window = WindowObject,
        MUIA_Window_ID,    MAKE_ID('H','E','L','P'),
        MUIA_Window_Title, "AmigaIDE - Hilfe",
        MUIA_Window_Menu,  MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, HGroup,
                Child, g_backBtn,
                Child, g_forwardBtn,
                Child, g_searchSelBtn,
            End,
            Child, HGroup,
                Child, VGroup,
                    MUIA_HorizWeight, 30,
                    Child, g_candidatesView,
                End,
                Child, BalanceObject, End,
                Child, VGroup,
                    Child, g_text,
                End,
            End,
        End,
    End;

    DoMethod(g_app, OM_ADDMEMBER, g_window);

    /* Secondary window: close hides it directly, no main-loop round trip
     * (the usual MUI idiom for secondary windows). */
    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_window, 3, MUIM_Set, MUIA_Window_Open, FALSE);

    DoMethod(g_candidatesView, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE,
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_CANDIDATE_DCLICK);
    DoMethod(g_backBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_BACK);
    DoMethod(g_forwardBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_FORWARD);
    DoMethod(g_searchSelBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_HELP_SEARCH_SELECTION);
}

void helpwindow_free(void)
{
    int i;
    for (i = 0; i < g_historyLen; ++i) free(g_history[i]);
    g_historyLen = 0;
    aide_autodocindex_free(&g_index);
}

void helpwindow_reload_index(const AideSdkDescriptor *activeSdks, size_t sdkCount,
                              AideAutodocProgressFn progress, void *progressCtx)
{
    aide_autodocindex_reload(&g_index, activeSdks, sdkCount, progress, progressCtx);
    g_indexReady = 1;
}

void helpwindow_show_function(const char *functionName)
{
    set(g_window, MUIA_Window_Open, TRUE);
    lookup_and_show(functionName);
    history_push(functionName);
}

void helpwindow_search_wildcard(const char *pattern)
{
    AideStrList names;
    size_t i;

    set(g_window, MUIA_Window_Open, TRUE);

    aide_strlist_init(&names);
    if (g_indexReady)
        aide_autodocindex_find_wildcard_names(&g_index, pattern, &names);

    DoMethod(g_candidates, MUIM_NList_Clear);
    for (i = 0; i < names.count; ++i)
        DoMethod(g_candidates, MUIM_NList_InsertSingle, (ULONG)names.items[i], MUIV_NList_Insert_Bottom);

    if (names.count > 0)
        lookup_and_show(names.items[0]);
    else
        set_text("Keine Treffer.");

    aide_strlist_free(&names);
}

void helpwindow_show_text(const char *title, const char *text)
{
    set(g_window, MUIA_Window_Title, title);
    set(g_window, MUIA_Window_Open, TRUE);
    DoMethod(g_candidates, MUIM_NList_Clear);
    set_text(text);
    set(g_window, MUIA_Window_Title, "AmigaIDE - Hilfe");
}

/* Looks up whatever is marked in the text pane, reduced to a query by
 * aide_autodoc_query_from_selection() -- the same rule the Qt help dock
 * uses. Nothing marked, or nothing usable in it: a beep, and the pane
 * keeps showing what it showed. */
static void search_selection(void)
{
    LONG x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    STRPTR block;
    char query[128];
    int ok;

    if (!DoMethod(g_text, MUIM_TextEditor_BlockInfo, &x1, &y1, &x2, &y2)) {
        DisplayBeep(NULL);
        return;
    }
    block = (STRPTR)DoMethod(g_text, MUIM_TextEditor_ExportBlock, 0UL, x1, y1, x2, y2);
    if (!block) {
        DisplayBeep(NULL);
        return;
    }
    ok = aide_autodoc_query_from_selection((const char *)block, query, sizeof(query));
    FreeVec(block);
    if (!ok) {
        DisplayBeep(NULL);
        return;
    }

    if (g_ensureIndex)
        g_ensureIndex();
    if (strchr(query, '*') || strchr(query, '?'))
        helpwindow_search_wildcard(query);
    else
        helpwindow_show_function(query);
}

int helpwindow_handle_id(long id)
{
    switch (id) {
    case ID_HELP_CANDIDATE_DCLICK: {
        LONG active = MUIV_NList_Active_Off;
        get(g_candidates, MUIA_NList_Active, &active);
        if (active != MUIV_NList_Active_Off) {
            char *rowText = NULL;
            DoMethod(g_candidates, MUIM_NList_GetEntry, active, &rowText);
            if (rowText) {
                /* Row text is "name  [library]" or a bare wildcard-search
                 * name -- either way the function name is everything
                 * before the first double space, or the whole string. */
                char name[128];
                char *sep = strstr(rowText, "  [");
                size_t len = sep ? (size_t)(sep - rowText) : strlen(rowText);
                if (len >= sizeof(name)) len = sizeof(name) - 1;
                memcpy(name, rowText, len);
                name[len] = '\0';
                lookup_and_show(name);
                history_push(name);
            }
        }
        return 1;
    }
    case ID_HELP_BACK:
        if (g_historyPos > 0) {
            g_historyPos--;
            lookup_and_show(g_history[g_historyPos]);
        }
        return 1;
    case ID_HELP_SEARCH_SELECTION:
        search_selection();
        return 1;
    case ID_HELP_FORWARD:
        if (g_historyPos < g_historyLen - 1) {
            g_historyPos++;
            lookup_and_show(g_history[g_historyPos]);
        }
        return 1;
    default:
        return 0;
    }
}
