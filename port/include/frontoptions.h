#ifndef PORT_FRONTOPTIONS_H
#define PORT_FRONTOPTIONS_H

/*
 * D343: PC options as a GE front-end screen, MENU_PC_OPTIONS
 * (port/src/frontoptions.c).
 *
 * Hooks:
 *   front.c  the five MENU_* dispatch sites : frontOptionsMenu{Init,Update,
 *            Interface,Draw} and the debug name (all #ifdef PORT)
 *   front.c  constructor_menu05_fileselect : optionsFileSelectLabel() draws the
 *            file-select entry label and enters MENU_PC_OPTIONS on a click
 *   video.c / input.c : F10 / Select don't open the overlay on this screen
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

void frontOptionsMenuInit(void);
void frontOptionsMenuUpdate(void);
void frontOptionsMenuInterface(void);
Gfx *frontOptionsMenuDraw(Gfx *DL);

Gfx *optionsFileSelectLabel(Gfx *gdl);   /* file-select hook */
int  frontOptionsBlocksOverlay(void);    /* 1 while MENU_PC_OPTIONS is up */

#ifdef __cplusplus
}
#endif

#endif /* PORT_FRONTOPTIONS_H */
