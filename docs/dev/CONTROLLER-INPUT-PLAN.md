# Controller & input options wave (port plan)

Status: **PROPOSED (v0.5.0 candidate)** — the "low-hanging fruit" from the
Nightdive *Turok* PC-port options menu that is cheap, well-scoped, and
**port-layer only** (zero `src/` / `src/game` edits → rule 2 and the ABI
exception do not apply). Sequenced after v0.4.0 ships. This is the *input*
wave; the sibling *video/audio/graphics* Tier-A items (borderless window,
primary display, brightness, master volume, button glyphs, uncapped-FPS) are a
separate follow-on wave — see §Out of scope.

Reference: `docs/dev/RUMBLE-PLAN.md` (D401) is the closest precedent — a
controller option that lives entirely in `port/src/input.c` +
`port/src/optionsoverlay.c` + a `configRegister*` key + a `kResetDefaults`
entry, with the game side (`src/joy.c`, `src/` stick consumers) untouched.
`docs/dev/GEPD-INPUT-PLAN.md` documents the pad poll path this wave extends.

---

## Ground truth (current state, verified 2026-09-27)

**Pad poll = `inputComputePad()`** (`port/src/input.c:1375`), runs on the game
thread (`osContStartReadData`). It reads the SDL axes/buttons for pad `idx`,
writes out a 16-bit button mask plus `*stick_x` / `*stick_y` (the N64
`signed char` stick, clamped to `STICK_MAX`). All the knobs in this wave are
applied **inside** `inputComputePad`, before the two `*stick_x`/`*stick_y`
writes at `:2063-2064` — i.e. the port re-shapes the *input the game already
consumes*, never the game's stick→turn math.

**Two pitch modes drive the look stick** (selected by `Input.NaturalPitch`,
the default-on "SOLITARE" mode, D194/D238):
- **Continuous (natural-pitch, the default):** the **right stick** is the
  continuous look axis — `input.c:1927-1933`:
  `rxs = scaleAxis(rx);` … `rys = -scaleAxis(ry); if (padLookInvertY)
  rys = -rys; sx = rxs; sy = rys;`. The left stick becomes digital-step
  movement. **This is the path the X/Y look-sensitivity + look-smoothing +
  right-stick deadzone knobs target.**
- **Digital (native, natural-pitch off):** the left stick is analog
  movement/turn and the right stick is digitized into the C-button look
  (`:1935-1947`, thresholds at `RSTICK_THRESHOLD`). Look here is the faithful
  N64 *digital* C-button turn — **not sens-scalable** (there is no analog look
  to scale), so the new look-sens/smoothing knobs are inert in this mode,
  exactly as `Input.AimMode`/`Input.AimRange` already are per mode.

**Existing pad knobs** (all `configRegister*` in `input.c`, rows in
`optionsoverlay.c` INPUT section): `Input.PadDeadzone` (single,
`padDeadzone = STICK_DEADZONE`, `:524`), `Input.PadTriggerPct` (`:525`),
`Input.PadLookInvertY` (`:526`), `Input.RumbleScale` (D401). `scaleAxis()`
applies the shared deadzone + `STICK_MAX` scale to a raw `int16` axis.

**Already covered elsewhere** (do not re-plan): mouse sens/invert/smoothing
(`Input.MouseSensitivity`/`MouseInvertY`/`MouseSmoothing`), **uncapped FPS**
(`docs/dev/UNLOCKED-FPS-PLAN.md` — parked; has a real perf prerequisite, D250),
**controller rebinding** (PR #109 / D383 — the hard, open gap, out of scope).

---

## Items (all port-layer)

### 1. `Input.PadLookSensX` — right-stick look **horizontal** sensitivity  *(S)*
New int key, default **100** (= 1×, current behaviour), range ~**25–200 %**.
Applied in the continuous-look branch only: after `rxs = scaleAxis(rx)`,
`rxs = clamp(rxs * padLookSensX / 100, ±STICK_MAX)` before `sx = rxs`
(`input.c:1928/1932`). Pure gain on the emitted look X. In digital mode: inert.
- Row: INPUT, "Look sensitivity (left/right)", slider, after "Rumble strength".
- Risk: none (monotonic gain; 100% = byte-identical to today). Deck-testable.

### 2. `Input.PadLookSensY` — right-stick look **vertical (pitch)** sensitivity  *(S)*
Same shape, applied to `rys` (`input.c:1930/1933`): `rys = clamp(rys *
padLookSensY / 100, ±STICK_MAX)`. Default **100**, range ~**25–200 %**.
Kept separate from X (Turok splits them; pitch and yaw feel different on a
stick). Independent of the existing `Input.HipfirePitchSpeed` (that one is the
hipfire/mouse pitch rate, a different path). In digital mode: inert.
- Row: INPUT, "Look sensitivity (up/down)", slider.
- Risk: none (same as item 1).

### 3. `Input.PadSouthpaw` — swap L/R triggers  *(S)*
New toggle, default **off**. Today (`input.c:1954-1958`): `TRIGGERRIGHT >
trigPt → GE_CONT_G`; `TRIGGERLEFT > trigPt → GE_CONT_R`. Under southpaw, swap
the two `GE_CONT_G`/`GE_CONT_R` assignments (left trigger becomes the
right-shoulder action and vice-versa). Two lines. Lets left-handed /
southpaw players put their index finger on the primary fire trigger.
- Row: INPUT, "Southpaw", toggle.
- Risk: none at default off; trivial to verify by re-aiming.

### 4. `Input.PadDeadzoneL` / `Input.PadDeadzoneR` — per-stick deadzone  *(S–M)*
Split the single `Input.PadDeadzone` into a **left (movement)** and a
**right (look)** deadzone. Thread the deadzone through `scaleAxis()` (it is
currently the global `padDeadzone`) as a per-call argument: left-stick calls
use `padDeadzoneL`, right-stick calls use `padDeadzoneR`.
- **Back-compat/migration:** on load, if `Input.PadDeadzoneL`/`R` are absent
  but legacy `Input.PadDeadzone` is set, seed both from it (one-time, like the
  `AimStyle`→`AimMode` migration at `input.c:449-454`). Keep `Input.PadDeadzone`
  registered so old inis keep loading; it becomes hidden/superseded (D337/`D333`
  precedent for a superseded key).
- Rows: INPUT, "Deadzone (left stick)" + "Deadzone (right stick)", sliders.
- Risk: low — a deadzone is a per-axis floor; splitting it cannot change
  behaviour when both equal the current single value. Watch the menu-navigation
  path (`:1918-1922`, left stick in menus) to use the **left** deadzone there.

### 5. `Input.PadLookSmoothing` — right-stick look low-pass  *(M)*
New key, default **0 (off)**, range 0–10 (0 = off = today). When >0, low-pass
filter the *emitted* right-stick look deflection so the look axis eases rather
than tracking the stick 1:1 — the pad analogue of `Input.MouseSmoothing`
(which smooths the *relative* mouse delta). Because the continuous stick is
**absolute** (not a delta), implement as a per-pad exponential moving average
on the emitted `rxs`/`rys` (state kept per `idx`, reset on pad hot-unplug and
on aim-mode exit so releasing the stick recentres cleanly). The higher the
value, the heavier the easing.
- Row: INPUT, "Look smoothing", slider (0 = off).
- Risk: **medium** — an EMA on an absolute stick can feel laggy or fail to
  recentre if the filter isn't driven back to 0 when the stick is centred;
  needs hands-on tuning on the Deck (the natural test device). Keep default off
  so it ships inert until tuned.

### 6. Mouse **vertical** sensitivity row  *(trivial — folds in)*
`Input.MouseYScale` already exists as a registered key; it just has no F10
row. Add one slider row (INPUT) so the asymmetric X/Y mouse sensitivity is
user-adjustable without editing the ini. No `input.c` change — row only.

---

## Shared plumbing (do once, not per item)
- **`configRegisterInt`/`Float`** for each new key in `input.c` next to the
  existing pad-key registrations (~`:2536-2538`).
- **Static state** next to `padDeadzone`/`padLookInvertY` (`:524-526`):
  `padLookSensX/Y`, `padSouthpaw`, `padDeadzoneL/R`, `padLookSmoothing` + the
  per-pad EMA state for item 5.
- **F10 rows** in `optionsoverlay.c` `rows[]` INPUT section (after
  "Rumble strength", with the other controller rows) + matching **front options**
  rows via the shared `optionsRow*` accessors (both UIs read `rows[]` — D401
  precedent, so one definition covers F10 *and* the front options screen).
- **`kResetDefaults`** entries for each new key (neutral: sens 100, smoothing
  0, southpaw 0, deadzones = `STICK_DEADZONE`) so INPUT "Reset to defaults"
  covers them (D401 item-3 precedent).

## Ground rules
1. **Port-layer only** — every edit is in `port/src/input.c`,
   `port/src/optionsoverlay.c`, `port/src/frontoptions.c`, and `CMakeLists.txt`
   (nothing new to compile — no new files). Zero `src/` / `src/game` edits →
   rule 2 and the pointer-width ABI exception are not engaged.
2. **Off-by-default = today's behaviour.** Every new key defaults to a neutral
   value that leaves the emitted stick/buttons byte-identical to the current
   build, so the golden gate (D117/`golden_gate.sh`) is unaffected at defaults
   and the v0.4.0 playtest baseline is preserved.
3. **Per-mode honesty.** The continuous-look knobs (1, 2, 5) apply only in
   natural-pitch mode; in digital mode they are inert — document this on the
   rows (mirrors how `Input.AimRange` is hidden in Centred mode, D338), rather
   than pretending they affect the digital C-button look.
4. **Deck is the reference device** for the pad wave (native Steam Controller,
   the real-world target); Windows + a plugged gamepad as the second check.

## Verification
1. `./build-pc.sh ntsc-final` clean (no new symbols/dupes — the `configRegister*`
   + `kResetDefaults` additions are the only new references).
2. `/linkcheck` + the golden gate (`tools_pc/golden_gate.sh`) — must pass
   **unchanged at defaults** (proves the neutral-default invariant).
3. Hands-on, on a pad / the Steam Deck: each knob at a non-neutral value
   changes feel as expected and at default leaves it identical; southpaw
   re-maps the triggers; per-stick deadzones floor the correct stick; look
   smoothing eases and **recentres on stick release** (item 5's known risk).
4. New finding (D4xx, next free label) recording the wave + per-item
   Deck-test result, cross-ref'd to D401 (precedent), D337/D338 (per-mode
   gating pattern), D333 (`AimStyle` migration pattern), PR #109 (out-of-scope
   rebind).

## Out of scope for this wave (see the assessment above)
- **Sibling Tier-A, other sections (separate follow-on wave):** borderless
  windowed, primary-display selection, brightness slider, master audio volume,
  button-prompt glyphs.
- **Tier B (medium, contained):** radial fog toggle, HUD opacity, flash-
  intensity slider (promote `Game.NoHitFlash`), show-HUD master toggle, audio
  output device, low-pass filter, SMAA/FXAA.
- **Uncapped FPS** — `docs/dev/UNLOCKED-FPS-PLAN.md` (parked; D250 perf
  prerequisite means it is not quick).
- **Tier C (rule-2 / big-feature / out-of-scope):** head-bobbing sliders,
  death-cinematic / switch-on-pickup / walk toggles (all `src/game` flow),
  bloom / light-scattering / water refraction-reflection / simple shadows,
  violence / blood colour, multiple graphics APIs, and **full controller
  rebinding** (PR #109 — the genuinely hard input item).

## Suggested sequencing (v0.5.0)
1. **Wave A (this doc):** items 1–4 + 6 (sens X/Y, southpaw, per-stick
   deadzone, mouse-Y row) — all low-risk, shippable behind neutral defaults.
2. **Item 5 (look smoothing)** lands in the same wave but may ship default-off
   pending Deck feel-tuning.
3. **Wave B:** the sibling video/audio/graphics Tier-A items.
4. Revisit Tier C only with explicit sign-off (rule-2 items need the
   `docs/dev-process.md` procedure).
