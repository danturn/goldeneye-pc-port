## GoldenEye 007 PC Port v0.4.0

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v0.3.0/docs/media/goldeneye-gh-preview.gif" width="480"
       alt="~12 s gameplay montage from live v0.3.0 play sessions (no audio track)">
</p>

> The full single-player campaign runs at a steady 60 fps with audio (music +
> SFX) playing throughout, on Windows and Linux including Steam Deck, and it
> is completable end to end: the whole campaign has been playtested through
> all 20 missions (Agent difficulty), on both Windows and Steam Deck with a
> physical controller. **This is a pre-1.0 release, not a finished
> product** — expect rough edges and missing features until v1.0; see the
> [README's Roadmap section](https://github.com/jkdansereau/goldeneye-pc-port#roadmap)
> for where this is headed. **v0.4.0 ships the NTSC-U (US) region only** —
> PAL and JP ROMs are on the post-release roadmap.

### What's new since v0.3.0

**Native widescreen**
- **The world now renders natively at your display's aspect ratio**
  (D334/D335): undistorted world geometry at any aspect (wider view, same
  vertical FOV), the HUD keeps its shape and anchors to the screen edges,
  and the 4:3 front-end menus are pillarboxed. The ending-credits sequence
  wraps widescreen too. The F10 `Native widescreen` toggle restores the old
  stretched frame if you prefer it.

**Aim, reworked for every input device**
- **Reticule jitter in aim mode is gone**: mouse aim now uses the Perfect
  Dark PC port's aim model (D332) on top of v0.3.0's direct-look rework.
- **New `Aim style` option** (D333/D337): N64 (the crosshair deflects and
  the camera follows at the edge; internally it runs on the PD aim model
  above) or Centred (crosshair fixed at screen center, input moves the
  camera directly). In v0.3.0 the centred mode only worked with a mouse;
  **v0.4.0 makes it work with a controller too**, with full-stick speed
  matching the N64 original's top turn rate (D404).
- **New `Aim range` option** (D338): PC (full-screen feel, the default) or
  N64 (original 65% stick limits). A single mouse-sensitivity control with a
  wider range remains from v0.3.0.

**A modern, regrouped options experience**
- Both options screens were reorganized into functional sections (video/
  graphics, sound, bond file) with per-section reset-to-defaults, and the
  confusing "save file" wording is now "profile" — settings are scoped per
  profile (D353–D356).
- The **F10 in-game overlay** got a GE-style refresh: category headers,
  controller value-adjust with hold-to-repeat, pointer/keyboard navigation
  fixes, plus five new settings — Skip Intro, Show FPS, and pad
  invert/deadzone/trigger rows (D237/D345–D347/D360–D370).
- The front end gained a **PC Options screen** next to the file-select bar,
  and file-select Copy/Erase moves into the bottom bar (D343).
- Options that only existed in the in-level watch (auto-aim, look-ahead and
  friends) are now editable from the PC options — and they *stick*: the
  "auto-aim won't stay on" report (issue #103) is fixed (D330/D350/D352/
  D354).
- **New `HUD scale` option** (75–150%): scale the ammo counter, pickup
  text, and subtitle bars for smaller/larger displays (D226).

**Crosshair, your way (opt-in)**
- On/off, colour, size and style rows were added to the options. All default
  to the untouched N64 crosshair — nothing changes unless you ask
  (D373/D379/D381/D382).

**Key rebinding + new default layout**
- A two-slot key editor (PC layout + in-level layout) lives in both options
  screens, alongside a **GEPD-style preset layout that is the new default**
  and a crouch/reload binding set. INI-file bindings still work
  (D371/D374/D383/D385).

**Controllers**
- The default pad layout now matches how the game is actually played:
  **A uses / interacts, X reloads, Y cycles weapons** (previously two
  buttons both crouched and there was no in-game use button) (D393);
  crouch itself now uses the engine's real crouch input (D375); Xbox-
  variant controller parity (D394); pad invert/deadzone/trigger rows
  (D237); and the F10 overlay is fully gamepad-driven with value-adjust and
  hold-to-repeat (D347/D395/D396).
- **The N64 Rumble Pak now drives real gamepad haptics** on supported pads,
  with a `Rumble strength` slider (D401).

**`All unlocked`, now a documented EXPERIMENTAL feature**
- It works for its intended purpose (skip to any mission, 007 mode, full
  cheat menu) — including on a fresh install, which now yields the game's
  own fresh-slot layout instead of corrupting (D281). But enabling it can
  still leave odd audio/aim state in edge cases: **complete a level
  normally first, and back up `data/ge007.eep` before enabling it** (D387).
  The in-game warning says the same.

**Performance**
- **The whole game now holds a steady 60 fps.** v0.3.0 could settle at 30
  fps game-wide on some setups; the cause was a geometry in the frame
  timing chain, and it's root-caused and fixed (D248). Combined with the
  v0.3.0 software-RSP work, the campaign runs at a steady 60 with audio.

**Audio**
- Gunshot SFX cadence now matches the N64 original's rate, and the
  intermittent silence under sustained fire near a looping sound is gone
  (D240/D241).
- Windows audio now prefers the DirectSound backend over WASAPI for lower,
  more robust latency (D322), long-session audio degradation (issue #87) got
  pool hardening plus an optional `GE_D322=1` telemetry probe if it ever
  recurs, and the silenced-PPK "slap" plus the broader real-time mixer
  corruption behind it are fixed (D202).

**Graphics fixes from the final playtest wave**
- **Water is fixed** (D245): the `IsWater` shimmer/cross-fade (Dam,
  Frigate, Surface 2, …) now matches the N64 original — the moving seam and
  the pattern "resetting" as you moved are gone, verified against an
  era-correct 1994 reference.
- The **particle "rainbow" effect is fixed** (D252): the intermittently
  recoloured explosion/spark particles from v0.3.0 no longer occur.
- Surface 1/2: the tree backdrop that rendered as a solid wall of texture
  now renders as proper camera-facing tree cards (D236).
- Frigate (and the rare "model briefly out of place" quirk): both were the
  same never-written-fields read in the head-bob animation; fixed
  (D336/D311).
- Ejected shell casings now render (D331); the intro gun-barrel blood drip
  draws correctly (D341); the extra erroneous long muzzle-flash on some guns
  is gone (D303); the occasional z-fighting is fixed (D308); backwards-facing
  security-camera props fixed (D307); the front-end Rareware logo work
  narrowed to a subtle filtering residual and the Nintendo logo/copyright
  page renders correctly (D403/D75); the train-intro soldier pose is correct
  (D392); file-select background/gun-barrel comb rendering fixed (D397);
  stale-texture artifacts after level transitions fixed (D235); the
  randomly flickering F10 menu items are gone (D314); the watch's
  controller-page graphic renders again (D290); and file-select folders
  and Bond photos no longer vanish after backing out of a file (D342).
- Distant-geometry dropout on the biggest open levels (Streets, Egyptian) is
  substantially improved at default settings (D249).

**Windows / PC stability**
- Game exit is now orderly, so quitting no longer lands inside the graphics
  driver (a class of driver bugchecks seen during testing) (D344); an
  idle-then-click mouse-mode fix cleared a Windows non-self-recovering
  freeze (D287); plus a port-wide code audit (missing returns, prototypes,
  undefined-behaviour shifts).
- The Facility execution-scene watchdog from v0.3.0 stays enabled as a
  safeguard; the animation-pinning that motivated it is root-caused and
  fixed at the tick level (D329).

**Steam Deck / Linux**
- The first clean SteamOS/glibc build (D402), plus a first-launch preset
  (native 1280×800, 2× MSAA, vsync) for decks without a config yet (D283).

**Renderer robustness (community contribution, PR #106)**
- Setting MSAA above your driver's maximum no longer risks a black screen
  (the request is clamped to `GL_MAX_SAMPLES`), and two framebuffer-state
  bugs in the render/resolve paths were fixed. Affects both Windows and
  Linux builds (D405).

**Also this cycle (research, not yet features)**
- ROM-level mod support was investigated and found viable (the game's file
  table lives in the ROM) (D325–D328); HD-texture packs and the drop-in-ROM
  flow are tracked post-1.0; a measured CPU budget on low-end hardware
  reshaped the #92 performance investigation toward the GPU (D339).

### Known issues

- On Facility, if gas leaks during Ourumov's monologue he can pause for up
  to ~10 s before resuming the scripted shootout — a latent race that exists
  in the N64 original too (where it softlocks permanently); the port detects
  and auto-recovers it (D318).
- With *Native widescreen* on, the F10 options overlay stretches with the
  window instead of pillarboxing like the front-end menus (legible; cosmetic
  only). The front-end menus themselves are pillarboxed correctly (D335b).
- The front-end Rareware logo shows a subtle texture-filtering artifact
  (Nintendo logo and legal page are clean). Cosmetic only (D75).
- This release ships NTSC (US) assets; PAL/JP ROMs are not supported in this
  version (D258).
- **`All unlocked` is experimental: back up `data/ge007.eep` before
  enabling it.** Any save while ON — even a profile-settings change — can
  leave odd audio/aim state; completing a level normally first avoids the
  fresh-install case (D387).

### Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-0.4.0-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-0.4.0-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
`prepare-assets` tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs; the Linux bundle carries SDL2, so on both platforms nothing
needs to be installed first.

### Running it

You supply your own **GoldenEye 007 N64 ROM** (`.z64`, big-endian) that you
legally own: the **NTSC-U (US)** release is the only region this build
supports (PAL / JP are on the post-release roadmap). No Python, no
toolchain, nothing to install.

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it, named
   `ge007.ntsc-final.z64`.
3. Run the executable **from that folder**. The first run takes a few extra
   seconds: it detects your ROM, generates the two derived asset folders
   (`data/pcmodels-ntsc-final/`, `data/pccg-ntsc-final/`) once, and saves them
   for every future run. (The generator is `prepare-assets/ge007-convert`
   inside the bundle; you can also run it manually; it prints what it's doing.)

**Steam Deck:** sideload the unpacked folder (USB or a file manager), do steps
2–3, then add the executable to Games → *Add Game* as a non-Steam game. The
first launch in Game Mode picks up the Deck preset (native 1280×800, 2× MSAA,
vsync) automatically if no config exists yet.

**In-game settings on the Deck:** the options overlay is fully gamepad-driven:
it opens with **Select**, the D-pad or left stick (up/down) moves between
options, **A** steps the selected option forward, **B** steps it back, and
**Start** (or Select again) closes. No keyboard needed.

Full steps are in the bundled `README.md`.

### Verify the download

```
sha256sum -c goldeneye-pc-port-0.4.0-win64.zip.sha256
sha256sum -c goldeneye-pc-port-0.4.0-linux-x86_64.tar.gz.sha256
```

### Source & docs

<https://github.com/jkdansereau/goldeneye-pc-port>, built on the
[GoldenEye 007 decompilation](https://github.com/n64decomp/007), architecture
after the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark).
Non-commercial fan preservation/research project; not affiliated with any
rights holder. **AI disclosure:** built through agentic AI coding (Claude
Code + a local open-weight model), directed by one person in their spare
time — as much a study of what agentic development gets wrong on a
game-sized codebase as it is a port. See the README's
[Background section](https://github.com/jkdansereau/goldeneye-pc-port#background)
for the full account.
