#ifndef _HUDASPECT_H_
#define _HUDASPECT_H_

/* D335 (WIDESCREEN-FOV-PLAN Option B, Phase 3): HUD alignment under native
 * widescreen (D334). The world is projected at the window aspect, but 2D/HUD
 * elements are laid out in the game's 4:3 logical canvas, which fast3d
 * stretches to the window. Wrapping a HUD draw in an aspect mode makes fast3d
 * map its x undistorted (x scaled by (4/3)/window_aspect) and anchor it to
 * the left edge, the right edge, or the centre -- the PD port's
 * G_ASPECT_{LEFT,RIGHT,CENTER}_EXT extra-geometry-mode, which GE's fast3d
 * already decodes (gfx_pc.cpp gfx_sp_extra_geometry_mode /
 * gfx_update_aspect_mode). Game-includable: no SDL, no fast3d headers.
 *
 * PORT_HUD_ASPECT emits only while native widescreen is on and the window is
 * wider than 4:3; otherwise the display list is byte-identical to before. */

#define GE_HUD_ASPECT_NONE     0x00u
#define GE_HUD_ASPECT_LEFT     0x10u   /* G_ASPECT_LEFT_EXT   (port/fast3d/gbiex.h) */
#define GE_HUD_ASPECT_RIGHT    0x20u   /* G_ASPECT_RIGHT_EXT  */
#define GE_HUD_ASPECT_CENTER   0x30u   /* G_ASPECT_CENTER_EXT */
#define GE_HUD_ASPECT_MASK     0x70u   /* G_ASPECT_MODE_EXT   */
#define GE_EXTRAGEOMETRYMODE_EXT 0x3a  /* G_EXTRAGEOMETRYMODE_EXT */

/* The decoder does extra_mode = (extra_mode & ~clear) | set with
 * clear = ~(w0 & 0xFFFFFF): passing ~MASK as the low 24 bits clears exactly
 * the aspect bits and leaves the other extra-geometry flags alone. */
#define gSPHudAspectEXT(pkt, mode) \
    gDma0p((pkt), GE_EXTRAGEOMETRYMODE_EXT, (mode), ((~GE_HUD_ASPECT_MASK) & 0xFFFFFFu))

extern float portNativeAspect(void);   /* port/src/video.c */

/* Only for windows wider than 4:3: in a narrower window a centred 4:3 HUD
 * would be wider than the window and crop, so there the HUD keeps the canvas
 * mapping (squeezed, never cropped). 1.3334 so exact 4:3 emits nothing. */
#define PORT_HUD_ASPECT(gdl, mode) \
    do { if (portNativeAspect() > 1.3334f) { gSPHudAspectEXT((gdl)++, (mode)); } } while (0)

/* D226: HUD span scale. PORT_HUD_SCALE(gdl, ax, ay) scales every rectangle
 * drawn after it (glyphs, message boxes, ammo icons) about the anchor
 * (logical screen px) by Game.HudScale %, until PORT_HUD_SCALE_END. At 100%
 * nothing is emitted (byte-identical). Decoded as G_HUDSCALE_EXT in fast3d. */
#define GE_HUDSCALE_EXT 0x46
extern s32 portHudScalePercent(void);   /* port/src/video.c */
#define gSPHudScaleEXT(pkt, scale256, ax4, ay4)     gDma0p((pkt), GE_HUDSCALE_EXT, ((u32)(ax4) << 16) | ((u32)(ay4) & 0xFFFFu), ((u32)(scale256) & 0xFFFFu))
#define PORT_HUD_SCALE(gdl, ax, ay)     do { s32 _hp = portHudScalePercent();          if (_hp != 100) { gSPHudScaleEXT((gdl)++, _hp * 256 / 100, (ax) * 4, (ay) * 4); } } while (0)
#define PORT_HUD_SCALE_PCT(gdl, pct, ax, ay)     do { s32 _hp = (pct); if (_hp != 100) { gSPHudScaleEXT((gdl)++, _hp * 256 / 100, (ax) * 4, (ay) * 4); } } while (0)
#define PORT_HUD_SCALE_END(gdl)     do { if (portHudScalePercent() != 100) { gSPHudScaleEXT((gdl)++, 256, 0, 0); } } while (0)

#endif
