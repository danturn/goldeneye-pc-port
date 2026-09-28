# F10 visual refresh — GoldenEye dossier styling (proposal)

Status: **superseded by D363 after user playtest** — the dossier/paper
concept below was implemented as D362, then replaced with a dark
watch-green/Bank Gothic version. This is the historical proposal for a
port-layer F10 overlay proposal, not a replacement for GE's pause watch or the
front-end PC Options screen. Reference surfaces: `src/game/front.c`
`constructor_menu15_cheat`/`frontSetupMenuBackground`, the existing
`port/src/frontoptions.c` cheat-list-inspired options screen, and GE's in-stage
watch. Current overlay: `port/src/optionsoverlay.c` `optionsOverlayEmit`,
`overlayRowAtY`, `sliderBarSpan`, `maxVisibleRows`.

## 1. Direction and boundaries

Make F10 read as an **MI6 dossier laid over the game scene**, not a generic
SDL debug panel: dark translucent scrim, warm paper/ivory content card,
black Zurich-style menu text, red for active options/slider fills and an
occasional muted gold title/accent. The front-end PC Options already uses
GE's actual 3D folder and Zurich text; use it as the visual reference, not
as a display-list dependency. In-stage F10 must remain an inexpensive flat
2D overlay: no loading a 3D wallet model, new ROM assets, front-end init
functions or `src/game` modifications. Preserve the current two-level
category -> settings navigation and the shared settings/commit API.

The initial port-only implementation lives in `optionsOverlayEmit()` and
uses one shared `OvLayout` for both 320x240 stage and 440x330 front canvases.
Headless captures live under `scratch/d362-test/` (not distributed). Manual
visual review on a bright stage and controller/mouse feel-check remain owed.

## 2. Concrete visual hierarchy

- **Backdrop/card:** keep an adjustable translucent dim over the scene;
  replace the opaque blue-black rectangle with a near-opaque ivory panel
  (subtle border and one offset shadow). Do not mimic the actual folder
  texture unless a later, measured prototype proves the simple card
  insufficient. Preserve legible contrast against both dark and bright
  levels, including safe-area crop and widescreen pillarboxes.
- **Masthead:** small gold/black `MI6 / OPTIONS` or `PC OPTIONS` heading,
  fine black rule (a restrained `CLASSIFIED` stamp can be added later if
  measured text fits) rather than a blue title/instruction block. Keep the
  category name on the **same
  page above its first setting**, especially INPUT over Mouse sensitivity.
- **Root:** numbered `1. Input` ... `5. Video`, GE's dossier/menu numbering
  and title case as on `frontoptions.c`; a narrow translucent dark selection
  box behind the focused item. Category order and labels stay unchanged in
  the shared row table; casing is presentation-only.
- **Section:** a compact `PREVIOUS`/Back tab, title and optional
  `(Profile N)` annotation, then two columns: label at left, value at right.
  Unselected values in black, active/toggled values in GE's dark red
  (`0xA00000FF` is the cheat-screen reference). Slider tracks are thin
  charcoal with a red fill and an obvious knob; show the numerical value
  outside the bar. A mixed-scope `(per profile)` marker must remain legible
  without colliding with the value column; measure it and, if necessary,
  use a small second line rather than silently removing the distinction.
- **Actions:** Reset retains its two-press/three-second confirmation and
  displays `CONFIRM` in red only while armed. Quit remains visually distinct
  from resets (and retains its existing activation semantics). Back/Close
  affordances should match the actual click targets, not become decoration.
- **Footer:** one quiet, short line for mouse/arrows/controller, with an
  explicit `ESC: Back / F10: Close` cue. No newly invented glyphs or fonts
  that may be missing from the game's text bank. FPS readout is independent.

Suggested layout sketch (not pixel positions):

```
  MI6  /  PC OPTIONS                         CLOSE
  -----------------------------------------------
  PREVIOUS     GRAPHICS
  Anti-aliasing                            2x
  Texture filter                     Bilinear
  Draw distance         [------|----]     50/100
  ...
  Reset to defaults                     [ENTER]
  ESC Back  /  F10 Close
```

## 3. Implementation boundaries

1. Extract a port-only layout helper for fonts, palette,
   panel rectangles and spacing; preserve `rows[]`, `rowAdjust`, persistence,
   file scoping, reset logic and input bindings. Use GE's loaded Zurich Bold
   font for body *only after* validating it exists on both stage and front
   contexts; Bank Gothic can remain a fallback/header font. Text clipping
   must use the whole viewport as `textRender` expects.
2. Replace only the fill/text passes in `optionsOverlayEmit()` first. Emit
   fills before text as currently, reset GBI state explicitly and keep
   `OV_BUF_CMDS`/DL size bounds. Avoid mutable texture uploads or new assets.
3. One shared geometry calculation must drive panel/card positions,
   `maxVisibleRows`, row Y, slider-bar span, close/back hitboxes, and
   `overlayRowAtY`. In particular, don't change visual row spacing without
   updating hit-testing (D304/D316), and don't use naive OS-window pixel
   mapping instead of `gfx_get_ui_screen_rect` (D316/D335). When a page
   scrolls, keep its section title visible; a clipped row cannot be clicked.
4. Use measured text widths to reserve space for values, `Confirm`,
   `(Profile N)` and restart markers. At 320x240, allow scrolling/short
   contextual hints, not overlapping text or tiny glyphs. At 440x330 and
   ultrawide, centre or constrain the card rather than stretching the
   columns across the entire viewport.
5. Make any GE-like UI sound effects opt-in to a *separate* follow-up: F10
   runs in controller polling, and D361 prohibits game save/audio calls
   from that thread. A cosmetic refresh must not reintroduce that lockup.

## 4. Acceptance gates

- Compare screenshots with the existing front Options/cheat dossier and the
  in-stage watch; user reviews the live palette/layout before closing the task.
- Native 320x240, front-end 440x330, windowed 4:3, 16:9 and 21:9,
  fullscreen, safe-area crop on/off: INPUT title visible; no text clipping,
  unreadable values, wrong hitboxes, phantom/duplicated bottom rows or
  overflow in the display list. Check graphics's longest labels and AUDIO's
  per-profile markers with and without a valid file.
- Mouse click/drag/wheel, arrows and D-pad/stick, ESC back, F10/Start/X
  close, focused/armed reset, and quit action all behave **exactly** as
  before. F10 Audio on file select still commits via D361's game-thread
  queue; test both fresh and existing EEPROM without altering the user's
  save (backup/restore for probes).
- With overlay closed, append no panel DL and retain existing golden frame
  hashes (unless FPS display is explicitly enabled). Run `./build-pc.sh
  ntsc-final`, link/duplicate-symbol checks, front-end + stage smoke,
  `GE_WSPROBE_F10FRONT` and targeted live playtest. Log the result in
  `docs/dev/findings.md` after implementation, not in advance.

## 5. Explicit non-goals

No fullscreen 3D folder in gameplay, changes to the N64 watch, new config
keys, row wording/value changes, save-format changes, controller remapping,
new persistent assets, or `src/game` behavioral edits. The front-end PC
Options screen is a **visual reference**, not part of the first styling
patch; harmonizing its tiny decorative details is a later optional pass.
