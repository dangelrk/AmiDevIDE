#include "disasm/disasmwindow.h"

#include <libraries/iffparse.h> /* MAKE_ID() */
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include "mui/mui_stubs.h"
#include "mui/reqtools_stubs.h"
#include "mui/TextEditor_mcc.h"

#include "aide/aide_disasm.h"
#include "aide/aide_fs.h"
#include "aide/aide_hunk.h"
#include "aide/aide_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_DASM_SAVE 4001

static Object *g_app;
static Object *g_window;
static Object *g_text;
static Object *g_saveBtn;
static Object *g_vScroll, *g_hScroll;

void disasmwindow_init(Object *app)
{
    g_app = app;

    g_vScroll = ScrollbarObject, End;
    g_hScroll = ScrollbarObject, MUIA_Group_Horiz, TRUE, End;

    g_text = TextEditorObject,
        MUIA_TextEditor_FixedFont, TRUE,
        MUIA_TextEditor_ReadOnly,  TRUE,
        MUIA_TextEditor_WrapMode,  MUIV_TextEditor_WrapMode_NoWrap,
        /* MCC_TextEditor.doc, MUIA_TextEditor_ActiveObjectOnClick: "In
         * case the texteditor is set to ReadOnly while initialized
         * (OM_NEW) this attribute will be set to FALSE as a default."
         * Without this override, clicking/dragging in a ReadOnly editor
         * never makes it the window's active object, so a freshly made
         * selection immediately reverts to the inactive mark color and
         * loses keyboard focus -- which is exactly the "selection
         * disappears right away, can't copy anything" symptom. */
        MUIA_TextEditor_ActiveObjectOnClick, TRUE,
    End;

    g_saveBtn = SimpleButton("_Speichern...");

    g_window = WindowObject,
        MUIA_Window_ID,    MAKE_ID('D','A','S','M'),
        MUIA_Window_Title, "AmigaIDE - Disassembler",
        MUIA_Window_Menu,  MUIV_Window_Menu_NoMenu,
        WindowContents, VGroup,
            Child, HGroup,
                MUIA_Group_Spacing, 0,
                Child, g_text,
                Child, g_vScroll,
            End,
            Child, g_hScroll,
            Child, g_saveBtn,
        End,
    End;

    DoMethod(g_app, OM_ADDMEMBER, g_window);
    AIDE_REMEMBER_WINDOW(g_window);
    DoMethod(g_window, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
            g_window, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(g_saveBtn, MUIM_Notify, MUIA_Pressed, FALSE,
            g_app, 2, MUIM_Application_ReturnID, ID_DASM_SAVE);

    /* MUIA_TextEditor_Slider/HorizontalSlider must be set via set() after
     * the whole object tree exists -- passing them as construction tags
     * collapses the editor to a tiny box (same pitfall as the main
     * window's editor/output panes, see mainwindow.c). */
    set(g_text, MUIA_TextEditor_Slider, g_vScroll);
    set(g_text, MUIA_TextEditor_HorizontalSlider, g_hScroll);
}

void disasmwindow_free(void)
{
    /* g_window is disposed with the rest of the app's object tree
     * (it's an OM_ADDMEMBER child of g_app). */
}

static const char *hunk_kind_name(AideHunkKind kind)
{
    switch (kind) {
    case AIDE_HUNK_CODE: return "CODE";
    case AIDE_HUNK_DATA: return "DATA";
    case AIDE_HUNK_BSS:  return "BSS";
    default:             return "?";
    }
}

void disasmwindow_show(const char *binaryPath)
{
    char *data;
    size_t size;
    AideHunkReader reader;
    AideStr out;
    size_t h;

    aide_str_init(&out);

    if (!aide_fs_read_file(binaryPath, &data, &size)) {
        aide_str_appendf(&out, "Konnte \"%s\" nicht lesen.\n", binaryPath);
        goto show;
    }

    aide_hunkreader_init(&reader);
    if (!aide_hunkreader_load_bytes(&reader, (const unsigned char *)data, size)) {
        aide_str_appendf(&out, "Kein g\374ltiges Hunk-Executable: %s\n", reader.error);
        aide_hunkreader_free(&reader);
        free(data);
        goto show;
    }
    free(data);

    for (h = 0; h < reader.sectionCount; ++h) {
        const AideHunkSection *sec = &reader.sections[h];

        aide_str_appendf(&out, "Hunk %d (%s), %lu Bytes:\n", sec->index, hunk_kind_name(sec->kind), sec->sizeBytes);

        if (sec->kind == AIDE_HUNK_CODE && sec->data) {
            int offset = 0;
            while ((size_t)offset < sec->dataSize) {
                AideDisasmInstruction ins;
                aide_disasm_decode_one(sec->data, sec->dataSize, &offset, 0, &ins);
                aide_str_appendf(&out, "  %06lx: %-12s %s\n", ins.address, ins.rawHex, ins.text);
                aide_disasm_instruction_free(&ins);
            }
        }
        aide_str_append_char(&out, '\n');
    }
    aide_hunkreader_free(&reader);

show:
    DoMethod(g_text, MUIM_TextEditor_ClearText);
    DoMethod(g_text, MUIM_TextEditor_InsertText, (ULONG)out.data, MUIV_TextEditor_InsertText_Top);
    set(g_text, MUIA_TextEditor_HasChanged, FALSE);
    aide_str_free(&out);

    set(g_window, MUIA_Window_Open, TRUE);
}

static void do_disasm_save(void)
{
    struct rtFileRequester *filereq;
    char filename[108];
    char path[512];
    struct TagItem tags[2];
    LONG ok;
    STRPTR text;
    FILE *f;

    filereq = (struct rtFileRequester *)rtAllocRequestA(RT_FILEREQ, NULL);
    if (!filereq)
        return;

    filename[0] = '\0';
    tags[0].ti_Tag = RTFI_Flags; tags[0].ti_Data = FREQF_SAVE;
    tags[1].ti_Tag = TAG_DONE;   tags[1].ti_Data = 0;

    ok = rtFileRequestA(filereq, (STRPTR)filename, (CONST_STRPTR)"Disassemblierung speichern", tags);
    if (ok) {
        strncpy(path, (char *)filereq->Dir, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        AddPart((STRPTR)path, (STRPTR)filename, (ULONG)sizeof(path));
    }
    rtFreeRequest(filereq);
    if (!ok)
        return;

    text = (STRPTR)DoMethod(g_text, MUIM_TextEditor_ExportText);
    if (!text)
        return;

    f = fopen(path, "wb");
    if (f) {
        fwrite(text, 1, strlen((char *)text), f);
        fclose(f);
    } else {
        MUI_Request(g_app, g_window, 0, "AmigaIDE", "_OK", "Konnte Datei nicht speichern:\n%s", path);
    }
    FreeVec(text);
}

int disasmwindow_handle_id(long id)
{
    if (id != ID_DASM_SAVE)
        return 0;
    do_disasm_save();
    return 1;
}
