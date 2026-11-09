/*
 * splash.c -- startup splash window, see splash.h.
 *
 * Deliberately not MUI: the point is to appear before muimaster.library
 * and the MCCs are opened. Intuition and graphics.library only, both in
 * ROM, so OpenWindow() is all that stands between starting the program
 * and seeing something.
 *
 * Borderless, centred on the default public screen, no IDCMP. Smart
 * refresh lets layers restore it when something passes over it, so it
 * never has to handle a refresh event -- nobody would be listening while
 * the IDE is busy starting up anyway.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <string.h>

#include "app/splash.h"

struct GfxBase *GfxBase;

static struct Window   *g_win;
static struct DrawInfo *g_dri;
static struct Screen   *g_scr;

/* Layout, in window coordinates, computed once in splash_open(). */
static WORD g_margin;
static WORD g_statusY;  /* top of the status text line */
static WORD g_barX, g_barY, g_barW, g_barH;

#define SPLASH_TITLE "AmigaIDE"
/* Width of the status line in average characters; long texts are cut. */
#define SPLASH_COLUMNS 44

static UWORD pen(int which)
{
    return g_dri->dri_Pens[which];
}

/* Draws `text` with its top at y, cut to fit [x, x+maxw). */
static void draw_text(struct RastPort *rp, WORD x, WORD y, WORD maxw, const char *text)
{
    struct TextExtent te;
    ULONG len = strlen(text);
    ULONG fit = TextFit(rp, (STRPTR)text, len, &te, NULL, 1, maxw, 32767);
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)text, fit);
}

/* A recessed 3D frame around (x, y, w, h), in the screen's own pens. */
static void draw_recessed(struct RastPort *rp, WORD x, WORD y, WORD w, WORD h)
{
    SetAPen(rp, pen(SHADOWPEN));
    Move(rp, x, y + h - 1);
    Draw(rp, x, y);
    Draw(rp, x + w - 1, y);
    SetAPen(rp, pen(SHINEPEN));
    Move(rp, x + w - 1, y + 1);
    Draw(rp, x + w - 1, y + h - 1);
    Draw(rp, x + 1, y + h - 1);
}

void splash_open(const char *version)
{
    struct RastPort *srp;
    struct TagItem tags[9];
    WORD fh, colw, textw, w, h, y;

    if (g_win)
        return;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 37);
    if (!GfxBase)
        return;
    g_scr = LockPubScreen(NULL);
    if (!g_scr)
        goto fail;
    g_dri = GetScreenDrawInfo(g_scr);
    if (!g_dri)
        goto fail;

    /* Measured in the screen's RastPort: the window inherits its font. */
    srp = &g_scr->RastPort;
    fh = srp->TxHeight;
    colw = TextLength(srp, (STRPTR)"n", 1);
    textw = colw * SPLASH_COLUMNS;
    {
        WORD vw = TextLength(srp, (STRPTR)version, strlen(version));
        if (vw > textw)
            textw = vw;
    }
    g_margin = fh;
    w = textw + 2 * g_margin;

    y = g_margin;
    y += fh + fh / 2;          /* title */
    y += fh + fh;              /* version, then a gap */
    g_statusY = y;
    y += fh + fh / 2;          /* status */
    g_barX = g_margin;
    g_barY = y;
    g_barW = textw;
    g_barH = fh / 2 + 4;
    y += g_barH + g_margin;
    h = y;

    tags[0].ti_Tag = WA_Left;         tags[0].ti_Data = (g_scr->Width - w) / 2;
    tags[1].ti_Tag = WA_Top;          tags[1].ti_Data = (g_scr->Height - h) / 2;
    tags[2].ti_Tag = WA_Width;        tags[2].ti_Data = w;
    tags[3].ti_Tag = WA_Height;       tags[3].ti_Data = h;
    tags[4].ti_Tag = WA_PubScreen;    tags[4].ti_Data = (ULONG)g_scr;
    tags[5].ti_Tag = WA_Borderless;   tags[5].ti_Data = TRUE;
    tags[6].ti_Tag = WA_SmartRefresh; tags[6].ti_Data = TRUE;
    tags[7].ti_Tag = WA_Activate;     tags[7].ti_Data = FALSE;
    tags[8].ti_Tag = TAG_DONE;        tags[8].ti_Data = 0;
    g_win = OpenWindowTagList(NULL, tags);
    if (!g_win)
        goto fail;

    {
        struct RastPort *rp = g_win->RPort;
        SetDrMd(rp, JAM1);

        SetAPen(rp, pen(BACKGROUNDPEN));
        RectFill(rp, 0, 0, w - 1, h - 1);
        /* Raised outer frame. */
        SetAPen(rp, pen(SHINEPEN));
        Move(rp, 0, h - 1);
        Draw(rp, 0, 0);
        Draw(rp, w - 1, 0);
        SetAPen(rp, pen(SHADOWPEN));
        Move(rp, w - 1, 1);
        Draw(rp, w - 1, h - 1);
        Draw(rp, 1, h - 1);

        SetAPen(rp, pen(HIGHLIGHTTEXTPEN));
        SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
        draw_text(rp, g_margin, g_margin, textw, SPLASH_TITLE);
        SetSoftStyle(rp, FS_NORMAL, AskSoftStyle(rp));

        SetAPen(rp, pen(TEXTPEN));
        draw_text(rp, g_margin, g_margin + fh + fh / 2, textw, version);

        draw_recessed(rp, g_barX, g_barY, g_barW, g_barH);
    }
    /* The screen stays locked while the window is open: a visitor window
     * holds its screen open anyway, and splash_close() unlocks it. */
    return;

fail:
    splash_close();
}

void splash_status(const char *text, int percent)
{
    struct RastPort *rp;
    WORD innerW, fill;

    if (!g_win)
        return;
    rp = g_win->RPort;

    SetAPen(rp, pen(BACKGROUNDPEN));
    RectFill(rp, g_margin, g_statusY, g_margin + g_barW - 1, g_statusY + rp->TxHeight - 1);
    SetAPen(rp, pen(TEXTPEN));
    draw_text(rp, g_margin, g_statusY, g_barW, text);

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    innerW = g_barW - 2;
    fill = (WORD)((long)innerW * percent / 100);
    if (fill > 0) {
        SetAPen(rp, pen(FILLPEN));
        RectFill(rp, g_barX + 1, g_barY + 1, g_barX + fill, g_barY + g_barH - 2);
    }
    if (fill < innerW) {
        SetAPen(rp, pen(BACKGROUNDPEN));
        RectFill(rp, g_barX + 1 + fill, g_barY + 1, g_barX + innerW, g_barY + g_barH - 2);
    }
}

void splash_close(void)
{
    if (g_win) {
        CloseWindow(g_win);
        g_win = NULL;
    }
    if (g_dri) {
        FreeScreenDrawInfo(g_scr, g_dri);
        g_dri = NULL;
    }
    if (g_scr) {
        UnlockPubScreen(NULL, g_scr);
        g_scr = NULL;
    }
    if (GfxBase) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
}
