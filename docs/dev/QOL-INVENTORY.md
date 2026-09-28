# QoL Inventory — PD-vs-GE options diff

Status: **partially implemented.** M-83 landed three port-only quick wins —
DisplayFPS (D213), anisotropic filtering (D212), and the FOV slider (D211, via
`WIDESCREEN-FOV-PLAN.md` Phase 4). Rebinding / SkipIntro / per-pad tuning /
window HiDpi still open. Companion to `WIDESCREEN-FOV-PLAN.md` and
`UNLOCKED-FPS-PLAN.md`.

Method: diffed the config surface of both ports — every `configRegister*`
key in `pd_port/port/src/*.c` vs `port/src/config.c` + the F10 overlay
(`port/src/optionsoverlay.c`) + PD's `optionsmenu.c`. Principle applied:
**where PD has a QoL feature that is achievable here, it is the standard;
GE keeps its own extras.**

Classification key — **P** = port-only (no rules issues), **G** = needs a
game-code exception (rule #2 sign-off, per `WIDESCREEN-FOV-PLAN.md` framing).

## GE already has (baseline — keep)

Input: mouse enable/capture/aim+turn speed/invert Y/Y scale/smoothing/raw
input, aim band, hipfire pitch speed, menu pointer speed + mode (GE's menu
cursor is injected entirely in `port/src/input.c` — no game code), pad
deadzone/trigger %. Video: VSync, FpsCap, MSAA, TextureFilter,
FixMipTextures, WrapFix, Fullscreen, Window geometry. Game:
ScreenShakeIntensity. Debug: FrameDump, InputLog.

## PD has, GE lacks (candidates)

| Feature | PD location | Class | Effort | Notes / priority |
|---|---|---|---|---|
| **Key rebinding** _(DONE M-83, D214)_ | `port/src/input.c` `[Input.Bind]` section | P | M | Keyboard only, 12 actions, `Input.Bind.* = Key,Key` (SDL scancode names), file-driven like PD. Defaults = prior FPS layout (verified). Per-controller pad rebinding + a UI still open. |
| **SkipIntro** _(DONE M-83, D216)_ | `Game.SkipIntro` -> `src/game/lv.c` | P | S | Boots to the SELECT FILE menu, skipping the legal screen + Nintendo/Rare/GoldenEye logo attract loop. Reuses the game's own post-intro route (sets `is_first_time_on_main_menu=FALSE` + `menu_update=MENU_FILE_SELECT`); one `#ifdef PORT` line in the existing GE_STARTMENU block. Verified headless (screenshots). |
| **DisplayFPS** _(DONE M-83, D213)_ | config `Video.DisplayFPS` | P | S | Top-right readout, config-only; ~0.5 s sample window. |
| **Anisotropic filtering** _(DONE M-83, D212)_ | `Video.Anisotropy` (1–16, dflt 4) | P | S | fast3d already had the GL hook + a hardcoded 4×; M-83 exposed it as config. MipmapFilter / TextureFilter2D still not surfaced. |
| Per-pad tuning: per-stick deadzones, stick scale, rumble scale, device index, swap sticks, C-button mapping | `input.c` padsCfg block | P | M | Medium. Matters for real-controller users; PD pattern is a config section per pad. **M-87 survey addendum:** PD does *incremental* hot-plug (`SDL_CONTROLLERDEVICEADDED/REMOVED` opens/closes individual pads into the first free slot, remembers `deviceIndex=-1` on removal so a replugged pad returns to its configured seat, `input.c:473-491`); GE's port instead does a full rescan-close-and-reopen-everything (`inputRescanPads()`, `input.c:983-1000`) and the game itself latches the controller mask at boot, so a pad added later merges into controller 0 for play. PD's incremental-with-remembered-seat approach is the natural implementation shape for this row and sidesteps the boot-latch merge — fold into the design when this row gets built, not a separate item. |
| **Rumble** _(DONE D401 — D224 / M-87, re-promoted 2026-09-27)_ | `port/src/input.c` + `port/src/libultra.c` + `port/src/optionsoverlay.c` | P | M | D401 implements the PD precedent per `docs/dev/RUMBLE-PLAN.md`: `inputRumbleSupported()`/`inputRumble()` → `SDL_GameControllerRumble()` when the pad reports haptics (SDL ≥ 2.0.18 `HasRumble`, else `SDL_JoystickIsHaptic` + known-type Windows fallback), per-pad haptics detection (`SDL_GameControllerHasRumble` / `SDL_JoystickIsHaptic` + known-type Windows fallback), a single global `Input.RumbleScale` (0..1, dflt 0.5) config + one `Rumble strength` slider row; `libultra.c` sets `CONT_CARD_ON` in `g_contStatus` for rumble-capable pads and routes `osPfsInit` → `PFS_ERR_DEVICE`, `osMotorInit/Start/Stop` → the haptics API — the two M-87 addendum blockers that kept `joyRumblePakInit` from arming. Zero `src/` edits. **Real-pad haptics confirmed working by the user (2026-09-28).** Plan item 3 landed the same day: the original four per-player rows were replaced (user decision) with one global `Rumble strength` slider in the F10 overlay + front options screen (`optionsoverlay.c`, shared `rows[]`), 0.05 detents, `kResetDefaults` 0.5; the four `Input.PlayerN.RumbleScale` ini keys were dropped (stale keys self-clean on the next save). A disabled-when-unsupported row state is deferred with the per-pad tuning row (needs a generic row-disabled mechanism). Unverified: `RumbleScale=0` silence check (trivial early-out). |
| FakeGamepads / FirstGamepadNum / UseHIDAPI | `input.c` | P | S–M | Niche (local MP on PC); defer until MP is actually played. |
| Window polish: DefaultFullscreen/Maximize, CenterWindow, AllowHiDpi, ExclusiveFullscreen | `video.c` vid* block | P | S | HiDpi + center are the useful ones on modern Windows. |
| VSync adaptive (−1..10) | `Video.VSync` range | P | S | Note for FPS plan — GE's 0/1 toggle is fine until then. |
| **CenterHUD** (0/1/2 = left/center/right) | `Game.CenterHUD` → `g_HudAlignModeL/R` | G* | — | Already scoped in `WIDESCREEN-FOV-PLAN.md` Option B (Phase 1 decision). Not a separate item. |
| Per-player FovY + FovAffectsZoom | `Game.Player%d.FovY`, `g_PlayerExtCfg` | G | M | PD does this in game code. GE's port-only RSP FOV slider (widescreen plan Phase 4) covers the global case; per-player is a stretch goal needing the exception class. |
| MemorySize (4–2048 MB emulated RAM) | `Game.MemorySize` → `g_OsMemSizeMb` | P? | S | Check how GE sizes its OS memory heap; if fixed, make it a config knob (stability lever for heavy levels). |
| MaxExplosions / GEMuzzleFlashes / DisableMpDeathMusic | PD decomp edits | G | S | Low priority; rule-#2 cost > benefit. Skip unless asked. |
| GlareBrightness / OverexposureScale / FramebufferEffects | PD post-FX | — | — | Not applicable: GE's fast3d has no framebuffer-effect path. Excluded. |
| **HUD scale → D226 (M-87, new).** Ammo counter + bottom-left pickup/status text + top-of-screen dialogue all funnel through one function, `textRenderOutlined()` (`textrelated.c:688`), from two `bondview2.c` call sites — plain RDP texture-rectangles at native glyph size, no scale factor anywhere. Menus/options/MP UI share the same function but must NOT be scaled by this (different constraint, own layout). | P | M | User-requested. No PD precedent. Scale-about-anchor (bottom stays bottom, top stays top), matching D211's HUD-static principle. F10 slider candidate alongside FovScale. |
| **F10 overlay: mouse-wheel scroll + more settings + category grouping → D237 (M-106, new).** The F10 options overlay (`port/src/optionsoverlay.c`) works but has no wheel-scroll navigation and only a minimal row set. User ask: (1) scroll with the mouse wheel; (2) expose more settings that can easily be surfaced — explicitly `Game.NoHitFlash` (D232, screen-flash setting), plus other cheap candidates from this inventory's baseline list (e.g. `Video.Anisotropy`, `Video.TextureFilter`, `Input.MouseYScale`, `Input.MouseSmoothing`, `Input.AimBand`); (3) organise rows by category — mouse settings together, video together, etc. (group headers over the existing flat registry walk). | P | M | User-requested. No PD precedent needed — pure port-layer overlay work on top of the existing `config.c` registry; wheel event already reaches the port (`inputPostWheel`, D223). Cross-ref `OPTIONS-MENU-PLAN.md` §3. |
| **Mouse sensitivity rework → D238 (M-106, new).** User wants normal PC-shooter mouse settings: (1) a *separate* RMB aim-mode sensitivity; (2) ideally ONE single master sensitivity driving both turn + aim, **linked by default**, with a future option to set them independently (today `Input.MouseAimSpeed` dflt 16 and `Input.MouseTurnSpeed` dflt 100 are two unrelated keys, `input.c:188-190`); (3) up/down should feel mostly the same as left/right — GEPD Mouse Injector cited as the good-feel reference. Implementation shape: one master `Input.MouseSensitivity` feeding both paths with a per-mode scale factor defaulting to equal, plus independent override keys that decouple only when set; audit the Y-axis gain asymmetry (`AIM_GAIN` vs `MOUSE_TURN_GAIN`, `MouseYScale`, smoothing) so vertical tracks horizontal. Lump with the overall mouse/input fixes (D194/D165/D166 lineage; the SOLITAIRE natural-pitch scheme switch noted on D166 may subsume part of this). | P | M | User-requested. No PD precedent — GE-specific feel work in `port/src/input.c` + `config.c`. |
| **Click-to-lock / always-grab parity → D239 (M-106, new).** User: click-to-lock should be the default (it already is — `mouseCaptureMode = 1`, `input.c:185`); "always grab" feels bad on the file-select menu and is not 1:1 with click-to-lock; if parity is too complicated, **drop always-grab and remove the setting**. Re-confirms the M-82 decision to rip out `MouseCaptureMode=0` (`docs/BACKLOG.md` "M-82 playtest feedback"; D192 documents mode 0's unreachable-outer-cells defect). Recommendation: take the fallback — delete mode 0 + the config key, keep click-to-lock only. | P | S | User-requested; closes D192 by deletion rather than fix. |
| **Debug unlock-all → D225 (M-87, new, dev-tooling not player QoL).** One env-gated `#ifdef PORT` short-circuit at `fileGetIsCheatUnlocked()` (`src/game/file2.c:391`) unlocks **both** all-mission-select **and** the full cheat menu in one change — the two gates already share that single function. Bonus: exposes leftover Rare dev cheats (`CHEAT_LINEMODE` wireframe, `CHEAT_BONDPHASE`, `CHEAT_DEBUG_POS/UNK5`) that are otherwise unreachable. | P | S | User-requested testing convenience ("always enable all level access + all cheats... goes a long way"). Default off, pure query-time override, zero save-data/gameplay-logic touch. |

## GE has, PD lacks (keep as-is)

`Video.FixMipTextures`, `Video.WrapFix`, `Input.AimBand`,
`Input.HipfirePitchSpeed`, `Input.MouseSmoothing`, `Debug.FrameDump`,
`Debug.InputLog`. No action.

## Suggested Phase 4 order (after audio; ordering vs widescreen/FPS TBD)

1. **Quick wins (all P, small):** SkipIntro, DisplayFPS, aniso + mipmap
   filters, window HiDpi/center.
2. **Rebinding** (P, medium) — the headline Phase 4 item; PD pattern is
   config-string driven, no UI required for parity.
3. **Per-pad tuning** (P, medium).
4. Niche: fake gamepads / HIDAPI (when MP gets played); MemorySize knob
   (after the heap check).

Everything here is parked behind audio like the other plans; nothing in this
inventory implies an implementation decision yet.
