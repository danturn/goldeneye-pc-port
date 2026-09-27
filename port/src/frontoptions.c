/*
 * frontoptions.c -- D343: PC options as a GE front-end screen.
 *
 * GE's front end has no generic menu system, so PC settings get their own
 * screen, MENU_PC_OPTIONS (route B, docs/dev/OPTIONS-MENU-PLAN.md), built the
 * way GE builds its Cheat Options screen (front.c init/interface/constructor_
 * menu15_cheat): the open dossier with tabs and a blank CLASSIFIED page, black
 * Zurich text on the paper via frontPrintText, GE's translucent highlight
 * boxes, the PREVIOUS tab as "back", and the game's crosshair.
 *
 *   page 0: a numbered section list (like mode select): 1. Display ... 5. Game
 *   page 1: one section's rows (like the cheat list), value column on the right
 *
 * The sections and rows are the F10 overlay's row table, reached through the
 * row API in optionsoverlay.c, so both UIs edit the same settings.
 *
 * Game-code surface (Rule 2, user sign-off 2026-09-27): the MENU_PC_OPTIONS
 * enum value (bondconstants.h) and five one-line dispatch cases (front.c),
 * all #ifdef PORT. Entry is the "PC Options" label on file select, drawn by
 * the existing D343 hook in constructor_menu05_fileselect.
 *
 * Everything here runs on the game's main thread (the front-end tick and
 * constructor), like GE's own screens.
 */

#include <string.h>
#include <stdio.h>

#include <ultra64.h>
#include <bondgame.h>
#include <bondconstants.h>
#include <fr.h>
#include <joy.h>
#include <music.h>
#include <snd.h>
#include "front.h"
#include "textrelated.h"

#include "platform.h"
#include "system.h"
#include "config.h"
#include "envflag.h"
#include "optionsoverlay.h"
#include "frontoptions.h"

/* front.c functions this screen shares with the cheat screen (not all are in
 * front.h). */
extern Gfx *frontSetupMenuBackground(Gfx *DL);
extern Gfx *frontAddPreviousTabText(Gfx *DL);
extern s32  frontCheckCursorOnPreviousTab(void);
extern void frontUpdateControlStickPosition(void);
extern Gfx *frontDrawCursor(Gfx *DL);
extern Gfx *frontPrintText(Gfx *gdl, s32 *x, s32 *y, s8 *text, s32 second_font_table,
                           s32 first_font_table, s32 arg6, s32 view_x, s32 view_y,
                           s32 arg9, s32 arga);
extern void load_walletbond(void);
extern void disable_all_switches(Model *arg0);                                   /* front.c:967 */
extern void set_item_visibility_in_objinstance(Model *objinstance, s32 item, s32 mode); /* front.c:968 */
extern s32  folder_selection_screen_option_icon;

/* ---- colours on the paper (RRGGBBAA; 0xFF = opaque black, GE's ink) ---- */
#define INK        0x000000FFu
#define INK_ON     0xA00000FFu   /* the cheat screen's ON red */
#define INK_DIM    0x00000080u
#define HILITE     0x32          /* GE's menu highlight box (black, alpha 0x32) */

/* ---- layout, in the front end's 440x330 canvas (paper; tabs start at 390) ---- */
#define TITLE_Y    0x2B
#define ROW_X      0x37          /* the cheat list's x */
#define ROW_Y0     0x41          /* cheat list starts 0x35; +12 for the title line */
#define ROW_DY     0x14          /* the cheat list's row pitch */
#define NUM_X      0x37          /* "1." on the section list */
#define SEC_X      0x4B
#define VAL_R      372           /* values right-aligned here */
#define BAR_X0     232
#define BAR_X1     316
#define ROW_HIT_X0 40.0f
#define ROW_HIT_X1 385.0f

/* ---- file-select label ---- */
#define LABEL_RIGHT 392
#define LABEL_Y     24
#define HIT_PAD     4

#define MAX_PAGES  8
#define MAX_PROWS  12            /* the paper fits 12 rows, like the cheat list */

static const char kLabel[]   = "Options";     /* ASCII only: issue #87 / D295 */
static const char kLabelNL[] = "Options\n";

/* ---- screen state (game thread) ---- */
static int s_level = 0;          /* 0 = section list, 1 = a section's rows */
static int s_page = 0;           /* current section */
static int s_hl = -1;            /* highlighted row / section, -1 = none */
static int s_dragRow = -1;       /* global row index being dragged */
static int s_repeatDir = 0, s_repeatTimer = 0;

static int s_pageHdr[MAX_PAGES];
static int s_pageN = 0;
static int s_rowIdx[MAX_PROWS];
static int s_rowN = 0;

/* ------------------------------------------------------------------------ */

static void playSfx(s16 id)
{
    sndPlaySfx((struct ALBankAlt_s *)g_musicSfxBufferPtr, id, NULL);
}

static s32 measureW(const char *str)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, (char *)str, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    return w;
}

static Gfx *ink(Gfx *DL, s32 x, s32 y, const char *str, u32 colour)
{
    return frontPrintText(DL, &x, &y, (s8 *)str, (s32)(uintptr_t)ptrFontZurichBoldChars,
                          (s32)(uintptr_t)ptrFontZurichBold, (s32)colour,
                          viGetX(), viGetY(), 0, 0);
}

static Gfx *inkR(Gfx *DL, s32 xr, s32 y, const char *str, u32 colour)
{
    return ink(DL, xr - measureW(str), y, str, colour);
}

/* "MOUSE / AIM" -> "Mouse / Aim" (GE's menus use title case). */
static void titleCase(const char *in, char *out, int n)
{
    int j = 0, start = 1;
    for (int i = 0; in[i] && j < n - 1; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z' && !start) c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z' && start)  c = (char)(c - 'a' + 'A');
        start = (c == ' ' || c == '/');
        out[j++] = c;
    }
    out[j] = '\0';
}

static int rowY(int k)
{
    return ROW_Y0 + k * ROW_DY;
}

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
    s_rowN = 0;
    if (s_pageN > 0) {
        for (int i = s_pageHdr[s_page] + 1; i < n && !optionsRowIsHeader(i); i++) {
            if (optionsRowIsShown(i) && s_rowN < MAX_PROWS) {
                s_rowIdx[s_rowN++] = i;
            }
        }
    }
}

static int itemCount(void)
{
    return s_level == 0 ? s_pageN : s_rowN;
}

/* Put the crosshair on item k (entering a page / going back). */
static void cursorToItem(int k)
{
    cursor_h_pos = (f32)(ROW_X + 20);
    cursor_v_pos = (f32)(rowY(k) + 6);
}

/* ------------------------------------------------------------------------ */
/* MENU_PC_OPTIONS: init / update / interface / constructor (front.c cases) */

void frontOptionsMenuInit(void)
{
    /* As init_menu15_cheat: reset the tab state and make sure the dossier
     * model is loaded (the background draws walletinst[0]). */
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    load_walletbond();

    /* frontSetupMenuBackground frames the folder at folderpositions[
     * selected_folder_num]; file select leaves it at -1. Use folder 1, as a
     * return from mode select would (file select's init does the same). */
    if (selected_folder_num < FOLDER1 || selected_folder_num >= MAX_FOLDER_COUNT) {
        selected_folder_num = FOLDER1;
    }
    folder_selection_screen_option_icon = 0;   /* crosshair, not Copy/Erase */

    s_level = 0;
    s_page = 0;
    s_hl = -1;
    s_dragRow = -1;
    s_repeatDir = 0;
    buildPages();
    cursorToItem(0);
    sysLogPrintf(LOG_INFO, "frontoptions: opened");
}

void frontOptionsMenuUpdate(void)
{
}

static void goBack(void)
{
    playSfx(DOOR_METAL_CLOSE2_SFX);
    s_dragRow = -1;
    if (s_level == 1) {
        s_level = 0;
        cursorToItem(s_page);
        sysLogPrintf(LOG_INFO, "frontoptions: back to section list");
        return;
    }
    configSave();
    sysLogPrintf(LOG_INFO, "frontoptions: closed");
    frontChangeMenu(MENU_FILE_SELECT, FALSE);
}

void frontOptionsMenuInterface(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(0);

    buildPages();   /* an auto toggle may have hidden or shown % rows */

    /* Highlight: as the cheat screen, recomputed while A is not held. */
    if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG) == 0) {
        tab_prev_highlight = FALSE;
        s_hl = -1;
        if (frontCheckCursorOnPreviousTab()) {
            tab_prev_highlight = TRUE;
        } else if (cursor_h_pos >= ROW_HIT_X0 && cursor_h_pos <= ROW_HIT_X1) {
            for (int k = itemCount() - 1; k >= 0; k--) {
                if (cursor_v_pos >= (f32)(rowY(k) - 3) &&
                    cursor_v_pos <  (f32)(rowY(k) + ROW_DY - 3)) {
                    s_hl = k;
                    break;
                }
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON)) {
        if (tab_prev_highlight) {
            goBack();
        } else if (s_hl >= 0 && s_level == 0) {
            playSfx(DOOR_METAL_CLOSE2_SFX);
            s_page = s_hl;
            s_level = 1;
            sysLogPrintf(LOG_INFO, "frontoptions: section %d", s_page + 1);
            buildPages();
            cursorToItem(0);
            s_hl = -1;
        } else if (s_hl >= 0 && s_hl < s_rowN) {
            int i = s_rowIdx[s_hl];
            playSfx(DOOR_LOCK_SFX);
            if (optionsRowIsSlider(i) && cursor_h_pos >= BAR_X0 - 4 &&
                cursor_h_pos <= BAR_X1 + 4) {
                optionsRowSetFraction(i, ((double)cursor_h_pos - BAR_X0) / (BAR_X1 - BAR_X0));
                s_dragRow = i;
            } else {
                optionsRowAdjust(i, +1);   /* toggle / cycle / step (wraps) */
            }
        }
    } else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON)) {
        goBack();
    }

    /* Drag a slider while A is held. */
    if (s_dragRow >= 0) {
        if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG)) {
            optionsRowSetFraction(s_dragRow, ((double)cursor_h_pos - BAR_X0) / (BAR_X1 - BAR_X0));
        } else {
            s_dragRow = -1;
        }
    }

    /* Left/right (D-pad, C buttons; keyboard A/D) adjust the highlighted row,
     * with hold-to-repeat. */
    if (s_level == 1 && s_hl >= 0 && s_hl < s_rowN) {
        int dir = 0;
        if (joyGetButtons(PLAYER_1, L_JPAD | L_CBUTTONS)) dir = -1;
        if (joyGetButtons(PLAYER_1, R_JPAD | R_CBUTTONS)) dir = +1;
        if (dir != 0) {
            int fire = 0;
            if (dir != s_repeatDir) {
                fire = 1;
                s_repeatTimer = 18;
            } else if (--s_repeatTimer <= 0) {
                fire = 1;
                s_repeatTimer = 4;
            }
            if (fire) {
                optionsRowAdjust(s_rowIdx[s_hl], dir);
            }
        }
        s_repeatDir = dir;
    } else {
        s_repeatDir = 0;
    }

    /* The dossier: tabs + a blank CLASSIFIED page, as the cheat screen. */
    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_CLASSIFIED, 1);
    frontUpdateControlStickPosition();
}

Gfx *frontOptionsMenuDraw(Gfx *DL)
{
    char buf[48];

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
#ifdef VERSION_EU
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
    DL = viFillScreen(DL);
#endif
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    if (s_level == 0) {
        DL = ink(DL, ROW_X, TITLE_Y, "PC Options\n", INK);
        for (int k = 0; k < s_pageN; k++) {
            char name[32], num[8];
            titleCase(optionsRowLabel(s_pageHdr[k]), name, sizeof(name));
            strcat(name, "\n");
            snprintf(num, sizeof(num), "%d.\n", k + 1);
            if (k == s_hl) {
                DL = microcode_constructor_related_to_menus(DL, SEC_X - 2, rowY(k) - 1,
                        SEC_X + measureW(name) + 5, rowY(k) + 0xE, HILITE);
            }
            DL = ink(DL, NUM_X, rowY(k), num, INK);
            DL = ink(DL, SEC_X, rowY(k), name, INK);
        }
    } else {
        char title[32];
        titleCase(optionsRowLabel(s_pageHdr[s_page]), title, sizeof(title));
        strcat(title, "\n");
        DL = ink(DL, ROW_X, TITLE_Y, title, INK);

        for (int k = 0; k < s_rowN; k++) {
            int i = s_rowIdx[k];
            int y = rowY(k);
            char label[48];
            snprintf(label, sizeof(label), "%s\n", optionsRowLabel(i));
            if (k == s_hl) {
                DL = microcode_constructor_related_to_menus(DL, ROW_X - 2, y - 1,
                        ROW_X + measureW(label) + 5, y + 0xE, HILITE);
            }
            DL = ink(DL, ROW_X, y, label, INK);

            if (optionsRowIsSlider(i)) {
                s32 fx = BAR_X0 + (s32)((BAR_X1 - BAR_X0) * optionsRowFraction(i) + 0.5);
                DL = microcode_constructor_related_to_menus(DL, BAR_X0, y + 5, BAR_X1, y + 8, 0x00000040);
                DL = microcode_constructor_related_to_menus(DL, BAR_X0, y + 5, fx, y + 8, 0xA00000C0);
            } else if (optionsRowNeedsRestart(i)) {
                DL = ink(DL, BAR_X0, y, "(restart)\n", INK_DIM);
            }

            optionsRowValueText(i, buf, sizeof(buf) - 1);
            if (buf[0]) {
                u32 col = (strcmp(buf, "ON") == 0) ? INK_ON : INK;
                strcat(buf, "\n");
                DL = inkR(DL, VAL_R, y, buf, col);
            }
        }
    }

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

/* F10 / Select must not stack the overlay on this screen. */
int frontOptionsBlocksOverlay(void)
{
    return current_menu == MENU_PC_OPTIONS;
}

/* ------------------------------------------------------------------------ */
/* File select: the "Options" entry, drawn from the D343 hook in           */
/* constructor_menu05_fileselect (game thread, after the interface tick).  */
/* ------------------------------------------------------------------------ */

Gfx *optionsFileSelectLabel(Gfx *gdl)
{
    s32 h = 0, w = 0, unusedw = 0;
    s32 x, y;
    int hot;

    /* The F10 overlay pads the controller away from front.c, so file select's
     * idle timer never resets and it dropped to the legal screen after 30 s.
     * This hook only runs on MENU_FILE_SELECT, the one screen where
     * g_MenuTimer is an idle timer (elsewhere it is the intro / cast-roll
     * clock). Same write front.c makes when a button is pressed. */
    if (optionsOverlayIsOpen()) {
        g_MenuTimer = 0;
    }

    /* textMeasure only counts height for completed lines, so measure the
     * height with a trailing newline (front.c's folder text does the same). */
    textMeasure(&unusedw, &w, (char *)kLabel, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    textMeasure(&h, &unusedw, (char *)kLabelNL, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = LABEL_RIGHT - w;
    y = LABEL_Y;

    hot = !optionsOverlayIsOpen()
       && menu_update == MENU_INVALID
       && folder_selected_for_deletion < 0
       && cursor_h_pos >= (f32)(x - HIT_PAD) && cursor_h_pos <= (f32)(x + w + HIT_PAD)
       && cursor_v_pos >= (f32)(y - HIT_PAD) && cursor_v_pos <= (f32)(y + h + HIT_PAD);

    if (hot && joyGetButtonsPressedThisFrame(PLAYER_1, A_BUTTON | Z_TRIG | START_BUTTON)) {
        playSfx(DOOR_LOCK_SFX);   /* Copy/Erase's click */
        frontChangeMenu(MENU_PC_OPTIONS, FALSE);
    }

    /* Same font and white as Copy/Erase; gold (the folder text colour) while
     * the cursor is over it. Text state is still the Copy/Erase text's. */
    return textRender(gdl, &x, &y, (char *)kLabel, ptrFontZurichBoldChars,
                      ptrFontZurichBold, hot ? 0xEBD879FF : 0xFFFFFFFF,
                      viGetX(), viGetY(), 0, 0);
}
