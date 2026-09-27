#ifndef PORT_OPTIONSOVERLAY_H
#define PORT_OPTIONSOVERLAY_H

/*
 * F10 in-game options overlay (approach C from docs/dev/OPTIONS-MENU-PLAN.md).
 *
 * A self-contained port-layer immediate-mode overlay: it draws its own 2D
 * display list on top of the game's frame (appended in fast3d's gfx_run,
 * after the game DL) and handles its own keyboard/mouse nav. It edits the
 * port-owned config.c options directly, so changes apply live for the live
 * ones. When closed it renders NOTHING (zero bytes appended) -- golden dumps
 * stay byte-identical.
 *
 * Hooks:
 *   video.c   videoPumpEvents  : F10 -> optionsOverlayToggle(); ESC closes;
 *                                wheel -> optionsOverlayScroll()
 *   input.c   inputComputePad  : controller 0 swallowed while open;
 *                                nav routed to optionsOverlayHandleInput()
 *   gfx_pc.cpp gfx_run          : optionsOverlayEmit() appended after the game DL
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Toggle open/closed. On the closing edge the config is saved. */
void optionsOverlayToggle(void);

/* 1 while the overlay is on screen. */
int optionsOverlayIsOpen(void);

/* Called from inputComputePad(0) while open: reads SDL keyboard edges and
 * drives the cursor / value adjustments. */
void optionsOverlayHandleInput(void);

/* Mouse-wheel notch -> move the selection (host event pump). */
void optionsOverlayScroll(int dir);

/* Build the overlay's 2D display list for this frame, or return NULL when the
 * overlay is closed. Called by fast3d after running the game DL. */
Gfx *optionsOverlayEmit(void);

/* D343 (M2): row API for the file-select options screen (frontoptions.c).
 * Index i runs over the same row table as the F10 overlay; headers split it
 * into the screen's sections. */
int         optionsRowCount(void);
int         optionsRowIsHeader(int i);
int         optionsRowIsShown(int i);
const char *optionsRowLabel(int i);
int         optionsRowIsSlider(int i);
int         optionsRowNeedsRestart(int i);
double      optionsRowFraction(int i);
void        optionsRowSetFraction(int i, double f);
void        optionsRowCommit(int i); /* slider release; watch rows persist once */
void        optionsRowValueText(int i, char *out, int n);
void        optionsRowAdjust(int i, int dir);

#ifdef __cplusplus
}
#endif

#endif /* PORT_OPTIONSOVERLAY_H */
