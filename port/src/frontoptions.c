/*
 * frontoptions.c -- D343: PC options on the file-select screen.
 *
 * GE's front end has no generic menu system and no options screen, so this is
 * the port's own screen, drawn in GE's style (route B,
 * docs/dev/OPTIONS-MENU-PLAN.md). It has two parts:
 *
 *   - an "Options" label next to the file-select menu, and
 *   - a settings panel over the folders, one tab per section of the F10
 *     overlay's row table (Display / View / Mouse & Aim / Controller / Game).
 *
 * Both are drawn from the single #ifdef PORT hook in
 * constructor_menu05_fileselect (src/game/front.c), inside GE's own frame, so
 * they fade with screen transitions and get the D335 pillarbox for free. The
 * game's crosshair is drawn after the hook and works as the pointer, exactly as
 * in every other GE menu: while the panel is open input.c still passes the
 * stick through (mouse and stick move the crosshair) but hands the BUTTONS to
 * frontOptionsFeedPad() instead of the game.
 *
 * Threads: the hook runs on the game's main thread and owns all of this
 * file's UI state. input.c (scheduler thread) only publishes button edges
 * through atomics. The settings themselves go through optionsoverlay.c's row
 * API (rowSet does the live-apply requests, which are thread-safe posts).
 */

#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "platform.h"
#include "system.h"
#include "config.h"
#include "envflag.h"
#include "optionsoverlay.h"
#include "frontoptions.h"

/* ---- game symbols (see optionsoverlay.c / input.c for the same pattern) ---- */
struct font;
struct fontchar;
extern struct font     *ptrFontZurichBold;
extern struct fontchar *ptrFontZurichBoldChars;
extern struct font     *ptrFontBankGothic;
extern struct fontchar *ptrFontBankGothicChars;
extern Gfx  *microcode_constructor(Gfx *gdl);
extern Gfx  *microcode_constructor_related_to_menus(Gfx *gdl, s32 ulx, s32 uly,
                                                    s32 lrx, s32 lry, u32 color);
extern Gfx  *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars,
                        struct font *font, u32 colour, s32 width, s32 height,
                        u32 yOffset, s32 lineheight);
extern void  textMeasure(s32 *textheight, s32 *textwidth, char *text,
                         struct fontchar *chars, struct font *font, s32 lineheight);
extern s16   viGetX(void);
extern s16   viGetY(void);
extern float cursor_h_pos;
extern float cursor_v_pos;
extern s32   folder_selected_for_deletion;
extern int   menu_update;                     /* MENU enum (signed, -1 = none) */
extern s32   g_MenuTimer;
extern u16   joyGetButtonsPressedThisFrame(s8 contpadnum, u16 mask);
extern ALBank *g_musicSfxBufferPtr;             /* music.h; sndPlaySfx comes via snd.h */

/* N64 controller bits (os.h is not included here, see porting-notes C2). */
#define FO_A      0x8000
#define FO_B      0x4000
#define FO_Z      0x2000
#define FO_START  0x1000
#define FO_UP     0x0800
#define FO_DOWN   0x0400
#define FO_LEFT   0x0200
#define FO_RIGHT  0x0100
#define FO_L      0x0020
#define FO_R      0x0010
#define FO_CUP    0x0008
#define FO_CDOWN  0x0004
#define FO_CLEFT  0x0002
#define FO_CRIGHT 0x0001
#define FO_CONFIRM (FO_A | FO_Z | FO_START)          /* as front.c */

#define FO_MENU_INVALID   (-1)
#define FO_SFX_CLICK      222   /* DOOR_LOCK_SFX: Copy/Erase's click */
#define FO_SFX_BACK       118   /* GUN_M60AMMGUN_3_SFX: Copy/Erase's cancel */

/* ---- colours (RRGGBBAA) ---- */
#define COL_WHITE   0xFFFFFFFFu
#define COL_GOLD    0xEBD879FFu   /* GE's folder text colour */
#define COL_GREY    0x9A9A9AFFu
#define COL_DIM     0x6A6A6AFFu

/* ---- layout, in the front end's 440x330 canvas ---- */
#define LABEL_RIGHT 392   /* the Erase text's right edge */
#define LABEL_Y     24    /* dark strip above the right-hand folder */
#define HIT_PAD     4

#define PNL_X0   24
#define PNL_X1   416
#define PNL_Y0   14
#define PNL_Y1   268
#define TITLE_X  38
#define TITLE_Y  22
#define TAB_Y    44
#define TAB_GAP  16
#define TAB_X1   (PNL_X1 - 12)   /* right edge the tab row must fit in */
#define ROWS_Y   68
#define ROW_H    18
#define LBL_X    40
#define VAL_R    402
#define BAR_X0   236
#define BAR_X1   346
#define FOOT_Y   248

static const char kLabel[]   = "Options";     /* ASCII only: issue #87 / D295 */
static const char kLabelNL[] = "Options\n";
static const char kBack[]    = "Back";

/* ---- state shared with input.c ---- */
static SDL_atomic_t s_open;          /* 1 while the panel is open            */
static SDL_atomic_t s_pressed;       /* button edges since the last frame    */
static SDL_atomic_t s_held;          /* buttons held at the last poll        */

/* ---- UI state (game thread only) ---- */
#define MAX_PAGES  8
#define MAX_PROWS  24
static int s_page = 0;
static int s_sel  = -1;              /* index into the current page's rows    */
static int s_dragRow = -1;           /* row index (global) being dragged      */
static float s_lastCurX = -1.0f, s_lastCurY = -1.0f;
static int s_repeatDir = 0, s_repeatTimer = 0;
static int s_waitRelease = 0;       /* ignore buttons until all are released */

static int s_pageHdr[MAX_PAGES];     /* global row index of each header      */
static int s_pageN = 0;
static int s_rowIdx[MAX_PROWS];      /* current page's visible rows (global) */
static int s_rowN = 0;

/* Tab geometry, filled when drawn and hit-tested next frame. */
static s32 s_tabX0[MAX_PAGES], s_tabX1[MAX_PAGES];
static s32 s_backX0, s_backX1;

/* ------------------------------------------------------------------------ */

int frontOptionsIsOpen(void)
{
    return SDL_AtomicGet(&s_open) != 0;
}

/* input.c, scheduler thread: called with the pad-0 buttons while the panel is
 * open (the game gets 0), or with 0 while something else owns the pad. */
void frontOptionsFeedPad(unsigned held)
{
    unsigned prev = (unsigned)SDL_AtomicSet(&s_held, (int)held);
    unsigned edge = held & ~prev;
    if (edge) {
        int old;
        do {
            old = SDL_AtomicGet(&s_pressed);
        } while (!SDL_AtomicCAS(&s_pressed, old, old | (int)edge));
    }
}

static void playSfx(int id)
{
    sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, (s16)id, NULL);
}

static s32 measureW(const char *str, int bank)
{
    s32 h = 0, w = 0;
    if (bank) textMeasure(&h, &w, (char *)str, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    else      textMeasure(&h, &w, (char *)str, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    return w;
}

static Gfx *text(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour, int bank)
{
    if (bank) {
        return textRender(gdl, &x, &y, (char *)str, ptrFontBankGothicChars,
                          ptrFontBankGothic, colour, viGetX(), viGetY(), 0, 0);
    }
    return textRender(gdl, &x, &y, (char *)str, ptrFontZurichBoldChars,
                      ptrFontZurichBold, colour, viGetX(), viGetY(), 0, 0);
}

static Gfx *textR(Gfx *gdl, s32 xr, s32 y, const char *str, u32 colour)
{
    return text(gdl, xr - measureW(str, 0), y, str, colour, 0);
}

/* The game's own menu box helper (the folder tabs' highlight). */
static Gfx *box(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba)
{
    return microcode_constructor_related_to_menus(gdl, x0, y0, x1, y1, rgba);
}

static int inRect(float x, float y, s32 x0, s32 y0, s32 x1, s32 y1)
{
    return x >= (float)x0 && x <= (float)x1 && y >= (float)y0 && y <= (float)y1;
}

/* ------------------------------------------------------------------------ */

static void buildPages(void)
{
    int n = optionsRowCount();
    s_pageN = 0;
    for (int i = 0; i < n && s_pageN < MAX_PAGES; i++) {
        if (optionsRowIsHeader(i)) {
            s_pageHdr[s_pageN++] = i;
        }
    }
    if (s_page >= s_pageN) {
        s_page = 0;
    }

    /* Visible rows of the current page (auto toggles can hide % rows). */
    s_rowN = 0;
    if (s_pageN > 0) {
        for (int i = s_pageHdr[s_page] + 1; i < n && !optionsRowIsHeader(i); i++) {
            if (optionsRowIsShown(i) && s_rowN < MAX_PROWS) {
                s_rowIdx[s_rowN++] = i;
            }
        }
    }
    if (s_sel >= s_rowN) {
        s_sel = s_rowN - 1;
    }
}

static void setPage(int page)
{
    if (s_pageN <= 0) {
        return;
    }
    s_page = (page + s_pageN) % s_pageN;
    s_sel = 0;
    s_dragRow = -1;
    buildPages();
}

static void openPanel(void)
{
    SDL_AtomicSet(&s_pressed, 0);
    SDL_AtomicSet(&s_open, 1);
    /* The press that opened the panel is usually still held when input.c
     * starts routing buttons here, and would register as a fresh press on
     * the first row. Ignore everything until the buttons are released. */
    s_waitRelease = 1;
    s_sel = 0;
    s_dragRow = -1;
    s_lastCurX = cursor_h_pos;
    s_lastCurY = cursor_v_pos;
    buildPages();
    sysLogPrintf(LOG_INFO, "frontoptions: opened");
}

static void closePanel(void)
{
    SDL_AtomicSet(&s_open, 0);
    s_dragRow = -1;
    configSave();
    sysLogPrintf(LOG_INFO, "frontoptions: closed");
}

static int rowY(int k)
{
    return ROWS_Y + k * ROW_H;
}

/* Row under the cursor (page-local index), or -1. */
static int hoverRow(void)
{
    for (int k = 0; k < s_rowN; k++) {
        if (inRect(cursor_h_pos, cursor_v_pos, PNL_X0 + 6, rowY(k) - 2,
                   PNL_X1 - 6, rowY(k) + ROW_H - 3)) {
            return k;
        }
    }
    return -1;
}

static double barFraction(float x)
{
    return ((double)x - BAR_X0) / (double)(BAR_X1 - BAR_X0);
}

/* One frame of panel input (game thread). */
static void panelInput(void)
{
    unsigned pressed = (unsigned)SDL_AtomicSet(&s_pressed, 0);
    unsigned held    = (unsigned)SDL_AtomicGet(&s_held);

    if (s_waitRelease) {
        if (held != 0) {
            return;
        }
        s_waitRelease = 0;
        pressed = 0;
    }

    buildPages();

    /* Hover follows the crosshair while it moves; the D-pad moves the
     * selection without moving the crosshair. */
    int hov = hoverRow();
    if (cursor_h_pos != s_lastCurX || cursor_v_pos != s_lastCurY) {
        if (hov >= 0) s_sel = hov;
        s_lastCurX = cursor_h_pos;
        s_lastCurY = cursor_v_pos;
    }
    if (pressed & (FO_UP | FO_CUP))   { if (s_sel > 0) s_sel--; }
    if (pressed & (FO_DOWN | FO_CDOWN)) { if (s_sel < s_rowN - 1) s_sel++; }

    if (pressed & FO_B) {
        playSfx(FO_SFX_BACK);
        closePanel();
        return;
    }
    if (pressed & FO_L) { setPage(s_page - 1); playSfx(FO_SFX_CLICK); }
    if (pressed & FO_R) { setPage(s_page + 1); playSfx(FO_SFX_CLICK); }

    if (pressed & FO_CONFIRM) {
        for (int p = 0; p < s_pageN; p++) {
            if (inRect(cursor_h_pos, cursor_v_pos, s_tabX0[p] - 4, TAB_Y - 3,
                       s_tabX1[p] + 4, TAB_Y + 14)) {
                if (p != s_page) setPage(p);
                playSfx(FO_SFX_CLICK);
                return;
            }
        }
        if (inRect(cursor_h_pos, cursor_v_pos, s_backX0 - 4, FOOT_Y - 3,
                   s_backX1 + 4, FOOT_Y + 14)) {
            playSfx(FO_SFX_BACK);
            closePanel();
            return;
        }
        /* Act on the highlighted row. The highlight follows the crosshair
         * whenever it moves, so a click lands on the row under it; a pad
         * user's D-pad choice isn't overridden by an idle crosshair. */
        int k = s_sel;
        if (k >= 0 && k < s_rowN) {
            int i = s_rowIdx[k];
            if (hov == k && optionsRowIsSlider(i) &&
                cursor_h_pos >= BAR_X0 - 4 && cursor_h_pos <= BAR_X1 + 4) {
                optionsRowSetFraction(i, barFraction(cursor_h_pos));
                s_dragRow = i;
            } else {
                optionsRowAdjust(i, +1);   /* toggle / cycle / step (wraps) */
            }
            playSfx(FO_SFX_CLICK);
            buildPages();                  /* an auto toggle may hide rows */
        }
    }

    /* Drag a slider while the confirm button is held. */
    if (s_dragRow >= 0) {
        if (held & FO_CONFIRM) {
            optionsRowSetFraction(s_dragRow, barFraction(cursor_h_pos));
        } else {
            s_dragRow = -1;
        }
    }

    /* Left/right adjust the selected row, with hold-to-repeat. */
    int dir = 0;
    if (held & (FO_LEFT | FO_CLEFT))  dir = -1;
    if (held & (FO_RIGHT | FO_CRIGHT)) dir = +1;
    if (dir != 0 && s_sel >= 0 && s_sel < s_rowN) {
        int fire = 0;
        if (dir != s_repeatDir) {
            fire = 1;
            s_repeatTimer = 18;
        } else if (--s_repeatTimer <= 0) {
            fire = 1;
            s_repeatTimer = 4;
        }
        if (fire) {
            optionsRowAdjust(s_rowIdx[s_sel], dir);
            buildPages();
        }
    }
    s_repeatDir = dir;
}

/* ------------------------------------------------------------------------ */

/* Tab name for a section header: "MOUSE / AIM" -> "MOUSE/AIM". */
static void tabName(const char *hdr, char *out, int n)
{
    int j = 0;
    for (int i = 0; hdr[i] && j < n - 1; i++) {
        if (hdr[i] == ' ' && (hdr[i + 1] == '/' || (i > 0 && hdr[i - 1] == '/'))) {
            continue;
        }
        out[j++] = hdr[i];
    }
    out[j] = '\0';
}

static Gfx *drawPanel(Gfx *gdl)
{
    char val[48];

    /* Backing: a near-opaque dark sheet over the folders, gold rule lines. */
    gdl = box(gdl, PNL_X0, PNL_Y0, PNL_X1, PNL_Y1, 0x0C0C0CEE);
    gdl = box(gdl, PNL_X0, PNL_Y0, PNL_X1, PNL_Y0 + 1, 0xEBD87999);
    gdl = box(gdl, PNL_X0, PNL_Y1 - 1, PNL_X1, PNL_Y1, 0xEBD87999);
    gdl = box(gdl, PNL_X0 + 10, TAB_Y + 18, PNL_X1 - 10, TAB_Y + 19, 0xEBD87955);

    gdl = microcode_constructor(gdl);
    gdl = text(gdl, TITLE_X, TITLE_Y, "PC OPTIONS", COL_GOLD, 1);

    /* Tabs: section names from the row headers, spread to fit the panel. */
    {
        char names[MAX_PAGES][24];
        s32 widths[MAX_PAGES], total = 0, gap = TAB_GAP;
        for (int p = 0; p < s_pageN; p++) {
            tabName(optionsRowLabel(s_pageHdr[p]), names[p], sizeof(names[p]));
            widths[p] = measureW(names[p], 0);
            total += widths[p];
        }
        if (s_pageN > 1) {
            gap = (TAB_X1 - TITLE_X - total) / (s_pageN - 1);
            if (gap > TAB_GAP) gap = TAB_GAP;
            if (gap < 4) gap = 4;
        }
        s32 x = TITLE_X;
        for (int p = 0; p < s_pageN; p++) {
            s32 w = widths[p];
            int hot = inRect(cursor_h_pos, cursor_v_pos, x - 4, TAB_Y - 3, x + w + 4, TAB_Y + 14);
            s_tabX0[p] = x;
            s_tabX1[p] = x + w;
            if (p == s_page) {
                gdl = box(gdl, x, TAB_Y + 14, x + w, TAB_Y + 16, 0xEBD879FF);
                gdl = microcode_constructor(gdl);
            }
            gdl = text(gdl, x, TAB_Y, names[p],
                       p == s_page ? COL_GOLD : (hot ? COL_WHITE : COL_GREY), 0);
            x += w + gap;
        }
    }

    /* Rows. */
    for (int k = 0; k < s_rowN; k++) {
        int i = s_rowIdx[k];
        int y = rowY(k);
        int sel = (k == s_sel);
        u32 col = sel ? COL_GOLD : COL_WHITE;

        if (sel) {
            gdl = box(gdl, PNL_X0 + 8, y - 2, PNL_X1 - 8, y + ROW_H - 4, 0xEBD8792A);
            gdl = microcode_constructor(gdl);
        }
        gdl = text(gdl, LBL_X, y, optionsRowLabel(i), col, 0);

        optionsRowValueText(i, val, sizeof(val));
        if (optionsRowIsSlider(i)) {
            s32 fx = BAR_X0 + (s32)((BAR_X1 - BAR_X0) * optionsRowFraction(i) + 0.5);
            gdl = box(gdl, BAR_X0, y + 5, BAR_X1, y + 9, 0x404040FF);
            gdl = box(gdl, BAR_X0, y + 5, fx, y + 9, sel ? 0xEBD879FF : 0xB0A060FF);
            gdl = microcode_constructor(gdl);
        } else if (optionsRowNeedsRestart(i)) {
            gdl = text(gdl, BAR_X0, y, "(restart)", COL_DIM, 0);
        }
        if (val[0]) {
            gdl = textR(gdl, VAL_R, y, val, col);
        }
    }

    /* Footer: hints + Back. */
    gdl = text(gdl, TITLE_X, FOOT_Y, "L / R: section", COL_DIM, 0);
    {
        s32 w = measureW(kBack, 0);
        s_backX1 = VAL_R;
        s_backX0 = VAL_R - w;
        int hot = inRect(cursor_h_pos, cursor_v_pos, s_backX0 - 4, FOOT_Y - 3, s_backX1 + 4, FOOT_Y + 14);
        gdl = text(gdl, s_backX0, FOOT_Y, kBack, hot ? COL_GOLD : COL_WHITE, 0);
    }
    return microcode_constructor(gdl);   /* leave the text state the icons expect */
}

/* ------------------------------------------------------------------------ */

/* The one #ifdef PORT hook in constructor_menu05_fileselect (game thread),
 * called after the Copy/Erase labels and before their icons + the cursor. */
Gfx *optionsFileSelectLabel(Gfx *gdl)
{
    s32 h = 0, w = 0, unusedw = 0;
    s32 x, y;
    int open = frontOptionsIsOpen();
    int hot;

    static int autoOpenChecked = 0;   /* GE_FRONTOPTIONS=1: open at first draw (headless) */
    if (!autoOpenChecked) {
        autoOpenChecked = 1;
        if (GE_ENVFLAG("GE_FRONTOPTIONS")) {
            openPanel();
            open = 1;
        }
    }

    /* A screen that pads the controller away from front.c must also hold its
     * idle timer, or file select drops to the legal screen after 30 s. This
     * hook only runs on MENU_FILE_SELECT, the one screen where g_MenuTimer is
     * an idle timer (elsewhere it is the intro / cast-roll clock). Same write
     * front.c makes when a button is pressed. */
    if (open || optionsOverlayIsOpen()) {
        g_MenuTimer = 0;
    }

    if (open) {
        panelInput();
        open = frontOptionsIsOpen();
    }

    /* textMeasure only counts height for completed lines, so measure the
     * height with a trailing newline (front.c's folder text does the same). */
    textMeasure(&unusedw, &w, (char *)kLabel, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    textMeasure(&h, &unusedw, (char *)kLabelNL, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = LABEL_RIGHT - w;
    y = LABEL_Y;

    hot = !open
       && !optionsOverlayIsOpen()
       && menu_update == FO_MENU_INVALID
       && folder_selected_for_deletion < 0
       && inRect(cursor_h_pos, cursor_v_pos, x - HIT_PAD, y - HIT_PAD,
                 x + w + HIT_PAD, y + h + HIT_PAD);

    if (hot && joyGetButtonsPressedThisFrame(0, FO_CONFIRM)) {
        playSfx(FO_SFX_CLICK);
        openPanel();
        open = 1;
    }

    if (open) {
        gdl = drawPanel(gdl);
        return gdl;   /* the panel's title replaces the label */
    }

    /* Same font and white as Copy/Erase; gold (the folder text colour) while
     * the cursor is over it. Text state is still the Copy/Erase text's. */
    return textRender(gdl, &x, &y, (char *)kLabel, ptrFontZurichBoldChars,
                      ptrFontZurichBold, hot ? COL_GOLD : COL_WHITE,
                      viGetX(), viGetY(), 0, 0);
}
