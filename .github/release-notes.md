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

- **Water is fixed.** The shimmer/cross-fade on `IsWater` levels (Dam,
  Frigate, Surface 2, …) now matches the N64 original — the moving seam
  between two patterns and the pattern "resetting" as you moved are gone.
  Verified against an era-correct 1994 reference (D245).
- **A modern, regrouped options experience.** Both options screens were
  reorganized into functional sections (video/graphics, sound, bond file)
  with per-section reset-to-defaults; a GE-style **F10 in-game overlay**
  covers video, input and the watch-backed settings; the front end gained a
  **PC Options** screen next to the file-select bar; and the confusing
  "save file" wording is now "profile". Options that only existed in the
  in-game watch (auto-aim, look-ahead and friends) are now editable from the
  PC options too — and they *stick*: the "auto-aim won't stay on" report is
  fixed (D330/D343/D350/D352–D356).
- **Crosshair, your way (opt-in).** On/off, colour, size and style rows were
  added to the options. All default to the untouched N64 crosshair — nothing
  changes unless you ask (D373/D379/D381/D382).
- **In-game key rebinding.** A two-slot key editor (PC layout + in-level
  layout) now lives in both options screens, alongside a GEPD-style preset
  layout that is the new default, and a crouch/reload binding set. INI-file
  bindings still work (D371/D383/D385).
- **Controller support got a wave of fixes**: crouch now uses the engine's
  real crouch input (D375), Xbox-variant controller parity (D394), and —
  new — the N64 **Rumble Pak now drives real gamepad haptics** on supported
  pads, with a `Rumble strength` slider (D401).
- **Aim mode finally works on every input device.** The "Aim style" toggle
  (N64: the crosshair deflects and the camera follows at the edge — vs
  Centred: the crosshair sits at screen center and your mouse *or* stick
  moves the camera directly) previously only functioned with a mouse; now it
  works with a controller too, with matched full-stick speed and new
  "Aim range"/sensitivity rows to dial it in (D404).
- **`All unlocked` ships as a loudly-documented EXPERIMENTAL feature.** It
  works for its intended purpose (skip to any mission), but enabling it on a
  brand-new install with no prior save can still corrupt audio state and
  aim behaviour: **complete a level normally first, then toggle it on**
  (D387). The in-game warning says the same.
- **Renderer robustness (community contribution, PR #106).** Setting MSAA
  above your driver's maximum no longer risks a black screen — the request
  is clamped to `GL_MAX_SAMPLES` — and two framebuffer-state bugs in the
  render/resolve paths were fixed. Affects both Windows and Linux builds
  (D405).
- **Graphics fixes from the final playtest wave**: the file-select
  background / gun-barrel comb rendering (D397), the train-intro soldier
  pose (D392), the extra erroneous muzzle-flash on some guns (D303), the
  occasional z-fighting (D308), the backwards-facing security-camera props
  (D307), and the front-end logo/copyright page (D403). Distant-geometry
  dropout on the biggest open levels (Streets, Egyptian) is substantially
  improved at default settings (D249).
- **Steam Deck / Linux**: the first clean SteamOS/glibc build, plus a
  first-launch preset (native 1280×800, 2× MSAA, vsync) for decks that don't
  have a config yet (D402/D283).
- **Gunshot audio fixed**: cadence now matches the N64 original's rate, and
  the intermittent silence under sustained fire near a looping sound is gone
  (D240/D241). Long-session audio degradation (reported on issue #87) got
  pool hardening plus an optional `GE_D322=1` telemetry probe if it ever
  recurs.

### Known issues

- **`All unlocked` is highly experimental — do not enable it until you have
  at least one save written.** Complete a level normally first (e.g. Dam on
  Agent), then turn the toggle on. Enabling it on a brand-new install with
  no prior save can cause silent audio and odd right-mouse-aim behaviour
  (D387).
- **No native widescreen render**: the automatic FOV scaling expands the
  *field of view* for 16:9+ displays, but the world geometry and HUD are
  still authored/laid out for 4:3 — this is not yet a true edge-to-edge 16:9
  render.
- **The front-end Rareware logo has a texture-filtering residual** (D75);
  the Nintendo logo and copyright page render correctly.
- **NTSC-U only**: PAL and JP ROMs boot but do not run correctly; asset
  repair for those regions is on the post-release roadmap (D258).
- The Facility execution-scene watchdog from v0.3.0 remains enabled as a
  safeguard; the underlying animation-pinning cause it was guarding against
  is fixed in this release (D318/D329).

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
