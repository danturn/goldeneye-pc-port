## GoldenEye 007 PC Port <version>

<p align="center">
  <img src="https://github.com/jkdansereau/goldeneye-pc-port/raw/v0.3.0/docs/media/goldeneye-gh-preview.gif" width="480"
       alt="~12 s gameplay montage from live v0.3.0 play sessions (no audio track)">
</p>

> The full single-player campaign runs at a steady 60 fps with audio (music +
> SFX) playing throughout, on Windows and Linux including Steam Deck, and it
> is completable end to end: the whole campaign has been playtested through
> all 20 missions (Agent difficulty). It's an early public cut: all 20 solo
> missions load and run crash-free, but the known issues below are real;
> feedback is very welcome. **This is a pre-1.0 release, not a finished
> product** — expect rough edges and missing features until v1.0; see the
> [README's Roadmap section](https://github.com/jkdansereau/goldeneye-pc-port#roadmap)
> for where this is headed.

### What's new since v0.2.2

- **Bond is fixed in cutscenes** — the most visible remaining glitch in
  prior releases: Bond no longer floats or glitches out during cutscenes,
  his animation no longer breaks or repeats, and the camera correctly
  tracks him through scripted sequences including all three Dam end-of-level
  abseil shots (D243). Root cause: a 32-bit-era struct-size constant in the
  cutscene body-model setup never accounted for 64-bit pointer widening,
  corrupting the model's own animation-timing fields on creation. Fixing
  that also fixed the associated camera shake, which had previously been
  worked around with a camera-freeze hack that's no longer needed. **Two
  related reports also cleared up as a side effect**: Bond's third-person
  model floating above the ground at level start, and death animations
  sinking him into the floor, both shared an underlying mechanism with this
  bug — Bond now ends up in the right place the large majority of the time,
  with only a small positional drift on the rare occasion he's off (D173/
  D292).
- **Particle colors mostly fixed**: bullet-impact sparks and lingering
  smoke/explosion residue no longer cycle through a rainbow palette (D219) —
  root cause was a hardcoded raw-memory-offset read into the spark's stored
  color that landed on the wrong bytes once the containing struct grew from
  64-bit pointer widening. A second, separate cause was found and fixed in
  explosion rendering itself (an out-of-bounds vertex-buffer write
  corrupting adjacent particle data). This fix is real and stays in, but the
  rainbow effect can still occur intermittently on any platform, not every
  time and not on every level — a second cause hasn't been found yet; see
  Known Issues.
- **Automatic widescreen FOV scaling**: the game now scales its field of
  view to match your display's aspect ratio automatically, instead of
  requiring the old manual FOV-scale slider. Toggle off in the F10 overlay
  (`Widescreen Auto`) if you prefer the original 4:3 framing. The scaled
  vertical FOV is clamped to a 20°–160° range as a guard against degenerate
  values on unusual aspect ratios. (This is FOV scaling, not a native 16:9
  world/HUD re-render — see Known Issues.)
- **TV overscan artifacts removed**: the black bars top/bottom and the thin
  pixel strips left/right of the gameplay view are gone (D247/D246) — both
  were the original N64 TV-safe-area margin, invisible on a CRT under real
  overscan but visible on a PC monitor. New `Video.SafeAreaCrop` toggle
  (default on) if you want the original framing back.
- **Two more crash fixes**: a campaign-halting crash at the end of Control
  Center on 00 Agent difficulty, which would then crash on every subsequent
  relaunch until the save was reset (D295); and a crash selecting most save
  slots with `Skip Intro` enabled (D299).
- **The elusive complete freeze is root-caused and fixed.** Players had hit
  a total, non-self-recovering lockup (most recently when returning from
  Caverns to the intro screen) that was real but never reproduced on demand.
  It turned out to be a latent defect in the original game's own AI command
  stream — a record type the engine mis-measures, which our level-teardown
  timing can trigger and the N64 never presents. Fixed with a minimal change
  under the project's game-code exception process (D309/D310); a second,
  unrelated freeze mechanism found during the same playtest is now guarded
  as well (D311).
- **A NULL-pointer guard was added** around a save-data lookup that two
  players hit with an identical crash address (GitHub #87, D301) — this
  should close that crash, though we weren't able to reproduce the exact
  trigger condition ourselves to confirm it end-to-end.
- **Facility's execution softlock is caught and recovered from**: a rare
  race in the original game's own AI script (present on the N64 too) could
  leave Ourumov frozen mid-execution after Objective C, with no way out short
  of reloading. A port-side watchdog now detects the exact unrecoverable
  state and re-seeds the attack so the scene plays out as authored; it
  re-arms once the scene has cleared, so replaying Facility later in the
  same session is still covered (D318).
- **A Steam Deck crash is fixed**: an audio-thread bug could crash the game
  early in a level (`sndHandleEvent`'s sound-preemption scan could
  dereference a pointer without checking it was valid first). Live-verified
  crash-free on real Steam Deck hardware, including with `All unlocked`
  toggled on.
- **Mouse input and F10 controls fixed**: mouse input was reworked to
  follow the Perfect Dark PC port's architecture — slow mouse motion is no
  longer barely recognized and fast motion no longer saturates almost
  immediately (D300, `Input.MouseDirectLook`, on by default). The separate
  aim-speed and turn-speed sliders — which could be set to inconsistent
  values against each other — are now a single `Mouse sensitivity` control
  with a wider range. Also fixed three F10 menu bugs found along the way:
  clicking a row could unexpectedly scroll the whole list, a click near
  certain rows could silently activate a different option than the one you
  clicked (most noticeably, clicking `All unlocked` could instead trigger
  `Quit to desktop`), and the panel's bottom row no longer duplicates
  whatever item is currently selected (D251).
- **Random-number generation now matches the N64 original exactly** (D284) —
  a shift-operation bug in the PRNG had desynced the PC's random stream from
  N64's since the very first release, affecting loot placement, AI variance,
  and other randomized elements. **This changes the random sequence from
  every prior build**, so old input recordings/replays will diverge, but
  your save files are unaffected: **a migration shim automatically upgrades
  any save written by an older build the first time you load it** (D297) —
  no manual action needed.
- **Assorted texture/rendering fixes**: an incorrect IA4 texture-format
  decode and an overly-tight near-plane portal-culling guard were both
  corrected (D266/D271) — reduces some texture-edge and visibility artifacts
  on affected levels.

### Known issues

- The front-end **Nintendo logo renders as two white blobs**, and the
  Rareware logo is close but its texture filtering looks off (D75).
- **Particle colors: a real fix landed this release, but the rainbow effect
  can still occur intermittently.** One genuine, reproducible cause (an
  out-of-bounds vertex-buffer write in explosion rendering) is fixed. A
  second, still-unidentified cause remains, on both Windows and Steam Deck —
  it doesn't reproduce every time or on every level, so treat this as
  improved, not fully resolved (D252).
- **Some muzzle flashes draw an extra, erroneous long flash straight up from
  the gun** (seen on the M16, among others), on top of the normal, correctly
  drawn flash. Cosmetic only (D303).
- **Surface 1: the 2D billboard trees near the start render as a solid wall
  of tree texture** instead of discrete sprites (D236). Under active
  investigation across ten+ passes; no fix yet.
- **No native widescreen render**: v0.3.0's automatic FOV scaling (above)
  expands the *field of view* for 16:9+ displays, but the world geometry and
  HUD are still authored/laid out for 4:3 — this is not yet a true
  edge-to-edge 16:9 render.
- **No controller rebinding UI, and no macOS or ARM builds.**
- **Distant geometry can drop out on the biggest open levels** (Streets,
  Egyptian) at default FOV — a culling/LOD issue that sometimes
  self-corrects as you keep moving (D249).
- **Gunshot SFX can sound off during sustained/rapid fire**: cadence can
  drift from the N64 original's rate, and PP7/AK47 fire can occasionally go
  silent under heavy automatic fire near another looping sound (e.g. an
  alarm klaxon) (D240/D241).
- **`All unlocked` is highly experimental — do not enable it until you have
  at least one save written.** Complete a level normally first (e.g. Dam on
  Agent), then turn the toggle on. Enabling it on a brand-new install with
  no prior save can still cause silent audio and odd right-mouse-aim
  behavior (D259/D257). (The Steam Deck crash fix above addresses a
  separate, now-fixed symptom that had also been associated with `All
  unlocked` use; this audio/aim caveat is unrelated and still open.)
- Water on `IsWater` levels shows a moving seam between two patterns (D245).
- **Occasional z-fighting on some levels' geometry** (D308) — a Dam intro/
  truck-wheel instance showing odd transparent-looking areas has also been
  seen and may be related (D306). Minor, cosmetic; not investigated for
  this release.
- **In-level security camera props can occasionally end up facing backwards**
  (the wrong direction) on some levels, seen on Bunker — this is the
  gameplay security-camera object, not the player's own view (D307). Not
  investigated for this release.
- **A rare, self-clearing visual quirk**: on an occasional animation
  transition, Bond's model can briefly render out of place. It has no effect
  on gameplay or your save, and it clears on its own or by re-entering the
  level; a safeguard added this release guarantees it can never affect game
  stability (D311).
- **Two collision oddities found in final playtesting**: throwable items
  (mines, grenades) can occasionally clip through walls in certain spots
  (D312), and bullet impacts don't appear on some surfaces — doors and
  windows are unaffected (D313). Both are queued for the next release; if
  you hit either, an issue with the level and spot helps a lot.

### Downloads

| File | Platform |
|---|---|
| `goldeneye-pc-port-<version>-win64.zip` | Windows x86-64 |
| `goldeneye-pc-port-<version>-linux-x86_64.tar.gz` | Linux x86-64 (incl. Steam Deck) |

Each contains the engine executable, a README, license texts, and the
`prepare-assets` tool. **No ROM, no game assets.** The Windows bundle carries
its runtime DLLs; the Linux bundle carries SDL2, so on both platforms nothing
needs to be installed first.

### Running it

You supply your own **GoldenEye 007 N64 ROM** (`.z64`, big-endian) that you
legally own: the **NTSC-U (US)** release is what this build supports (PAL / JP
are on the roadmap). No Python, no toolchain, nothing to install.

1. Unpack the archive.
2. Make a `data/` folder next to the executable and put the ROM in it, named
   `ge007.ntsc-final.z64`.
3. Run the executable **from that folder**. The first run takes a few extra
   seconds: it detects your ROM, generates the two derived asset folders
   (`data/pcmodels-ntsc-final/`, `data/pccg-ntsc-final/`) once, and saves them
   for every future run. (The generator is `prepare-assets/ge007-convert`
   inside the bundle; you can also run it manually; it prints what it's doing.)

**Steam Deck:** sideload the unpacked folder (USB or a file manager), do steps
2–3, then add the executable to Games → *Add Game* as a non-Steam game.

**In-game settings on the Deck:** the options overlay is fully gamepad-driven:
it opens with **Select**, the D-pad or left stick (up/down) moves between
options, **A** steps the selected option forward, **B** steps it back, and
**Start** (or Select again) closes. No keyboard needed.

Full steps are in the bundled `README.md`.

### Verify the download

```
sha256sum -c goldeneye-pc-port-<version>-win64.zip.sha256
sha256sum -c goldeneye-pc-port-<version>-linux-x86_64.tar.gz.sha256
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
