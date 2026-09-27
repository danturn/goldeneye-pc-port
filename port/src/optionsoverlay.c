/*
 * F10 in-game options overlay -- approach (C) from docs/dev/OPTIONS-MENU-PLAN.md.
 *
 * Port-layer only. No src/ menu code is touched: the overlay draws its own
 * fast3d 2D display list (appended after the game DL in gfx_run) and edits the
 * port-owned config.c variables directly. Live knobs apply immediately; the
 * two that need an FBO/window rebuild (MSAA, Fullscreen) are tagged "(restart)".
 *
 * The panel adapts to whatever 2D space it is drawn in (320x240 in-game vs
 * 440x330 on front-end screens -- viSetXY differs) and scrolls when the row
 * list outgrows the viewport (wheel / arrows at the edges). Cyclic rows
 * (MSAA, texture filter, resolution, toggles) wrap in both directions; the
 * manual % rows (draw/LOD distance) are hidden while their "auto" toggle is on.
 *
 * Text + fill helpers are the game's own (textRender / microcode_constructor /
 * gDPFillRectangle) reached by extern -- same pattern input.c uses to read
 * current_menu / cursor_h_pos. This is a rendering/UI view, not a logic change.
 *
 * Diagnostic: set GE_OPTIONSOVERLAY=1 to auto-open at boot (headless layout
 * check). Env-gated, harmless when unset.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <PR/ultratypes.h>
/* gbi.h's gDP* DL macros use _SHIFTL/_SHIFTR but do not define them -- game TUs
 * get them from <ultra64.h>/<PR/mbi.h>, which also drags in N64 OS headers that
 * shadow libc here. Define the two pure macros locally (verbatim from mbi.h) so
 * this stays a plain port TU. Without them GCC/ld fails "undefined reference to
 * _SHIFTL" (MinGW's chain happens to provide it). */
#ifndef _SHIFTL
#define _SHIFTL(v, s, w) ((u32)(((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w) ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))
#endif
#include <PR/gbi.h>
#include <bondconstants.h>

extern MENU current_menu;

#include "platform.h"
#include "system.h"
#include "config.h"
#include "video.h"
#include "input.h"
#include "front.h"   /* selected_folder_num (D356 stage probe) */
#include "file.h"   /* save_data (D356 reset probe: second-file isolation) */
#include "optionsoverlay.h"
#include "watchsettings.h"
#include "../fast3d/gfx_api.h"

/* file2.c; same extern as watchsettings.c (not in a header). */
extern save_data *fileGetSaveForFoldernum(u32 folder);

/* D324 class: -Iinclude resolves <math.h> to GE's N64 stub, which does not
 * declare lround; without this the call is an implicit `int lround()`
 * (GCC only rescued it via its builtin signature). */
long lround(double x);

/* ---- game symbols (rendering/UI only; see input.c for the same pattern) ---- */
struct font;
struct fontchar;
extern struct font     *ptrFontBankGothic;
extern struct fontchar *ptrFontBankGothicChars;
extern Gfx  *microcode_constructor(Gfx *gdl);
extern Gfx  *textRender(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars,
                        struct font *font, u32 colour, s32 width, s32 height,
                        u32 yOffset, s32 lineheight);
extern void  textMeasure(s32 *textheight, s32 *textwidth, char *text,
                         struct fontchar *chars, struct font *font, s32 lineheight);
extern s16   viGetX(void);
extern s16   viGetY(void);

/* ------------------------------------------------------------------------ */

enum { ROW_TOGGLE, ROW_SLIDER, ROW_ENUM, ROW_MSAA, ROW_RES, ROW_ACTION, ROW_FPSCAP,
       ROW_HEADER, ROW_BOND_FILE /* explicit chooser (front end only) */ };

/* D346: wording pass -- Nightdive/Turok + PD-port conventions: title-case
 * On/Off, no all-caps value strings. Display-only; config stores 0/1 either way. */
static const char *const kOnOff[]     = { "Off", "On", NULL };
static const char *const kReverse[]   = { "Reverse", "Upright", NULL };
static const char *const kHold[]      = { "Hold", "Toggle", NULL };
static const char *const kTexFilter[] = { "Nearest", "Bilinear", "3-Point", NULL };
static const char *const kAimMode[]   = { "N64", "Centred (PC)", NULL };   /* D337 */
static const char *const kAimRange[]  = { "PC", "N64", NULL };             /* D338 */
static const int         kMsaaSeq[]   = { 1, 2, 4, 8 };
/* D186: the sim's own tick pacemaker is hardcoded to the console's native VI
 * rate (60Hz NTSC / 50Hz PAL, port/src/libultra.c) -- Video.FpsCap can only
 * throttle down from there, never past it, and throttling it below 30
 * throttles game logic itself (video.c already force-uncaps anything under
 * 30). A free 0-360 slider therefore had a huge dead zone (every value above
 * the console rate is a no-op, every value 1-29 silently snaps to 0) with
 * only two states that actually do anything. Exposed as a plain 30/60 toggle
 * instead (user ask, 2026-09-18). A third "uncapped" (0, skip the port's own
 * frame-pacing wait) state exists at the config level and old inis may still
 * have it, but it's dropped from the menu: with VSync on (the default) it's
 * indistinguishable from 60, and with VSync off it just burns GPU time
 * re-presenting the same simulated frame -- confusing for no real benefit. */
static const int         kFpsCapSeq[] = { 30, 60 };

/* Windowed-mode resolution presets. Filtered at init to those that fit the
 * desktop; the Resolution row cycles the surviving list. */
static const int kResList[][2] = {
    {  640,  480 }, {  800,  600 }, {  960,  720 }, { 1024,  768 },
    { 1152,  864 }, { 1280,  720 }, { 1280,  800 }, { 1280,  960 },
    { 1366,  768 }, { 1440,  900 }, { 1600,  900 }, { 1600, 1200 },
    { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 },
    { 3200, 1800 }, { 3840, 2160 },
};
#define NUM_RES ((int)(sizeof(kResList) / sizeof(kResList[0])))
static int s_resFit[NUM_RES];   /* indices into kResList that fit the desktop */
static int s_resFitN = 0;
static int s_resSel  = 0;       /* index into s_resFit */
static int s_dragRow = -1;      /* scheduler-thread mouse drag */
static SDL_atomic_t s_dragWatchField; /* field+1, host may close F10 mid-drag */

struct Row {
    const char        *key;
    const char        *label;
    int                kind;
    double             step;
    const char *const *names;    /* ROW_TOGGLE / ROW_ENUM value names */
    int                restart;  /* value change needs a restart      */
    double             uiMin, uiMax; /* 0,0 -> use the registered clamp */

    /* resolved from config.c at init */
    int                found;
    int                type;     /* CONFIG_OPT_*    */
    void              *ptr;
    double             cfgMin, cfgMax;

    /* Hidden while the option named here is nonzero (a manual % row
     * disappears while its "auto" toggle is on). Resolved to hidePtr at init. */
    const char        *hiddenIfOn;
    int               *hidePtr;

    /* D346: value-text decoration (display only). unit is appended to integer
     * slider values ("%"/"x"); dispDiv>0 divides the raw value before display
     * (e.g. deadzone raw 0..30000 -> % of full stick). NULL/0 = plain int. */
    const char        *unit;
    int                dispDiv;

    /* D356: 1 = this row's value lives in the selected save file (per-file
     * watch rows). Carries the dim "(profile)" tag on the only section that
     * mixes scopes (GAMEPLAY); on headers the flag is recomputed at init to
     * mean "this section contains per-file rows" (drives the "(File N)"
     * title annotation in both UIs). */
    int                saveScoped;
};

static struct Row rows[] = {
    /* D356: Turok-style functional sections in Turok's order (INPUT,
     * GAMEPLAY, GRAPHICS, AUDIO, VIDEO; docs/dev/D356-SETTINGS-REGROUP-PLAN.md).
     * The D353 "BOND FILE" section -- the one named after WHERE a value is
     * saved -- is gone: its surviving rows split into GAMEPLAY (the per-file
     * watch toggles, dim "(profile)" tagged since the section mixes scopes) and
     * AUDIO (the per-file volume sliders, homogeneous so title annotation
     * only), and the "Edit file" chooser row is retired in favour of the
     * single top save-file row on the front options screen (frontoptions.c).
     * Per-section "Reset to defaults" rows (D356, plan Gate E) share
     * rowResetSection(); "(File N)" title annotations come from
     * watchSettingsActiveFolder(). All rows use designated initializers
     * (D351 class). */
    { .key="__HdrInput", .label="INPUT", .kind=ROW_HEADER },
    { .key="Input.MouseSensitivity", .label="Mouse sensitivity", .kind=ROW_SLIDER, .step=5 },
    { .key="Input.MouseInvertY", .label="Invert look (mouse)", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* Input.PdMouseAim (findings D332): the Perfect Dark port's mouse-aim
     * model. The port only accumulates the mouse; the game's own crosshair
     * integrator is driven by the port-supplied turn with PD's near-zero damp
     * (0.01), instead of GEPD's overwrite with the weapon's ~0.8 -- which is
     * what made the reticule step. Changes aim feel, so opt-in. GE_PDMOUSEAIM=0/1
     * overrides it at launch for A/B runs. */
    /* D337: N64 = the N64 aim model (crosshair travels, camera edge-scrolls)
     * with the mouse fed through the game's integrator at PD's mouse damp --
     * the default. CENTRED (PC) = opt-in FPS-style aim (#104), not N64. Applies
     * to every aim input (RMB, Shift, Q/L, pad trigger, Toggle mode). */
    { .key="Input.AimMode", .label="Aim style", .kind=ROW_ENUM, .step=1, .names=kAimMode },
    /* D338: how far the N64-style crosshair travels. PC = to the screen edge
     * (GEPD / mouse-injector feel, default); N64 = the original stick limits
     * (65% of the half-width, camera turn from ~49%). Hidden while the aim
     * style is CENTRED (PC), where the crosshair doesn't travel. */
    { .key="Input.AimRange", .label="Aim range", .kind=ROW_ENUM, .step=1, .names=kAimRange, .hiddenIfOn="Input.AimMode" },
    { .key="Input.PadLookInvertY", .label="Invert look (controller)", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Input.PadDeadzone", .label="Stick deadzone", .kind=ROW_SLIDER, .step=500, .unit="%", .dispDiv=300 },
    { .key="Input.PadTriggerPct", .label="Trigger threshold", .kind=ROW_SLIDER, .step=1, .unit="%" },
    { .key="__ResetInput", .label="Reset to defaults", .kind=ROW_ACTION },

    { .key="__HdrGameplay", .label="GAMEPLAY", .kind=ROW_HEADER },
    /* D356 exposure filter (menu surface only, the D181/D216/D304 pattern --
     * config keys, the N64 watch paths and ini hand-edits all stay live):
     *   Bond.Look        duplicates Input.MouseInvertY + Input.PadLookInvertY
     *                    (stacking both is a double-negation trap);
     *   Bond.AimControl  collides with Input.AimMode (two rows fighting over
     *                    the same N64-vs-PC aim model).
     * The four surviving per-file toggles move here from the D353 BOND FILE
     * section and carry .saveScoped (dim "(profile)" tag, the only mixed-scope
     * section). */
    /* { .key="Bond.Look", .label="Look up/down (watch; stacks)", .kind=ROW_TOGGLE,
       .names=kReverse, .found=1, .uiMax=1, .cfgMax=1 }, */
    { .key="Bond.AutoAim", .label="Auto-aim", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    /* { .key="Bond.AimControl", .label="Aim control", .kind=ROW_TOGGLE,
       .names=kHold, .found=1, .uiMax=1, .cfgMax=1 }, */
    { .key="Bond.Sight", .label="Sight on screen", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    { .key="Bond.LookAhead", .label="Look ahead", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    { .key="Bond.Ammo", .label="Ammo on screen", .kind=ROW_TOGGLE,
      .names=kOnOff, .found=1, .uiMax=1, .cfgMax=1, .saveScoped=1 },
    /* D216/Game.SkipIntro: user report (v0.2.1 testing) that it breaks audio
     * -- pulled from the menu until root-caused. Not exposed to players; the
     * config var + lv.c hook stay in place (dead unless an existing ini has
     * it set, which no menu path can do any more). Do not re-add without
     * fixing the underlying issue first. */
    { .key="Game.SkipIntro", .label="Skip intro", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* D232: the community "no damage flash" toggle (suppresses the red/green
     * hit-flash overlay in bondview2). */
    { .key="Game.NoHitFlash", .label="No hit flash", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* D257: everything-unlocked goodie. The C initializers (port/src/video.c)
     * start it OFF (0) -- the old "default ON" note was stale and is
     * corrected by D356. Consumed at startup by main.c -- applies from the
     * next launch. */
    { .key="Game.AllUnlocked", .label="All unlocked", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* D226: scales the ammo counter, pickup / status messages and dialogue
     * about their screen anchors. 100% = original, nothing emitted. */
    { .key="Game.HudScale", .label="HUD scale", .kind=ROW_SLIDER, .step=5, .unit="%" },
    { .key="__ResetGameplay", .label="Reset to defaults", .kind=ROW_ACTION },
    /* D293: only quit path used to be the OS window-close / Alt+F4 -- no
     * discoverable in-game way to exit, a real gap on Deck/controller-only
     * setups. Not config-backed (like __Resolution); activating it exits
     * the same way video.c's SDL_QUIT/Alt+F4 handlers already do. */
    { .key="__QuitToDesktop", .label="Quit to desktop", .kind=ROW_ACTION },

    { .key="__HdrGraphics", .label="GRAPHICS", .kind=ROW_HEADER },
    { .key="Video.MSAA", .label="Anti-aliasing", .kind=ROW_MSAA, .restart=1 },
    { .key="Video.TextureFilter", .label="Texture filter", .kind=ROW_ENUM, .step=1, .names=kTexFilter },
    { .key="Video.Anisotropy", .label="Anisotropic filtering", .kind=ROW_SLIDER, .step=1, .unit="x" },
    { .key="Video.FovScale", .label="FOV scale", .kind=ROW_SLIDER, .step=5, .unit="%" },
    /* D334: native widescreen (world projected at the window aspect, Hor+).
     * While on, "Widescreen auto FOV" has no effect (it was the stretch-era
     * vertical-FOV compensation). */
    { .key="Video.NativeWidescreen", .label="Native widescreen", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.WidescreenAuto", .label="Widescreen auto FOV", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.SafeAreaCrop", .label="Crop overscan", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.DrawDistance", .label="Draw distance", .kind=ROW_SLIDER, .step=25, .hiddenIfOn="Video.DrawDistanceAutoFov", .unit="%" },
    { .key="Video.DrawDistanceAutoFov", .label="Draw dist. follows FOV", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.LodDistance", .label="LOD distance", .kind=ROW_SLIDER, .step=25, .hiddenIfOn="Video.LodDistanceAutoFov", .unit="%" },
    { .key="Video.LodDistanceAutoFov", .label="LOD dist. follows FOV", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    /* D304: the per-mode aim/turn-sensitivity sliders and the "Link" toggle
     * that papered over their decoupling risk were pulled from the menu
     * (user feedback after the D304 widening); the ONE master
     * Input.MouseSensitivity row above is the player-facing knob, mirroring
     * the GEPD/PD injector model. Config vars + SensLink logic stay live in
     * input.c/rowSetCommit() for ini hand-edits. */
    /* { "Input.AimModeSens",        "Mouse aim speed",  ROW_SLIDER, 5, NULL, 0, 0, 0, 0,0,0,0,0 }, */
    /* { "Input.MouseTurnSpeed",     "Mouse turn speed", ROW_SLIDER, 5, NULL, 0, 0, 0, 0,0,0,0,0 }, */
    /* { "Input.SensLink",           "Link aim/turn sens",ROW_TOGGLE,1, kOnOff, 0, 0, 0, 0,0,0,0,0 }, */
    /* D181/Game.ScreenShakeIntensity: user testing (v0.2.1) found the slider
     * "basically useless" -- viShake() is only called from explosion.c, so it
     * scales explosion shake alone; it never touches the always-on walking
     * head-bob or any getting-shot reaction, which is what "Screen shake"
     * reads as to a player. Pulled from the menu until it covers all
     * screen-shake/view-bob sources, not just explosions. Config var + fr.c
     * hook stay in place. */
    { .key="__ResetGraphics", .label="Reset to defaults", .kind=ROW_ACTION },

    { .key="__HdrAudio", .label="AUDIO", .kind=ROW_HEADER },
    /* D356: the per-file volume sliders get a real AUDIO section (the D353
     * "AUDIO (BOND FILE)" header was retired with the BOND FILE section).
     * A port-level master volume (M3) will join here as Turok's
     * Master/Sound/Music trio. */
    { .key="Bond.Music", .label="Music volume", .kind=ROW_SLIDER, .step=128,
      .uiMax=32767, .cfgMax=32767, .found=1, .unit="%", .dispDiv=328 },
    { .key="Bond.FX", .label="FX volume", .kind=ROW_SLIDER, .step=128,
      .uiMax=32767, .cfgMax=32767, .found=1, .unit="%", .dispDiv=328 },
    { .key="__ResetAudio", .label="Reset to defaults", .kind=ROW_ACTION },

    /* D353's DISPLAY section renamed VIDEO (the presentation knobs), with
     * Video.DisplayFPS moved here from the old GAME section. */
    { .key="__HdrVideo", .label="VIDEO", .kind=ROW_HEADER },
    { .key="Video.Fullscreen", .label="Fullscreen", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="__Resolution", .label="Resolution", .kind=ROW_RES },
    { .key="Video.VSync", .label="VSync", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="Video.FpsCap", .label="Frame rate cap", .kind=ROW_FPSCAP },
    { .key="Video.DisplayFPS", .label="Show FPS", .kind=ROW_TOGGLE, .step=1, .names=kOnOff },
    { .key="__ResetVideo", .label="Reset to defaults", .kind=ROW_ACTION },
    /* D356: the D353 BOND FILE header + "Edit file" chooser row are retired --
     * the front options screen's top save-file row (frontoptions.c, D356) is
     * the single file control; F10 always targets the active file. */
    /* { .key="__HdrBond", .label="BOND FILE", .kind=ROW_HEADER, .found=1 }, */
    /* { .key="__BondFile", .label="Edit file", .kind=ROW_BOND_FILE, .found=1 }, */
};
#define NUM_ROWS ((int)(sizeof(rows) / sizeof(rows[0])))

static int  s_inited = 0;
static volatile int s_open = 0;
static int  s_sel = 0;        /* selection, index into s_visIdx (visible list) */

/* Visible-row list: rows whose hiddenIfOn option is nonzero are omitted
 * (manual % rows hide while their auto toggle is on). Rebuilt every frame in
 * overlayUpdateVisible(); s_scroll is the first visible-list entry drawn. */
static int  s_visIdx[NUM_ROWS];
static int  s_visN = 0;
static int  s_scroll = 0;
static SDL_atomic_t s_wheelPending;   /* D314: host-thread wheel notches */

/* D213: optional on-screen FPS readout (PD parity: Video.DisplayFPS). Drawn
 * top-right whenever enabled, independent of the F10 panel. Config-only knob
 * (kept off the 13-row panel, which is at its layout limit) -- matches PD,
 * whose DisplayFPS is also file-only. Default 0 => emit path unchanged =>
 * golden dumps byte-identical. */
static int      s_showFps = 0;
static char     s_fpsText[16] = "";

PD_CONSTRUCTOR static void overlayConfigInit(void)
{
    configRegisterInt("Video.DisplayFPS", &s_showFps, 0, 1);
}

/* Sampled once per emitted frame; recomputes the string every ~0.5 s. */
static void fpsTick(void)
{
    static uint64_t winStartUs = 0;
    static int      frames = 0;

    uint64_t nowUs = sysGetMicroseconds();
    if (winStartUs == 0) {
        winStartUs = nowUs;
        return;
    }
    frames++;
    uint64_t dtUs = nowUs - winStartUs;
    if (dtUs >= 500000) {
        int fps = (int)((double)frames * 1e6 / (double)dtUs + 0.5);
        snprintf(s_fpsText, sizeof(s_fpsText), "%d FPS", fps);
        winStartUs = nowUs;
        frames = 0;
    }
}

/* Layout (game 2D pixel space = viGetX() x viGetY(), ~320x240). Shared by the
 * emit path and the mouse hit-testing in optionsOverlayHandleInput().
 * BankGothic caps are ~9 units tall here, so rows need ~16 units of pitch and
 * values are right-aligned to the panel edge to survive the wide font. */
#define OV_X0        20
#define OV_LABEL_X   28
#define OV_TOP       12
#define OV_LINE      15                       /* row pitch (13 rows must fit ~240) */
#define OV_HDR       2                        /* header rows above row 0 (title+hint) */
/* i is a VISIBLE-POSITION (already scroll-adjusted by the caller). */
#define OV_ROW_Y(i)  (OV_TOP + ((i) + OV_HDR) * OV_LINE)

/* How many rows fit between the header and the bottom edge of whatever 2D
 * space we are in right now (320x240 in-game, 440x330 on front-end screens --
 * viSetXY differs, see src/game/front.c). More rows than this => scroll. */
static int maxVisibleRows(void)
{
    int n = (viGetY() - 6 - OV_TOP - OV_HDR * OV_LINE) / OV_LINE;
    if (n < 4) {
        n = 4;
    }
    return n;
}
#define OV_RIGHT     (viGetX() - OV_X0)       /* right edge for right-aligned text */
#define OV_NUM_W     36                       /* reserved width for a slider's number */
#define OV_BAR_X     150

/* Slider fill bar span in overlay space (shared by emit + hit-testing). */
static void sliderBarSpan(s32 *x0, s32 *x1)
{
    *x0 = OV_BAR_X;
    *x1 = OV_RIGHT - OV_NUM_W;
    if (*x1 < *x0 + 16) {
        *x1 = *x0 + 16;
    }
}

/* Visible position (0..s_visN-1) the given overlay-space y falls in, or -1.
 *
 * D304 fix: this used to loop over ALL s_visN entries (0..s_visN-1)
 * regardless of which ones are actually scrolled into view and drawn this
 * frame -- OV_ROW_Y(p - s_scroll) is a plain linear function of p, so a row
 * sitting just past the last visibly-drawn one (pLast, see the emit path)
 * still produces a geometrically valid, in-range Y band immediately below
 * the panel's real bottom edge. A click near that boundary (real-pixel-to-
 * virtual-2D-space rounding in the ox/oy scale-up, or simply a slightly low
 * click on the last visible row) could therefore resolve to the NEXT,
 * invisible, scrolled-off-the-bottom row instead of the one actually drawn
 * there. Concretely: with "All unlocked" sitting in the last visible slot,
 * this let a click on it silently hit "Quit to desktop" (the very next row)
 * instead -- reported as "I clicked unlock all and I think it crashed"
 * (user, 2026-09-18): the game didn't crash, __QuitToDesktop's ROW_ACTION
 * fired and closed it via the game's normal exit path. Fix: bound the scan
 * to the same [s_scroll, pLast] range the draw loop actually renders.
 *
 * D316 fix: the band used to be [Y-3, Y+12) -- 3 units above OV_ROW_Y but
 * 12 below it, a systematic downward bias baked in independent of the
 * mouse-mapping bug above. Centered here on OV_ROW_Y so a borderline click
 * no longer favors the row below. */
static int overlayRowAtY(double oy)
{
    int maxV = maxVisibleRows();
    int pLast = (s_visN - s_scroll < maxV) ? s_visN - 1 : s_scroll + maxV - 1;
    for (int p = s_scroll; p <= pLast; p++) {
        double top = OV_ROW_Y(p - s_scroll) - OV_LINE / 2;
        if (oy >= top && oy < top + OV_LINE) {
            return p;
        }
    }
    return -1;
}

/* Rebuild the visible-row list and keep the selection in range. */
/* D237: move a visible-list position by dir (+1/-1) to the next row that
 * is not a category header; stays put at the ends. */
static int overlayStepSel(int pos, int dir)
{
    int p = pos;
    for (;;) {
        int q = p + dir;
        if (q < 0 || q >= s_visN) {
            /* nothing selectable that way: if we started on a header, try
             * the other direction once so we never rest on one */
            if (rows[s_visIdx[p]].kind == ROW_HEADER && dir > 0) {
                return overlayStepSel(pos, -1);
            }
            return (rows[s_visIdx[p]].kind == ROW_HEADER) ? pos : p;
        }
        p = q;
        if (rows[s_visIdx[p]].kind != ROW_HEADER) {
            return p;
        }
    }
}

static void overlayUpdateVisible(void)
{
    s_visN = 0;
    for (int i = 0; i < NUM_ROWS; i++) {
        /* The file chooser belongs to the GE front-end screen, not F10.
         * F10 in-stage always targets the active Bond file. */
        if (rows[i].kind != ROW_BOND_FILE &&
            !(rows[i].hidePtr && *rows[i].hidePtr != 0)) {
            s_visIdx[s_visN++] = i;
        }
    }
    if (s_visN == 0) {   /* cannot happen (toggles have no hide source) */
        s_visIdx[s_visN++] = 0;
    }
    if (s_sel < 0) {
        s_sel = 0;
    }
    if (s_sel >= s_visN) {
        s_sel = s_visN - 1;
    }
    /* D237: never rest on a category header -- step to the nearest row
     * below it (or above, at the end of the list). */
    if (rows[s_visIdx[s_sel]].kind == ROW_HEADER) {
        s_sel = overlayStepSel(s_sel, +1);
    }
}

/* Keep the selected row on screen: shift the window ONLY when the selection
 * is actually outside it, by the minimum amount needed.
 *
 * D304 fix: this used to unconditionally set `s_scroll = s_sel - (maxV-1)`
 * -- i.e. bottom-anchor the selected row -- on every single call, including
 * every mouse click. A click on a row already visible (anywhere but the very
 * last slot) still forced the whole list to re-scroll so that row landed at
 * the bottom, shifting every row's on-screen position for the rest of that
 * same frame -- so whatever row the user then saw/clicked at that same
 * screen position was a DIFFERENT (usually the next, i.e. "below") setting.
 * Reported as "the F10 menu keeps jumping to the setting below when I left
 * click" (user, 2026-09-18) -- clicking any row not already at the bottom
 * slot reproduced it every time. Fix: only move the window when the
 * selection is above the top or below the bottom of the current view. */
static void overlayUpdateScroll(void)
{
    int maxV = maxVisibleRows();
    if (s_visN <= maxV) {
        s_scroll = 0;
        return;
    }
    if (s_sel < s_scroll) {
        s_scroll = s_sel;
    } else if (s_sel > s_scroll + maxV - 1) {
        s_scroll = s_sel - (maxV - 1);
    }
    if (s_scroll < 0) {
        s_scroll = 0;
    }
    if (s_scroll > s_visN - maxV) {
        s_scroll = s_visN - maxV;
    }
}

/* The close box brackets the title row at the panel's right edge. */
#define OV_CB_X0   (OV_RIGHT - 14)
#define OV_CB_X1   (OV_RIGHT + 7)
#define OV_CB_Y0   (OV_TOP - 3)
#define OV_CB_Y1   (OV_TOP + 12)
static int overlayInCloseBox(double ox, double oy)
{
    return ox >= OV_CB_X0 && ox <= OV_CB_X1 &&
           oy >= OV_CB_Y0 && oy <= OV_CB_Y1;
}

/* ------------------------------------------------------------------------ */

static void resolveCb(const char *key, int type, void *ptr, double min, double max,
                      double step, const char *label, const char *const *names,
                      void *ctx)
{
    (void)step; (void)label; (void)names; (void)ctx;
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            rows[i].found  = 1;
            rows[i].type   = type;
            rows[i].ptr    = ptr;
            rows[i].cfgMin = min;
            rows[i].cfgMax = max;
            return;
        }
    }
}

static void overlayInit(void)
{
    if (s_inited) {
        return;
    }
    s_inited = 1;

    /* Publish display metadata so config.c / future consumers can see it,
     * without config.c knowing any specific key. */
    for (int i = 0; i < NUM_ROWS; i++) {
        configSetOptionMeta(rows[i].key, rows[i].label, rows[i].step, rows[i].names);
    }
    configForEachOption(resolveCb, NULL);

    for (int i = 0; i < NUM_ROWS; i++) {
        if (rows[i].kind == ROW_RES || rows[i].kind == ROW_ACTION ||
            rows[i].kind == ROW_HEADER || rows[i].kind == ROW_BOND_FILE ||
            watchSettingsFieldForKey(rows[i].key) >= 0) {
            rows[i].found = 1;   /* not config-backed */
            continue;
        }
        if (!rows[i].found) {
            sysLogPrintf(LOG_WARNING, "optionsoverlay: option '%s' not registered",
                         rows[i].key);
        }
    }

    /* Resolve the hidden-while-on sources (manual % rows vs their auto
     * toggles), then build the initial visible list. */
    for (int i = 0; i < NUM_ROWS; i++) {
        if (!rows[i].hiddenIfOn) {
            continue;
        }
        for (int j = 0; j < NUM_ROWS; j++) {
            if (strcmp(rows[j].key, rows[i].hiddenIfOn) == 0 && rows[j].found) {
                rows[i].hidePtr = rows[j].ptr;
                break;
            }
        }
    }
    /* D356: on a header row, saveScoped means "this section contains
     * per-file rows" (drives the "(File N)" title annotation in both UIs);
     * on a content row it is the literal per-file flag from the table. */
    for (int i = 0; i < NUM_ROWS; i++) {
        if (rows[i].kind != ROW_HEADER) {
            continue;
        }
        int has = 0;
        for (int j = i + 1; j < NUM_ROWS && rows[j].kind != ROW_HEADER; j++)
            has |= rows[j].saveScoped;
        rows[i].saveScoped = has;
    }
    overlayUpdateVisible();

    /* Build the windowed-resolution preset list: presets that fit the desktop,
     * plus the current window size snapped to the nearest surviving entry. */
    {
        int dw = 1920, dh = 1080;
        videoGetDesktopSize(&dw, &dh);
        s_resFitN = 0;
        for (int i = 0; i < NUM_RES; i++) {
            if (kResList[i][0] <= dw && kResList[i][1] <= dh) {
                s_resFit[s_resFitN++] = i;
            }
        }
        if (s_resFitN == 0) {
            s_resFit[s_resFitN++] = 0;
        }
        int cw = 0, ch = 0;
        videoGetWindowSize(&cw, &ch);
        long best = -1;
        for (int k = 0; k < s_resFitN; k++) {
            int i = s_resFit[k];
            long d = labs((long)kResList[i][0] - cw) +
                     labs((long)kResList[i][1] - ch);
            if (best < 0 || d < best) { best = d; s_resSel = k; }
        }
    }

    const char *e = getenv("GE_OPTIONSOVERLAY");
    if (e && atoi(e) != 0) {
        s_open = 1;
        sysLogPrintf(LOG_INFO, "optionsoverlay: auto-opened (GE_OPTIONSOVERLAY)");
    }
}

static double rowGet(const struct Row *r)
{
    int field = watchSettingsFieldForKey(r->key);
    if (field >= 0) return (double)watchSettingsRead(field);
    if (!r->found || !r->ptr) {
        return 0.0;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   return (double)*(int *)r->ptr;
    case CONFIG_OPT_UINT:  return (double)*(unsigned int *)r->ptr;
    case CONFIG_OPT_FLOAT: return (double)*(float *)r->ptr;
    default:               return 0.0;
    }
}

static double rowLo(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMin : r->cfgMin;
}
static double rowHi(const struct Row *r)
{
    return (r->uiMin != r->uiMax) ? r->uiMax : r->cfgMax;
}

static struct Row *rowByKey(const char *key)
{
    for (int i = 0; i < NUM_ROWS; i++) {
        if (strcmp(rows[i].key, key) == 0) {
            return &rows[i];
        }
    }
    return NULL;
}

static int s_linkDepth = 0;   /* re-entrancy guard for the sens link below */

static void rowSet(struct Row *r, double v);

static void rowSetCommit(struct Row *r, double v, int commit)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (lo != hi) {
        if (v < lo) v = lo;
        if (v > hi) v = hi;
    }
    int field = watchSettingsFieldForKey(r->key);
    if (field >= 0) {
        watchSettingsSet(field, (int)lround(v), commit);
        return;
    }
    switch (r->type) {
    case CONFIG_OPT_INT:   *(int *)r->ptr = (int)lround(v); break;
    case CONFIG_OPT_UINT:  *(unsigned int *)r->ptr = (unsigned int)(v < 0 ? 0 : lround(v)); break;
    case CONFIG_OPT_FLOAT: *(float *)r->ptr = (float)v; break;
    default: return;
    }

    /* Live-apply the video knobs that need a fast3d/SDL call. Everything else
     * is read straight off the pointer by its owner every frame/poll. */
    if (strcmp(r->key, "Video.Fullscreen") == 0) {
        videoRequestFullscreen((int)lround(v));
    } else if (strncmp(r->key, "Video.", 6) == 0 && !r->restart) {
        videoRequestLiveConfig();
    }

    /* Linked aim/turn sensitivity (Input.SensLink, default on): moving either
     * knob scales the other to hold the stock default ratio -- AimModeSens 38
     * : MouseTurnSpeed 50 (the D194/D238 calibrated defaults). */
    if (!s_linkDepth && strcmp(r->key, "Input.SensLink") != 0) {
        struct Row *lk = rowByKey("Input.SensLink");
        if (lk && lk->found && *(int *)lk->ptr != 0) {
            struct Row *o = NULL;
            double nv = 0.0;
            if (strcmp(r->key, "Input.MouseTurnSpeed") == 0) {
                o = rowByKey("Input.AimModeSens");
                nv = v * 38.0 / 50.0;
            } else if (strcmp(r->key, "Input.AimModeSens") == 0) {
                o = rowByKey("Input.MouseTurnSpeed");
                nv = v * 50.0 / 38.0;
            }
            if (o && o->found) {
                s_linkDepth = 1;
                rowSet(o, nv);
                s_linkDepth = 0;
            }
        }
    }
}

static void rowSet(struct Row *r, double v) { rowSetCommit(r, v, 1); }

/* D356 reset rows (defined below, in the reset block). */
static struct Row *rowAt(int i);
static int isResetRow(const struct Row *r);
static void rowActivateReset(struct Row *r);

static void rowAdjust(struct Row *r, int dir)
{
    if (!r->found || r->kind == ROW_HEADER) {   /* D237: headers have no value */
        return;
    }
    if (r->kind == ROW_BOND_FILE) {
        if (current_menu == MENU_PC_OPTIONS) watchSettingsChooseFile(dir);
        return;
    }
    if (watchSettingsFieldForKey(r->key) >= 0 && !watchSettingsAvailable()) return;
    double v = rowGet(r);
    switch (r->kind) {
    case ROW_TOGGLE:
        rowSet(r, (v != 0.0) ? 0.0 : 1.0);
        break;
    case ROW_MSAA: {
        int idx = 0;
        for (int i = 0; i < 4; i++) {
            if (kMsaaSeq[i] == (int)lround(v)) idx = i;
        }
        /* Wrap like a normal settings-menu cycle: OFF->2x->4x->8x->OFF, in
         * both directions (left/right click and arrows all roll). */
        idx = (idx + dir + 4) % 4;
        rowSet(r, (double)kMsaaSeq[idx]);
        break;
    }
    case ROW_ENUM: {
        double lo = r->cfgMin, hi = r->cfgMax;
        v += dir;
        if (v < lo) v = hi;
        if (v > hi) v = lo;
        rowSet(r, v);
        break;
    }
    case ROW_FPSCAP: {
        int idx = 0;
        for (int i = 0; i < 2; i++) {
            if (kFpsCapSeq[i] == (int)lround(v)) idx = i;
        }
        idx = (idx + dir + 2) % 2;
        rowSet(r, (double)kFpsCapSeq[idx]);
        break;
    }
    case ROW_RES: {
        if (s_resFitN <= 0 || videoIsFullscreen()) {
            break;   /* resolution is windowed-only */
        }
        s_resSel += (dir >= 0) ? 1 : -1;
        if (s_resSel < 0) s_resSel = s_resFitN - 1;
        if (s_resSel >= s_resFitN) s_resSel = 0;
        int i = s_resFit[s_resSel];
        videoRequestWindowSize(kResList[i][0], kResList[i][1]);
        break;
    }
    case ROW_ACTION:
        /* D356: reset rows arm/confirm through the shared activation
         * contract (edge-triggered by their callers: fresh key press / fresh
         * click / A-press -- the held-repeat paths below never re-fire them).
         * The quit row is the only non-reset ROW_ACTION left. */
        if (isResetRow(r)) { rowActivateReset(r); break; }
        /* D293: same exit path as SDL_QUIT / Alt+F4 (video.c), just reachable
         * without OS window chrome or a keyboard. */
        sysLogPrintf(LOG_INFO, "optionsoverlay: quit to desktop requested");
        configSave();
        videoRequestQuit("Quit to desktop");   /* D344: never exit() off the host thread */
        break;
    default: /* ROW_SLIDER */
        rowSet(r, v + dir * r->step);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* D356: per-section "Reset to defaults" (plan Gate E, made concrete).       */
/* The activation contract (plan §5.4): edge-triggered, two-step            */
/* arm -> confirm within 3 s, one commit per confirmed activation,          */
/* disarm on timeout or navigating away. The arm state is shared by the     */
/* F10 overlay and the front options screen (they are never open at the     */
/* same time; both clear it on open/close).                                 */
/* ------------------------------------------------------------------------ */

#define RESET_ARM_US 3000000   /* arm expires after 3 s */

static int  s_resetArmedRow = -1;   /* rows[] index of the armed reset row */
static uint64_t s_resetArmedUs = 0;

static int isResetRow(const struct Row *r)
{
    return r->kind == ROW_ACTION && strncmp(r->key, "__Reset", 7) == 0;
}

int optionsRowIsReset(int i)
{
    struct Row *r = rowAt(i);
    return r && isResetRow(r);
}

/* ini rows reset to the port's C initializers (verified at implementation
 * time, D356 -- each entry cites its source variable): the reset table
 * mirrors them; config.c itself has no central default table (first-write
 * "defaults" are just the current values). __Resolution is NOT here: it is
 * action-backed (s_resSel + videoRequestWindowSize), not a registered config
 * row, so a numeric default cannot express it -- a documented exclusion (the
 * findings D356 entry states it); the player's resolution choice survives a
 * VIDEO reset. */
static const struct { const char *key; double def; } kResetDefaults[] = {
    /* INPUT (port/src/input.c initializers) */
    { "Input.MouseSensitivity", 100 },  /* static int mouseSensitivity = 100 */
    { "Input.MouseInvertY",      0 },   /* = 0 */
    { "Input.AimMode",           0 },   /* = AIMMODE_N64 (0) */
    { "Input.AimRange",          0 },   /* = 0 (PC) */
    { "Input.PadLookInvertY",    0 },   /* = 0 */
    { "Input.PadDeadzone",       7000 },/* = STICK_DEADZONE (7000) */
    { "Input.PadTriggerPct",     23 },  /* = 23 */
    /* GRAPHICS (port/src/video.c initializers) */
    { "Video.MSAA",                 4 },   /* = 4 */
    { "Video.TextureFilter",        1 },   /* = 1 (bilinear) */
    { "Video.Anisotropy",           4 },   /* = 4 */
    { "Video.FovScale",            100 },  /* = 100 */
    { "Video.NativeWidescreen",      1 },  /* = 1 */
    { "Video.WidescreenAuto",        1 },  /* = 1 */
    { "Video.SafeAreaCrop",          1 },  /* = 1 */
    { "Video.DrawDistance",        150 },  /* = 150 (D218) */
    { "Video.DrawDistanceAutoFov",   1 },  /* = 1 (D218) */
    { "Video.LodDistance",         150 },  /* = 150 (D249) */
    { "Video.LodDistanceAutoFov",    0 },  /* = 0 (standalone perf lever) */
    /* GAMEPLAY ini rows (port/src/video.c initializers) */
    { "Game.SkipIntro",   0 },   /* = 0 */
    { "Game.NoHitFlash",  0 },   /* = 0 */
    { "Game.AllUnlocked", 0 },   /* = 0 -- D257's "default ON" note was stale */
    { "Game.HudScale",   100 },  /* = 100 (D226) */
    /* VIDEO (port/src/video.c initializers; DisplayFPS in overlayConfigInit) */
    { "Video.Fullscreen", 0 },   /* = 0 (windowed) */
    { "Video.VSync",        1 }, /* = 1 (on) */
    { "Video.FpsCap",      60 }, /* = 60 */
    { "Video.DisplayFPS",   0 }, /* registered 0 */
};

static double kResetDefault(const char *key)
{
    for (size_t i = 0; i < sizeof(kResetDefaults) / sizeof(kResetDefaults[0]); i++)
        if (strcmp(kResetDefaults[i].key, key) == 0) return kResetDefaults[i].def;
    return -1.0;
}

/* The section's DECLARED row range (its header through the next header) --
 * NOT the visible list, so conditionally hidden rows (the manual draw/LOD %
 * sliders while their auto-FOV toggles are on) are reset too; their live
 * values are written through the same ptr/rowSet path, visibility
 * irrelevant. Per row, its OWN scope is reset: watch rows -> the selected
 * file's BLANKSAVEDATA values through the normal commit path (front: direct
 * write; stage: D352 queue, applied+persisted by the game thread); ini rows
 * -> the table above (applied live now, persisted like any ini edit when the
 * screen/overlay closes). No cross-scope surprise: a section reset touches
 * only that section's rows, and a file reset never touches other files. */
static void rowResetSection(int iReset)
{
    int hdr = iReset - 1;
    while (hdr >= 0 && rows[hdr].kind != ROW_HEADER) hdr--;
    if (hdr < 0) {
        sysLogPrintf(LOG_WARNING, "optionsoverlay: reset row %d has no section header", iReset);
        return;
    }
    int end = hdr + 1;
    while (end < NUM_ROWS && rows[end].kind != ROW_HEADER) end++;
    int nWatch = 0, nIni = 0;
    for (int j = hdr + 1; j < end; j++) {
        struct Row *r = &rows[j];
        if (r->kind == ROW_ACTION) continue;             /* reset rows, Quit */
        if (strcmp(r->key, "__Resolution") == 0) continue;  /* documented exclusion */
        int field = watchSettingsFieldForKey(r->key);
        if (field >= 0) {
            watchSettingsSet((enum WatchSettingField)field,
                             watchSettingsBlankValue((enum WatchSettingField)field), 1);
            nWatch++;
        } else if (r->found && r->ptr) {
            double def = kResetDefault(r->key);
            if (def < 0.0) continue;
            rowSetCommit(r, def, 1);
            nIni++;
        }
    }
    sysLogPrintf(LOG_INFO, "optionsoverlay: section reset '%s': %d file row(s), %d ini row(s)",
                 rows[hdr].label, nWatch, nIni);
}

/* Arm (first edge) or confirm (second edge within RESET_ARM_US). Callers
 * guarantee edges: the front screen routes only fresh presses (its held-key
 * repeat is suppressed for reset rows) and F10's fresh-press / fresh-click
 * paths; the held-repeat branches skip reset rows entirely. */
static void rowActivateReset(struct Row *r)
{
    if (!isResetRow(r)) return;
    uint64_t now = sysGetMicroseconds();
    int i = (int)(r - rows);
    if (s_resetArmedRow == i && now - s_resetArmedUs <= RESET_ARM_US) {
        s_resetArmedRow = -1;
        rowResetSection(i);   /* exactly one commit per confirmed activation */
        return;
    }
    s_resetArmedRow = i;
    s_resetArmedUs = now;
    int h = i - 1;
    while (h >= 0 && rows[h].kind != ROW_HEADER) h--;
    sysLogPrintf(LOG_INFO, "optionsoverlay: section reset '%s' armed (confirm within 3 s)",
                 h >= 0 ? rows[h].label : "?");
}

void optionsRowActivateReset(int i)
{
    struct Row *r = rowAt(i);
    if (r) rowActivateReset(r);
}

/* Timeout / navigate-away disarm. selRow = the rows[] index the UI has
 * selected (any other value, e.g. -1, disarms). */
void optionsResetMaintain(int selRow)
{
    if (s_resetArmedRow < 0) return;
    uint64_t now = sysGetMicroseconds();
    if (s_resetArmedRow != selRow || now - s_resetArmedUs > RESET_ARM_US)
        s_resetArmedRow = -1;
}

void optionsResetClear(void)
{
    s_resetArmedRow = -1;
}

/* ------------------------------------------------------------------------ */

void optionsOverlayToggle(void)
{
    overlayInit();
    if (s_open) {
        int pending = SDL_AtomicSet(&s_dragWatchField, 0);
        if (pending > 0) watchSettingsCommit(pending - 1);
    }
    s_open = !s_open;
    sysLogPrintf(LOG_INFO, "optionsoverlay: %s", s_open ? "opened" : "closed");
    if (!s_open) {
        optionsResetClear();   /* D356: closing the overlay disarms a pending reset */
        configSave();
    }
}

int optionsOverlayIsOpen(void)
{
    if (!s_inited) {
        overlayInit();
    }
    return s_open;
}

void optionsOverlayScroll(int dir)
{
    if (!s_inited) {
        overlayInit();
    }
    if (!s_open || dir == 0) {
        return;
    }
    /* D314: this runs on the host thread (SDL wheel event), while HandleInput
     * and Emit rebuild s_visIdx/s_visN/s_scroll on the scheduler thread; the
     * old direct overlayUpdateVisible() + s_sel write here could leave Emit
     * drawing a half-rebuilt list. Queue the notches instead (D287 pattern);
     * optionsOverlayHandleInput() applies them on the scheduler thread. */
    SDL_AtomicAdd(&s_wheelPending, dir);
}

/* Scheduler thread: apply wheel notches queued by optionsOverlayScroll().
 * wheel-up -> move up the list; clamps at both ends like a normal PC settings
 * list (wrapping read as a duplicate) and skips D237 headers. */
static void overlayApplyWheel(void)
{
    int n = SDL_AtomicSet(&s_wheelPending, 0);
    while (n != 0) {
        s_sel = overlayStepSel(s_sel, (n > 0) ? -1 : 1);
        n += (n > 0) ? -1 : 1;
    }
}

/* Set a slider row from an overlay-space x inside its value bar, snapped to
 * the row's step. */
static void sliderSetFromX(struct Row *r, double ox)
{
    double lo = rowLo(r), hi = rowHi(r);
    if (hi <= lo) {
        return;
    }
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);
    double f = (ox - bx0) / (double)(bx1 - bx0);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    double v = lo + f * (hi - lo);
    double step = (r->step > 0.0) ? r->step : 1.0;
    v = lround(v / step) * step;
    rowSetCommit(r, v, 0); /* slider drag: save only on release */
}

/* D314 (findings.md): F10 menu items flicker/mis-land on the file-select
 * screen, mechanism unpinned. Ruled out: viGetX/viGetY (stable per-screen,
 * set once by front.c) and non-determinism in the display list. This logs
 * the remaining suspects -- viGetY(), s_visN, s_scroll, s_sel -- once per
 * frame while the overlay is open, to catch whichever one oscillates during
 * a file-select repro. Env-gated, cached once (not re-queried per frame --
 * see D302: a hot-path getenv() regression cost a real bug before). */
static int s_d314Enabled = -1;   /* -1 = not yet resolved */

void optionsOverlayHandleInput(void)
{
    static int prevUp, prevDn, prevLf, prevRt, prevLmb, prevRmb;
    /* D347: hold-to-repeat state (18/4-frame cadence, same as the options
     * screen's D345(e)/(f) blocks). */
    static int navDir = 0, navTimer = 0, adjDir = 0, adjTimer = 0;

    if (!s_open) {
        prevUp = prevDn = prevLf = prevRt = prevLmb = prevRmb = 0;
        s_dragRow = -1;
        SDL_AtomicSet(&s_dragWatchField, 0);
        navDir = adjDir = 0;
        SDL_AtomicSet(&s_wheelPending, 0);
        return;
    }

    if (s_d314Enabled < 0) {
        s_d314Enabled = getenv("GE_D314") ? 1 : 0;
    }
    if (s_d314Enabled) {
        fprintf(stderr, "GE_D314 viGetY=%d visN=%d scroll=%d sel=%d\n",
                (int)viGetY(), s_visN, s_scroll, s_sel);
    }

    /* The overlay owns the mouse while it is open: force the OS cursor free +
     * visible (a stage poll would otherwise leave it locked/hidden). */
    inputSuspendForOverlay();

    /* D356: a pending reset arm expires after 3 s or when the selection
     * leaves the armed row; the overlay's selection is s_visIdx[s_sel]. */
    optionsResetMaintain(s_visIdx[s_sel]);

    overlayUpdateVisible();   /* % rows may have appeared/vanished (auto toggles) */
    overlayApplyWheel();      /* D314: wheel notches queued by the host thread */
    overlayUpdateScroll();

    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    int mx = 0, my = 0;
    Uint32 mb = SDL_GetMouseState(&mx, &my);
    int lmb = (mb & SDL_BUTTON(SDL_BUTTON_LEFT))  ? 1 : 0;
    int rmb = (mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? 1 : 0;

    /* Gamepad navigation (controller-only machines, e.g. Steam Deck): the
     * D-pad or left stick moves the selection; A/X step forward (the Enter
     * equivalent), B/Y step back; Start closes. OR-ed into the same edge
     * logic as the keyboard, so repeat/clamp/scroll behaviour is identical.
     * input.c swallows the pad while we are open, so none of this reaches
     * the game. (Select also closes -- handled in input.c's toggle, which
     * runs before this one.)
     * D347: D-pad left/right and stick-X join A/X/B/Y as value adjust --
     * the standard controller convention; A/X/B/Y keep working. */
    int gUp = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_UP)
           || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) < -12000;
    int gDn = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_DOWN)
           || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTY) > 12000;
    int gLf = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_LEFT)
            || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTX) < -12000;
    int gRt = inputPadButton(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            || inputPadAxis(0, SDL_CONTROLLER_AXIS_LEFTX) > 12000;
    int up = ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_KP_8] || gUp;
    int dn = ks[SDL_SCANCODE_DOWN]  || ks[SDL_SCANCODE_KP_2] || gDn;
    int lf = ks[SDL_SCANCODE_LEFT]  || ks[SDL_SCANCODE_KP_4]
          || gLf
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_B)
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_Y);
    int rt = ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6]
          || ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]
          || gRt
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_A)
          || inputPadButton(0, SDL_CONTROLLER_BUTTON_X);

    static int prevStart = 0;   /* not reset while closed: a Start held across
                                  close must not re-close on the next open */
    int startNow = inputPadButton(0, SDL_CONTROLLER_BUTTON_START);
    if (startNow && !prevStart) optionsOverlayToggle();   /* Start closes */
    prevStart = startNow;

    /* ---- keyboard / D-pad nav (clamped at the ends; scroll follows).
     * D347: hold-to-repeat so a controller (and held keys) can traverse the
     * list without one-press-per-row. */
    {
        int vdir = (dn && !prevDn) ? +1 : (up && !prevUp) ? -1 : 0;
        if (vdir != 0) {
            navDir = vdir;
            navTimer = 18;
            s_sel = overlayStepSel(s_sel, vdir);   /* skips headers */
        } else if ((up || dn) && navDir != 0) {
            if (--navTimer <= 0) {
                navTimer = 4;
                s_sel = overlayStepSel(s_sel, navDir);
            }
        } else {
            navDir = 0;
        }
    }
    overlayUpdateScroll();
    /* ---- value adjust (D347: D-pad/stick left-right wired in; hold repeats) ---- */
    {
        int dir = (rt && !prevRt) ? +1 : (lf && !prevLf) ? -1 : 0;
        if (dir != 0) {
            adjDir = dir;
            adjTimer = 18;
            rowAdjust(&rows[s_visIdx[s_sel]], dir);
        } else if ((rt || lf) && adjDir != 0) {
            if (--adjTimer <= 0) {
                adjTimer = 4;
                /* D356: reset actions are edge-triggered -- the held-repeat
                 * re-fires adjust rows but must never re-fire a reset (a held
                 * key could arm AND confirm, or commit repeatedly). */
                if (!isResetRow(&rows[s_visIdx[s_sel]]))
                    rowAdjust(&rows[s_visIdx[s_sel]], adjDir);
            }
        } else {
            adjDir = 0;
        }
    }

    /* ---- mouse ----
     * D316: mx/my are raw window pixels, but the overlay's own 2D content
     * is drawn into whatever on-window rect the safe-area crop currently
     * maps the logical (viGetX() x viGetY()) canvas to -- NOT the full
     * window whenever that rect is inset (default-on: any in-game "Full"
     * viewport insets it). Map through the real forward transform's rect
     * (gfx_get_ui_screen_rect) instead of a naive window-size scale so a
     * click lands on the same row it visually appears over. */
    int32_t rx = 0, ry = 0, rw = 0, rh = 0;
    gfx_get_ui_screen_rect(&rx, &ry, &rw, &rh);
    if (rw > 0 && rh > 0) {
        double ox = (double)(mx - rx) * (double)viGetX() / rw;
        double oy = (double)(my - ry) * (double)viGetY() / rh;
        int hoverVis = overlayRowAtY(oy);
        int onClose  = overlayInCloseBox(ox, oy);

        /* No hover-to-highlight: merely moving the mouse must not move the
         * selection or scroll the window (hovering near a list edge fed the
         * new row back into the cursor and made the bottom twitch/echo).
         * Selection moves only by click, wheel, or arrows. */

        /* left press. A click in the label column only focuses the row; a
         * click in the value/control column (>= the bar-span start) changes
         * it -- so clicking to select a toggle doesn't also flip it. */
        s32 bx0, bx1;
        sliderBarSpan(&bx0, &bx1);
        if (lmb && !prevLmb) {
            if (onClose) {
                optionsOverlayToggle();   /* close + configSave */
                return;
            }
            if (hoverVis >= 0 && rows[s_visIdx[hoverVis]].kind != ROW_HEADER) {
                s_sel = hoverVis;         /* explicit click -> select */
                overlayUpdateScroll();
                if (ox >= bx0) {
                    struct Row *r = &rows[s_visIdx[hoverVis]];
                    if (r->kind == ROW_SLIDER && r->found) {
                        sliderSetFromX(r, ox);
                        s_dragRow = s_visIdx[hoverVis];
                        int field = watchSettingsFieldForKey(r->key);
                        SDL_AtomicSet(&s_dragWatchField, field + 1);
                    } else {
                        rowAdjust(r, +1);   /* toggle / cycle forward (wraps) */
                    }
                }
            }
        }
        /* drag a slider */
        if (lmb && s_dragRow >= 0 && rows[s_dragRow].kind == ROW_SLIDER) {
            sliderSetFromX(&rows[s_dragRow], ox);
        }
        if (!lmb) {
            if (s_dragRow >= 0) {
                int field = watchSettingsFieldForKey(rows[s_dragRow].key);
                if (field >= 0) watchSettingsCommit(field);
            }
            s_dragRow = -1;
            SDL_AtomicSet(&s_dragWatchField, 0);
        }
        /* right press in the value column: cycle back / decrement */
        if (rmb && !prevRmb && hoverVis >= 0 && !onClose && ox >= bx0 &&
            rows[s_visIdx[hoverVis]].kind != ROW_HEADER) {
            s_sel = hoverVis;
            overlayUpdateScroll();
            rowAdjust(&rows[s_visIdx[hoverVis]], -1);
        }
    }

    prevUp = up; prevDn = dn; prevLf = lf; prevRt = rt;
    prevLmb = lmb; prevRmb = rmb;
}

/* ------------------------------------------------------------------------ */

#define OV_BUF_CMDS 8192
static Gfx s_buf[OV_BUF_CMDS];

static Gfx *fillRect(Gfx *gdl, s32 x0, s32 y0, s32 x1, s32 y1,
                     u8 r, u8 g, u8 b, u8 a)
{
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
    gDPFillRectangle(gdl++, x0, y0, x1, y1);
    return gdl;
}

static void valueText(int i, char *out, int n)
{
    const struct Row *r = &rows[i];
    double v = rowGet(r);
    if (r->kind == ROW_BOND_FILE) {
        int f = watchSettingsFolder();
        if (f < 0) snprintf(out, n, "Select file");
        else snprintf(out, n, "File %d", f + 1);
        return;
    }
    if (watchSettingsFieldForKey(r->key) >= 0 && !watchSettingsAvailable()) {
        snprintf(out, n, "Select file");
        return;
    }
    if (r->kind == ROW_RES) {
        if (videoIsFullscreen()) {
            snprintf(out, n, "(fullscreen)");
        } else if (s_resFitN <= 0) {
            snprintf(out, n, "n/a");
        } else {
            int i2 = s_resFit[s_resSel];
            snprintf(out, n, "%d x %d", kResList[i2][0], kResList[i2][1]);
        }
        return;
    }
    if (r->kind == ROW_ACTION) {
        if (isResetRow(r)) {
            /* D356: the label is the button; the value column shows the
             * armed state so the confirm step is discoverable. */
            snprintf(out, n, s_resetArmedRow == i ? "Confirm" : "");
        } else {
            snprintf(out, n, "[ENTER]");
        }
        return;
    }
    if (r->kind == ROW_MSAA) {
        if ((int)lround(v) <= 1) snprintf(out, n, "None");
        else                     snprintf(out, n, "%dx", (int)lround(v));
        return;
    }
    if ((r->kind == ROW_TOGGLE || r->kind == ROW_ENUM) && r->names) {
        int idx = (int)lround(v);
        int cnt = 0;
        while (r->names[cnt]) cnt++;
        if (idx >= 0 && idx < cnt) {
            snprintf(out, n, "%s", r->names[idx]);
            return;
        }
    }
    if (r->kind == ROW_SLIDER && r->type == CONFIG_OPT_FLOAT) {
        snprintf(out, n, "%.2f", v);
        return;
    }
    if (r->kind == ROW_FPSCAP) {
        int fps = (int)lround(v);
        if (fps <= 0) snprintf(out, n, "Uncapped");
        else          snprintf(out, n, "%d FPS", fps);
        return;
    }
    /* D346: integer sliders -- optional raw->display divide + unit suffix. */
    if (r->dispDiv > 0) {
        v = (double)(int)lround(v / (double)r->dispDiv);
    }
    if (r->unit) {
        snprintf(out, n, "%d%s", (int)lround(v), r->unit);
    } else {
        snprintf(out, n, "%d", (int)lround(v));
    }
}

static Gfx *drawText(Gfx *gdl, s32 x, s32 y, const char *str, u32 colour)
{
    s32 px = x, py = y;
    /* width/height are the on-screen CLIP rect textRenderGlyph tests against
     * (clipX=start x, clipY=start y, +clipWidth/+clipHeight), NOT the text's
     * own measured size -- passing the measured w/h clipped every glyph out
     * (baseline+height > measured h => nothing drawn).  Match the game: pass
     * the full 2D viewport, like bondview2.c's debug-text path. */
    return textRender(gdl, &px, &py, (char *)str, ptrFontBankGothicChars,
                      ptrFontBankGothic, colour, viGetX(), viGetY(), 0, 0);
}

static s32 measureText(const char *str)
{
    s32 h = 0, w = 0;
    textMeasure(&h, &w, (char *)str, ptrFontBankGothicChars, ptrFontBankGothic, 0);
    return w;
}

/* Right-aligned: the string ends at xr. */
static Gfx *drawTextR(Gfx *gdl, s32 xr, s32 y, const char *str, u32 colour)
{
    return drawText(gdl, xr - measureText(str), y, str, colour);
}

Gfx *optionsOverlayEmit(void)
{
    if (!s_inited) {
        overlayInit();
    }

    fpsTick();

    if (!s_open) {
        if (!s_showFps || !s_fpsText[0]) {
            return NULL;   /* nothing appended -> golden dumps byte-identical */
        }
        /* D213: FPS-only mini DL (top-right), panel closed. */
        const s32 fw = viGetX();
        const s32 fh = viGetY();
        Gfx *fgdl = s_buf;
        gDPPipeSync(fgdl++);
        gDPSetCycleType(fgdl++, G_CYC_1CYCLE);
        gDPSetTexturePersp(fgdl++, G_TP_NONE);
        gDPSetScissor(fgdl++, G_SC_NON_INTERLACE, 0, 0, fw, fh);
        fgdl = microcode_constructor(fgdl);
        fgdl = drawTextR(fgdl, fw - 6, 6, s_fpsText, 0x40ff60ff);
        gDPPipeSync(fgdl++);
        gSPEndDisplayList(fgdl++);
        return s_buf;
    }

    overlayUpdateVisible();
    overlayUpdateScroll();

    const s32 W = viGetX();
    const s32 H = viGetY();
    const s32 right = OV_RIGHT;
    const s32 panelTop = OV_TOP - 9;
    /* Last visible position actually drawn (window may be shorter than the
     * list at small 2D viewports -- the rest is reached by scrolling).
     * D304 fix: the "everything fits" branch was `s_visN - s_scroll`, which
     * is a COUNT, not the last valid 0-based index -- since that branch only
     * runs when s_scroll==0 (overlayUpdateScroll's own invariant), this
     * evaluated to `s_visN`, one past the last valid entry, so both draw
     * loops below (`for (p = s_scroll; p <= pLast; p++)`) read one row past
     * the end of `s_visIdx[]`/`rows[]` on any 2D viewport large enough to fit
     * the whole list without scrolling (e.g. the larger front-end 440x330
     * screen once a couple of rows are pulled from the menu, as just
     * happened above). Should be `s_visN - 1`, matching overlayRowAtY's
     * identical fix. */
    const int maxV  = maxVisibleRows();
    const int pLast = (s_visN - s_scroll < maxV) ? s_visN - 1
                                                 : s_scroll + maxV - 1;
    const s32 panelBottom = OV_ROW_Y(pLast) + OV_LINE / 2 + 3;
    s32 bx0, bx1;
    sliderBarSpan(&bx0, &bx1);
    Gfx *gdl = s_buf;

    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gdl++, G_TP_NONE);
    gDPSetScissor(gdl++, G_SC_NON_INTERLACE, 0, 0, W, H);

    /* ---- pass 1: all fills (G_CC_PRIMITIVE) ---- */
    gdl = fillRect(gdl, 0, 0, W, H, 0, 0, 0, 150);                       /* dim */
    gdl = fillRect(gdl, OV_X0 - 8, panelTop, W - (OV_X0 - 8), panelBottom,
                   8, 10, 24, 210);                                     /* panel */
    gdl = fillRect(gdl, OV_CB_X0, OV_CB_Y0, OV_CB_X1, OV_CB_Y1,
                   150, 40, 40, 235);                                   /* close */

    for (int p = s_scroll; p <= pLast; p++) {
        const struct Row *r = &rows[s_visIdx[p]];
        s32 rowY = OV_ROW_Y(p - s_scroll);
        if (p == s_sel) {
            gdl = fillRect(gdl, OV_X0 - 4, rowY - 3, W - (OV_X0 - 4),
                           rowY + OV_LINE - 4, 40, 46, 96, 220);
        }
        if (r->kind == ROW_SLIDER && r->found) {
            double lo = rowLo(r), hi = rowHi(r);
            double f = (hi > lo) ? (rowGet(r) - lo) / (hi - lo) : 0.0;
            if (f < 0) f = 0; if (f > 1) f = 1;
            s32 by = rowY + 3;
            gdl = fillRect(gdl, bx0, by, bx1, by + 5, 60, 60, 70, 220);
            gdl = fillRect(gdl, bx0, by, bx0 + (s32)((bx1 - bx0) * f), by + 5,
                           210, 200, 90, 255);
        }
    }

    /* ---- pass 2: text ---- */
    gdl = microcode_constructor(gdl);

    gdl = drawText(gdl, OV_X0, OV_TOP, "PC OPTIONS", 0xffe040ff);
    gdl = drawText(gdl, OV_X0, OV_TOP + OV_LINE,
                   "select: scroll/click, change: mouse/arrows",
                   0x8890a0ff);                                        /* hint line */
    gdl = drawText(gdl, (OV_CB_X0 + OV_CB_X1) / 2 - measureText("X") / 2,
                   OV_TOP, "X", 0xffffffff);                            /* close glyph */

    for (int p = s_scroll; p <= pLast; p++) {
        const struct Row *r = &rows[s_visIdx[p]];
        s32 rowY = OV_ROW_Y(p - s_scroll);
        u32 col = (p == s_sel) ? 0xffffffff : 0xc0c0c8ff;
        char val[48];

        if (r->kind == ROW_HEADER) {   /* D237 category label, no value */
            gdl = drawText(gdl, OV_X0, rowY, (char *)r->label, 0xffe040ff);
            /* D356: sections that contain per-file rows carry the active
             * file in the header ("GAMEPLAY (File 1)"), PD-style -- in-stage
             * the active file, front end the resolved front target. */
            if (r->saveScoped) {
                int f = watchSettingsActiveFolder();
                char note[16];
                if (f < 0) snprintf(note, sizeof(note), "(no file)");
                else       snprintf(note, sizeof(note), "(File %d)", f + 1);
                gdl = drawText(gdl, OV_X0 + measureText(r->label) + 6, rowY, note, 0x909090ff);
            }
            continue;
        }
        gdl = drawText(gdl, OV_LABEL_X, rowY, (char *)r->label,
                       r->found ? col : 0x808080ff);
        /* D357: per-file rows in the mixed-scope section carry a dim
         * "(profile)" tag (the section title already says which file). */
        if (r->saveScoped && r->kind != ROW_HEADER) {
            gdl = drawText(gdl, OV_LABEL_X + measureText(r->label) + 5, rowY,
                           "(profile)", 0x909090ff);
        }
        if (!r->found) {
            gdl = drawTextR(gdl, right, rowY, "(n/a)", 0x808080ff);
            continue;
        }

        valueText(s_visIdx[p], val, sizeof(val));
        if (r->restart) {
            /* value left of the bar span, "(restart)" pinned to the edge */
            gdl = drawText(gdl, bx0, rowY, val, col);
            gdl = drawTextR(gdl, right, rowY, "(restart)", 0x909090ff);
        } else {
            gdl = drawTextR(gdl, right, rowY, val, col);
        }
    }

    gDPPipeSync(gdl++);
    gSPEndDisplayList(gdl++);

    if ((gdl - s_buf) > OV_BUF_CMDS) {
        sysLogPrintf(LOG_ERROR, "optionsoverlay: DL overflow (%d)", (int)(gdl - s_buf));
    }
    return s_buf;
}

/* ------------------------------------------------------------------------ */
/* D343 (M2): row API for the file-select options screen (frontoptions.c).   */
/* The screen shares this file's rows, value text and live-apply logic; it   */
/* keeps its own page/selection state. Both UIs call these from one thread   */
/* at a time: the screen gets no input while the F10 overlay is open.        */
/* ------------------------------------------------------------------------ */

static struct Row *rowAt(int i)
{
    overlayInit();
    return (i >= 0 && i < NUM_ROWS) ? &rows[i] : NULL;
}

int optionsRowCount(void)
{
    overlayInit();
    return NUM_ROWS;
}

int optionsRowIsHeader(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_HEADER;
}

/* Registered and not hidden by its "auto" toggle. */
int optionsRowIsShown(int i)
{
    struct Row *r = rowAt(i);
    return r && r->found && !(r->hidePtr && *r->hidePtr) &&
           (r->kind != ROW_BOND_FILE || current_menu == MENU_PC_OPTIONS);
}

const char *optionsRowLabel(int i)
{
    struct Row *r = rowAt(i);
    return r ? r->label : "";
}

int optionsRowIsSlider(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_SLIDER && rowHi(r) > rowLo(r);
}

/* D353: the explicit Bond-file chooser row (front options screen only). */
int optionsRowIsBondChooser(int i)
{
    struct Row *r = rowAt(i);
    return r && r->kind == ROW_BOND_FILE;
}

/* D356: content rows carry the literal per-file flag from the table; header
 * rows were recomputed at init to "this section contains per-file rows".
 * Both drive UI decoration (the "(profile)" tag, the "(File N)" annotation).
 * The F10 overlay and the front options screen are the only consumers. */
int optionsRowIsSaveScoped(int i)
{
    struct Row *r = rowAt(i);
    return r && r->saveScoped;
}

int optionsRowNeedsRestart(int i)
{
    struct Row *r = rowAt(i);
    return r && r->restart;
}

double optionsRowFraction(int i)
{
    struct Row *r = rowAt(i);
    if (!r || rowHi(r) <= rowLo(r)) {
        return 0.0;
    }
    double f = (rowGet(r) - rowLo(r)) / (rowHi(r) - rowLo(r));
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

/* D356: raw value (the fraction accessors are bar geometry only); the
 * reset probe verifies against raw defaults. */
double optionsRowGetValue(int i)
{
    struct Row *r = rowAt(i);
    return r ? rowGet(r) : 0.0;
}

void optionsRowSetFraction(int i, double f)
{
    struct Row *r = rowAt(i);
    if (!r || !r->found || rowHi(r) <= rowLo(r)) {
        return;
    }
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    double step = (r->step > 0.0) ? r->step : 1.0;
    double v = rowLo(r) + f * (rowHi(r) - rowLo(r));
    rowSetCommit(r, lround(v / step) * step, 0);
}

void optionsRowCommit(int i)
{
    struct Row *r = rowAt(i);
    if (r) {
        int field = watchSettingsFieldForKey(r->key);
        if (field >= 0) watchSettingsCommit(field);
    }
}

void optionsRowValueText(int i, char *out, int n)
{
    struct Row *r = rowAt(i);
    if (!r || n <= 0) {
        return;
    }
    valueText(i, out, n);
}

void optionsRowAdjust(int i, int dir)
{
    struct Row *r = rowAt(i);
    if (r) {
        rowAdjust(r, dir);
    }
}

/* ------------------------------------------------------------------------ */
/* D356 GE_WSPROBE_RESET dev hook (env-gated; driven from the game-thread   */
/* hook in watchSettingsGameTick, so the dispatch runs on the right        */
/* thread in both contexts). Drives the REAL arm -> confirm -> dispatch    */
/* UI path for EVERY section's reset row and verifies each section's       */
/* values after ITS dispatch (plan §5.8): watch rows vs BLANKSAVEDATA,     */
/* ini rows vs the defaults table, __Resolution untouched, and (front) a   */
/* second file's bytes unchanged (scope isolation).                        */
/* ------------------------------------------------------------------------ */

#define PROBE_MAX_SECTIONS 8
static int probeSectionIdxs[PROBE_MAX_SECTIONS];
static int probeSectionN = 0;
static int probeResSelBefore = -2;
static int probeOtherFolder = -1;
static save_data probeOtherBytes;

/* The header + end of the section a reset row belongs to. Written in
 * plain local-variable form on purpose: the pointer-increment style
 * (`while (*hdr >= 0 && ...) *hdr--;`) miscompiled under -O2 (the loop
 * pointer walked the stack and the faulted on garbage rows[] indices). */
static void probeSectionRange(int iReset, int *hdr, int *end)
{
    int h, e;
    if (iReset <= 0 || iReset >= NUM_ROWS) {
        sysLogPrintf(LOG_ERROR, "wsresetprobe: bad reset row index %d (NUM_ROWS %d)", iReset, NUM_ROWS);
        *hdr = -1; *end = -1;
        return;
    }
    h = iReset - 1;
    while (h >= 0 && rows[h].kind != ROW_HEADER) h--;
    e = h + 1;
    while (e < NUM_ROWS && rows[e].kind != ROW_HEADER) e++;
    *hdr = h;
    *end = e;
}

/* Verify every row of section iReset is at its default. Returns the
 * failure count. Watch rows compare against BLANKSAVEDATA via
 * watchSettingsRead (front: the saved file, stage: the post-drain
 * snapshot); ini rows against the defaults table. */
static int probeVerifySection(int iReset)
{
    int hdr, end;
    probeSectionRange(iReset, &hdr, &end);
    if (hdr < 0) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: section %d: no header", iReset);
        return 1;
    }
    int bad = 0;
    for (int j = hdr + 1; j < end; j++) {
        struct Row *r = &rows[j];
        if (r->kind == ROW_ACTION) continue;
        if (strcmp(r->key, "__Resolution") == 0) continue;   /* documented exclusion */
        int field = watchSettingsFieldForKey(r->key);
        if (field >= 0) {
            int want = watchSettingsBlankValue((enum WatchSettingField)field);
            int got = watchSettingsRead((enum WatchSettingField)field);
            if (got != want) {
                sysLogPrintf(LOG_WARNING, "wsresetprobe: section '%s': %s = %d, want %d (BLANKSAVEDATA)",
                             rows[hdr].label, r->key, got, want);
                bad++;
            }
        } else if (r->found && r->ptr) {
            double want = kResetDefault(r->key);
            if (want < 0.0) continue;   /* no default (the handler skips it too) */
            double got = optionsRowGetValue(j);
            if (got < want - 0.001 || got > want + 0.001) {
                sysLogPrintf(LOG_WARNING, "wsresetprobe: section '%s': %s = %g, want %g (default table)",
                             rows[hdr].label, r->key, got, want);
                bad++;
            }
        }
    }
    return bad;
}

/* Front context (tick 1): dirty one value per section, then arm + confirm
 * every section's reset row in order, verifying each section right after
 * its own dispatch (the front commit is direct). Finally: resolution
 * untouched + a second file's bytes unchanged (scope isolation). */
void optionsResetProbePrepare(void)
{
    overlayInit();   /* idempotent: resolves the ini rows (found + ptr) so
                     * the front probe dispatches every declared row, not
                     * just the watch rows */
    probeSectionN = 0;
    for (int i = 0; i < NUM_ROWS; i++)
        if (isResetRow(&rows[i]) && probeSectionN < PROBE_MAX_SECTIONS)
            probeSectionIdxs[probeSectionN++] = i;
    probeResSelBefore = s_resSel;
    int active = watchSettingsActiveFolder();
    sysLogPrintf(LOG_INFO, "wsresetprobe: front prepare start (active folder %d, %d sections)", active, probeSectionN);
    probeOtherFolder = -1;
    for (int f = FOLDER1; f < MAX_FOLDER_COUNT; f++) {
        if (f == active) continue;
        save_data *s = fileGetSaveForFoldernum((u32)f);
        if (s) {
            probeOtherFolder = f;
            probeOtherBytes = *s;
            break;
        }
    }
    sysLogPrintf(LOG_INFO, "wsresetprobe: other-folder snapshot done (folder %d)", probeOtherFolder);
    /* 1. dirty a distinct value in each section (its first adjustable row). */
    for (int s = 0; s < probeSectionN; s++) {
        int iR = probeSectionIdxs[s];
        int hdr, end;
        probeSectionRange(iR, &hdr, &end);
        if (hdr < 0) continue;
        int did = 0;
        for (int j = hdr + 1; j < end && !did; j++) {
            struct Row *r = &rows[j];
            if (r->kind == ROW_ACTION || strcmp(r->key, "__Resolution") == 0) continue;
            int field = watchSettingsFieldForKey(r->key);
            if (field >= 0) {
                int blank = watchSettingsBlankValue((enum WatchSettingField)field);
                int want = (blank > 1) ? 0 : 1;   /* distinct from the blank */
                if (watchSettingsRead((enum WatchSettingField)field) == want) continue;
                watchSettingsSet((enum WatchSettingField)field, want, 1);
                did = 1;
            } else if (r->found && r->ptr && (r->kind == ROW_TOGGLE || r->kind == ROW_ENUM ||
                       r->kind == ROW_SLIDER || r->kind == ROW_MSAA)) {
                optionsRowAdjust(j, 1);   /* one step off the default */
                did = 1;
            }
        }
        sysLogPrintf(LOG_INFO, "wsresetprobe: section '%s' dirty=%d", rows[hdr].label, did);
    }
    /* 2. arm + confirm every section, verifying each after its dispatch. */
    for (int s = 0; s < probeSectionN; s++) {
        int iReset = probeSectionIdxs[s];
        int hdr, end;
        probeSectionRange(iReset, &hdr, &end);
        if (hdr < 0) continue;
        optionsRowActivateReset(iReset);   /* arm */
        sysLogPrintf(LOG_INFO, "wsresetprobe: section %d armed", s);
        optionsRowActivateReset(iReset);   /* confirm -> commit */
        int bad = probeVerifySection(iReset);
        sysLogPrintf(LOG_INFO, "wsresetprobe: section %d confirmed + verified", s);
        if (s_resSel != probeResSelBefore) {
            sysLogPrintf(LOG_WARNING, "wsresetprobe: __Resolution changed (%d -> %d)", probeResSelBefore, s_resSel);
            bad++;
        }
        sysLogPrintf(LOG_INFO, "wsresetprobe: section '%s' verified, failures=%d", rows[hdr].label, bad);
    }
    if (probeOtherFolder >= 0) {
        save_data *s = fileGetSaveForFoldernum((u32)probeOtherFolder);
        if (!s || memcmp(&probeOtherBytes, s, sizeof(save_data)) != 0) {
            sysLogPrintf(LOG_WARNING, "wsresetprobe: file %d bytes CHANGED (scope isolation broken)", probeOtherFolder + 1);
        } else {
            sysLogPrintf(LOG_INFO, "wsresetprobe: file %d unchanged (scope isolation)", probeOtherFolder + 1);
        }
    } else {
        sysLogPrintf(LOG_INFO, "wsresetprobe: no second folder to compare");
    }
}

/* In stage (tick 2): arm + confirm every section. The file rows enter the
 * D352 command queue (applied + persisted by the game thread at the next
 * drain); log the queue depth so the dispatch is visible. */
void optionsResetProbeDispatchStage(void)
{
    if (probeSectionN == 0) {
        for (int i = 0; i < NUM_ROWS; i++)
            if (isResetRow(&rows[i]) && probeSectionN < PROBE_MAX_SECTIONS)
                probeSectionIdxs[probeSectionN++] = i;
    }
    probeResSelBefore = s_resSel;
    for (int s = 0; s < probeSectionN; s++) {
        optionsRowActivateReset(probeSectionIdxs[s]);   /* arm */
        optionsRowActivateReset(probeSectionIdxs[s]);   /* confirm */
    }
    int q = watchSettingsQueueCount();
    if (q < 6) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: stage dispatch: queue depth %d, want >= 6 (4 GAMEPLAY + 2 AUDIO watch rows)", q);
    } else {
        sysLogPrintf(LOG_INFO, "wsresetprobe: stage dispatch: %d command(s) queued (game thread drains next tick)", q);
    }
}

/* In stage (tick 3, after the drain + persist): verify every section. */
void optionsResetProbeVerifyStage(void)
{
    for (int s = 0; s < probeSectionN; s++) {
        int iReset = probeSectionIdxs[s];
        int hdr, end;
        probeSectionRange(iReset, &hdr, &end);
        int bad = probeVerifySection(iReset);
        if (s_resSel != probeResSelBefore) {
            sysLogPrintf(LOG_WARNING, "wsresetprobe: __Resolution changed (%d -> %d)", probeResSelBefore, s_resSel);
            bad++;
        }
        sysLogPrintf(LOG_INFO, "wsresetprobe: stage: section '%s' verified, failures=%d", rows[hdr].label, bad);
    }
    /* The persisted bytes must hold the BLANKSAVEDATA raw values too. */
    save_data *save = fileGetSaveForFoldernum(selected_folder_num);
    if (!save) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: stage: no save for folder %d", (int)selected_folder_num);
    } else if (save->music_vol != 0xFF || save->sfx_vol != 0xFF) {
        sysLogPrintf(LOG_WARNING, "wsresetprobe: stage: saved music=%d fx=%d, want 0xFF/0xFF",
                     (int)save->music_vol, (int)save->sfx_vol);
    } else {
        sysLogPrintf(LOG_INFO, "wsresetprobe: stage: saved bytes OK (music/sfx = 0xFF)");
    }
}
