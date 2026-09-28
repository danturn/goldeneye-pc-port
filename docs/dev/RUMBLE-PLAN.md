# Rumble Pak → real gamepad haptics (port plan)

Status: **IMPLEMENTED — finding D401 (2026-09-27/28).** Items 1+2+3 all
landed (port layer only; zero `src/` edits), re-promoting the
DEPRIORITIZED finding **D224** (M-87) to DONE. **Real-pad haptics confirmed
working by the user (2026-09-28).** Menu shape: **one global `Rumble
strength` slider** (`Input.RumbleScale`, user decision 2026-09-28 — the
four per-player rows were dropped). Only the `RumbleScale=0` silence check
remains unverified (trivial early-out path).

Scope: wire the N64 Rumble Pak path (already present in `src/`) through to
`SDL_GameControllerRumble()` on the PC. **Port-layer only — zero `src/game` or
`src/` logic edits**, so rule 2 / ABI-exception do not apply.

Reference: the PD PC port (local checkout `../pd_port`, `port/src/input.c`
+ `port/src/libultra.c`) implements the identical feature for the same Rare
engine family. Copy its shape; the only friction is that **GE's `src/joy.c`
calls the classic libultra names** (`osMotorInit/Start/Stop`, `osPfsInit/
osPfsIsPlug`) while PD's engine renamed them (`osMotorProbe`,
`__osMotorAccess`, `osPfsInitPak`). We translate names, not logic.

---

## Ground truth (current state)

Game side — **already correct, untouched:**
- `src/joy.c` full Rumble-Pak state machine: `joyRumblePakInit` (:178),
  `joyRumblePakTick` (:298), `joyRumblePakStart`/`Stop`. Call sites (verified
  2026-09-27 for D401) live in `gunfire.c:3284` (start) / `:3288` (stop),
  `bondview2.c:9722` (start) / `:9726` (stop, incl. 2P second pad),
  `lv.c:1688` (stop at level start), plus `sched.c:272` (stop on level
  teardown).
- `src/motor.c` is **EXCLUDED** from the PC build (N64 SI-DMA driver), replaced
  by no-op stubs. CMakeLists.txt:525 documents this.

Port side — **all no-oped today, in `port/src/libultra.c`:**
- `osPfsInit`        (:1413) → `return PFS_ERR_NOPACK;`
- `osPfsIsPlug`      (:1415) → `*pattern = 0; return 0;`
- `osMotorInit`      (:1417) → `return -1;`
- `osMotorStart`     (:1419) → `return -1;`
- `osMotorStop`      (:1420) → `return -1;`
- `g_contStatus[i].status = 0;` (:1102) — `CONT_CARD_ON` **never set**.

Gamepad side — **exists, no change needed:**
- `port/src/input.c` owns `static SDL_GameController *pads[MAX_PADS]`
  (MAX_PADS = 4, aligned with `MAXCONTROLLERS`), open/close, hotplug,
  `inputComputePad()`. This is the exact object `SDL_GameControllerRumble()`
  needs.

## The two upstream blockers (D224 M-87 addendum — MUST land together)

`joyRumblePakInit` (src/joy.c:178) only drives the motor if BOTH pass:
1. `g_ContStatus[index].status & CONT_CARD_ON`  → today always 0, so init
   never even runs.
2. `osPfsInit(...)` returns `PFS_ERR_ID_FATAL` **or** `PFS_ERR_DEVICE`  →
   today returns `PFS_ERR_NOPACK`, so `osMotorInit` is never attempted.

If the SDL wiring is done without (1)+(2), the state machine parks at
`RUMBLEPAKINITSTATE_NOT_READY` forever and nothing rumbles despite "looking
wired." Both fixes are in the same file we already touch, so no extra cost.

---

## Work items

### 1. `port/src/input.c` — detection + API

Add per-pad fields (GE uses flat statics, not PD's `padsCfg[]` array):
```c
static int   padRumbleOn[MAX_PADS];    // set where pads[i] is opened (line ~582)
static float padRumbleScale[MAX_PADS] = { 0.5f, 0.5f, 0.5f, 0.5f };
```
On pad open (in the `SDL_GameControllerOpen` path, line ~575-589), mirror PD:
```c
#if SDL_VERSION_ATLEAST(2, 0, 18)
    padRumbleOn[i] = SDL_GameControllerHasRumble(pads[i]);
#else
    padRumbleOn[i] = SDL_JoystickIsHaptic(SDL_GameControllerGetJoystick(pads[i]));
    if (!padRumbleOn[i]) {
        const SDL_GameControllerType t = SDL_GameControllerGetType(pads[i]);
        padRumbleOn[i] = t && (t != SDL_CONTROLLER_TYPE_VIRTUAL);  // Win no-haptic fallback
    }
#endif
```
On pad close, `padRumbleOn[i] = 0;`. GE has no `inputCloseController`
(PD-only) — the close sites are `inputDestroy()` and the close loop in
`inputRescanPads()`, both of which zero it (`inputOpenPads` re-detects on
open anyway).

New public fns (declare in `port/include/input.h`, define in input.c):
```c
int  inputRumbleSupported(int idx);            // 0<=idx<MAX_PADS ? padRumbleOn[idx] : 0
void inputRumble(int idx, float strength, float time);   // scaled SDL_GameControllerRumble
float inputRumbleGetScale(int idx);
void  inputRumbleSetScale(int idx, float v);
```
`inputRumble` body (from PD, verbatim logic):
```c
if (idx < 0 || idx >= MAX_PADS || !pads[idx]) return;
if (padRumbleScale[idx] <= 0.f) return;
if (padRumbleOn[idx]) {
    strength *= padRumbleScale[idx];
    if (strength <= 0.f) { strength = 0; time = 0; }
    else { strength *= 65535.f; time *= 1000.f; }
    SDL_GameControllerRumble(pads[idx], (Uint16)strength, (Uint16)strength, (Uint32)time);
}
```
In init, add the two PS4/5 hints (PD input.c:696-697) so BT pads report
haptics:
```c
SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
```
Register per-pad config (in the existing `configRegister*` block, ~line 2457+):
```c
char key[48];
for (int c = 0; c < MAX_PADS; ++c) {
    sprintf(key, "Input.Player%d.RumbleScale", c + 1);
    configRegisterFloat(key, &padRumbleScale[c], 0.f, 1.f);
}
```
Note: **`strFmt` is NOT available in `input.c`** (0 uses, no decl in
`port/include`) — use a local `sprintf` buffer as above. `configRegisterFloat`
is declared in `port/include/config.h` (already included by input.c:80); it's
used elsewhere (video.c) so the symbol is already in the link set.

### 2. `port/src/libultra.c` — rewire the shims

`contSnapshotFromKeyboard` (:1096): set the status bit for connected,
rumble-capable pads (need a per-channel capability check → call
`inputRumbleSupported(i)`; it's declared in input.h so include it):
```c
g_contStatus[i].type   = connected ? CONT_TYPE_NORMAL : 0;
g_contStatus[i].status = (connected && inputRumbleSupported(i)) ? CONT_CARD_ON : 0;
```
Replace the 5 accessory stubs (:1413-1420) with real routing (PD libultra.c:235-351):
```c
s32 osPfsInit(OSMesgQueue *q, OSPfs *pfs, int channel) {
    (void)q;
    // "pretend the Rumble Pak is present" when the real pad can rumble:
    // returning PFS_ERR_DEVICE is what joyRumblePakInit (src/joy.c:186-190)
    // needs to proceed to osMotorInit.
    return inputRumbleSupported(channel) ? PFS_ERR_DEVICE : PFS_ERR_NOPACK;
}
s32 osPfsIsPlug(OSMesgQueue *q, u8 *pattern) {
    (void)q;
    if (pattern) {
        *pattern = 0;
        for (int i = 0; i < MAXCONTROLLERS; ++i)
            if (inputRumbleSupported(i)) *pattern |= (u8)(1 << i);
    }
    return 0;
}
s32 osMotorInit(OSMesgQueue *mq, OSPfs *pfs, int channel) {
    if (pfs && inputRumbleSupported(channel)) {
        pfs->queue = mq;
        pfs->channel = channel;
        pfs->activebank = 0xff;   // PD's osMotorProbe stores this too
        // NOTE: PFS_MOTOR_INITIALIZED is NOT defined in GE's headers, and
        // joy.c never reads pfs->status — it only checks (osMotorInit()==0)
        // to set RUMBLEPAKINITSTATE_READY (src/joy.c:190). Skip the status store.
        return 0;
    }
    (void)mq; (void)channel;
    return PFS_ERR_NOPACK;
}
s32 osMotorStart(OSPfs *pfs) {
    if (!pfs) return PFS_ERR_NOPACK;
    // N64 motor has no duration; the game's joyRumblePakTimer60 (joy.c) turns it
    // off. Issue one generous window, PD-style ("hope the timer stops it"):
    inputRumble(pfs->channel, 1.0f, 5.0f);
    return 0;
}
s32 osMotorStop(OSPfs *pfs) {
    if (!pfs) return PFS_ERR_NOPACK;
    inputRumble(pfs->channel, 0.0f, 0.0f);   // zero-duration / zero-strength = stop
    return 0;
}
```
**JPN note (2026-09-27):** `_HW_VERSION_1` defines `MAXCONTROLLERS` as 6
while `MAX_PADS` is 4 — the `MAXCONTROLLERS`-bounded loops above are safe
because `inputRumbleSupported()` bounds-checks against `MAX_PADS` and reports
unsupported (silent) for `i >= 4`. `osPfsIsPlug` has **no GE game-side call
sites** — its rewiring is completeness/hygiene only; the load-bearing stubs
are `osPfsInit`/`osMotor*`.

Headers confirmed: `OSPfs` (`include/PR/os.h:282`) has `queue` + `channel`; the
error macros `PFS_ERR_DEVICE` (:642) and `PFS_ERR_ID_FATAL` (:641) and
`PFS_ERR_NOPACK` (:631) are all defined there. **`PFS_MOTOR_INITIALIZED` is NOT
defined** anywhere in GE's headers and `joy.c` never reads `pfs->status`, so it
is not needed. `input.h` is already included by libultra.c (line 50), so
`inputRumbleSupported` is directly callable there.

### 3. (DONE 2026-09-28) options-menu slider — single global

Landed in `port/src/optionsoverlay.c` (not PD's `optionsmenu.c` — GE's F10
overlay + front options screen share `rows[]`, walked by `frontoptions.c`
via the `optionsRow*` accessors): ONE `ROW_SLIDER` row `Rumble strength`
on `Input.RumbleScale` (0.05 detents, generic `CONFIG_OPT_FLOAT` display
`%.2f`), in the INPUT section after the controller rows, plus a
`kResetDefaults` entry (0.5, the `gRumbleScale` C initializer) so INPUT's
"Reset to defaults" covers it.

**Decision (user, 2026-09-28):** the original four-per-player-row design
(`Rumble (Player 1..4)` on `Input.Player%d.RumbleScale`) was coded and
reviewed, then replaced with the single global row **before its first
compile**: the scale applies to every rumble-capable pad, and the four
`Input.PlayerN.RumbleScale` ini keys are dropped — stale keys in an
existing ini log four harmless `unknown key` `LOG_NOTE` lines at config
load and self-clean on the next `configSave` (which rewrites the whole
file with the registered keys). Per-player rumble tuning stays a G-class
stretch goal (QOL-INVENTORY) — and a per-pad override-on-top-of-global
design would be a no-op for everyone anyway, because `configSave`
rewrites all registered keys and the "user explicitly set this?" signal
is burned as soon as any key exists at the 0.5 default. The
row-disabled-when-unsupported state is deferred with the per-pad tuning
row (the overlay has no generic row-disabled mechanism); the row is inert
(no-op) on non-rumble setups.

**Original plan (for reference):** PD wires the per-pad scale into the
options menu (`optionsmenu.c:533-540`): `inputRumbleGetScale(player)`,
`inputRumbleSetScale(player, v/10)`, disabled when
`!inputRumbleSupported(player)`. In GE this maps onto
`port/src/optionsoverlay.c` / the per-pad tuning row (QOL-INVENTORY.md).
Config file value alone is enough for the feature; the UI is the polish.

---

## Verification ritual (AGENTS.md)

- [x] `./build-pc.sh ntsc-final` compiles clean (2026-09-27). New
      `inputRumble*` defined once in input.c, declared in input.h; no new
      undefined/duplicate symbols (full `/linkcheck` sweep pending at D401
      close).
- [x] `CONT_CARD_ON` + `PFS_ERR_DEVICE` land (the two-blocker addendum);
      the state machine's READY path is provable by code read — the runtime
      proof is folded into the real-pad test below.
- [x] **By-hand, real pad:** user confirmed the rumble works on a real
      controller (2026-09-28) — fire a weapon (`gunfire.c:3284` recoil),
      explosion rumble, 2P second pad (`bondview2.c:9722/9726`). Non-rumble
      pad and mouse/keyboard-only stay silent by construction (`pads[idx]`
      NULL / `padRumbleOn` 0 ⇒ `inputRumble` no-op).
- [ ] `Input.RumbleScale=0` fully silences (unverified — the trivial
      `gRumbleScale <= 0` early-out in `inputRumble`); max value loud
      (confirmed implicitly by the working playtest at scale 0.5).

## Finding log

Done (2026-09-27): **D401** added to `docs/dev/findings.md` (§F index +
full entry) documenting the D224 implementation + the two-blocker
resolution; the `QOL-INVENTORY.md` M-87/D224 row re-promoted
DEPRIORITIZED → DONE (D401); `docs/dev/findings-index.csv` regenerated.

---

## Effort

- Items 1+2: **~1–2 h** coding (mostly a rename + copy from PD) + build/linkcheck.
- Hands-on pad testing: **~30 min–1 h** (the only genuinely open-ended part —
  feels-right tuning).
- Item 3 (UI slider): **~1 h**, landed 2026-09-28 as the single global
  `Rumble strength` row.

**Total for working core: half a day max, <2 h of pure code.**
