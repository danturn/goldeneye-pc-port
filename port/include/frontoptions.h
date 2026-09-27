#ifndef PORT_FRONTOPTIONS_H
#define PORT_FRONTOPTIONS_H

/*
 * D343: PC options on the file-select screen (port/src/frontoptions.c).
 *
 * Hooks:
 *   front.c  constructor_menu05_fileselect : optionsFileSelectLabel() draws the
 *            "Options" label, or the options panel while it is open
 *   input.c  inputComputePad(0)            : while open on file select, the
 *            buttons go to frontOptionsFeedPad() and the game gets 0 (the
 *            stick still moves the game's crosshair)
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

Gfx *optionsFileSelectLabel(Gfx *gdl);   /* game thread */
int  frontOptionsIsOpen(void);           /* any thread */
void frontOptionsFeedPad(unsigned held); /* input thread */

#ifdef __cplusplus
}
#endif

#endif /* PORT_FRONTOPTIONS_H */
