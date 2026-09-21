---
title: GoldenEye 007 PC Port
# v0.3.0: status bumped from v0.2.1; Honest-status bullets synced with the
# README's v0.3.0 known issues (D255/D282/F10-dup/overscan items removed as
# fixed). Description kept at 133 chars (Bing's 160 limit).
description: >-
  A native PC port of the 1997 N64 classic, built from decompiled source with
  a software RSP. v0.3.0 for Windows, Linux and Steam Deck.
---

<!-- v0.2.1 (Bing SEO): the "# GoldenEye 007 PC Port" h1 was removed here --
the Cayman masthead already renders the site title as an <h1>, so the page
was emitting two h1 tags. -->

A native PC port of the original 1997 Nintendo 64 _GoldenEye 007_, compiled
from the game's [decompiled source](https://github.com/n64decomp/007) with the
N64's graphics coprocessor (RSP) running in software, the same architecture
as the [Perfect Dark PC port](https://github.com/fgsfdsfgs/perfect_dark), the
same Rare "Indy" engine family, one hardware generation apart.

**Status: v0.3.0.** The full single-player campaign runs at a steady 60 fps
and is completable end to end (all 20 missions playtested); all 20 missions
load and run clean on Windows, Linux and Steam Deck, and audio (music + SFX)
plays throughout. The known rough edges — mostly cosmetic rendering defects,
stretched-not-native widescreen, and missing features — are listed plainly
under [Honest status](#honest-status).

**This is a pre-1.0 release, not a finished product** — v1.0 is the target
for a polished, feature-complete build; expect rough edges and missing
features until then. See the
[README's Roadmap section](https://github.com/jkdansereau/goldeneye-pc-port#roadmap)
for direction (PAL/JP support, real widescreen, LAN multiplayer under
consideration, and more).

<p align="center">
  <img src="media/goldeneye-gh-preview.gif" width="70%"
       alt="~12 s gameplay montage from live play sessions">
  <br><em>~12 s gameplay montage from live v0.3.0 play sessions, running in the port.</em>
</p>

## Download

| Platform | Bundle | Notes |
|---|---|---|
| **Windows** (x86_64) | [win64.zip](https://github.com/jkdansereau/goldeneye-pc-port/releases) | Engine + runtime DLLs + the one-time asset tool. |
| **Linux** (x86_64) / **Steam Deck** | [linux tarball](https://github.com/jkdansereau/goldeneye-pc-port/releases) | SDL2 is bundled, so it runs as-is on any distro, and sideloads onto a Deck with nothing installed. |

**Bring your own ROM.** Both bundles contain no ROM and no game assets: you
supply your own GoldenEye 007 N64 ROM (the
[Requirements table](https://github.com/jkdansereau/goldeneye-pc-port#requirements)
has the region filenames and SHA-1s). This release supports the NTSC-U (US)
ROM; PAL and JP are on the roadmap. Then: unpack, drop the ROM in `data/`,
and launch; the first run generates the derived assets automatically (no
Python or other tooling needed). The full steps are in the
[Quick start](https://github.com/jkdansereau/goldeneye-pc-port#quick-start);
pre-built releases are legal to distribute precisely because they're useless
without a ROM you already own.

## See it running

<p align="center">
  <img src="img/shots/shot-06.jpg" width="45%" alt="Bunker 1, rendered by the port (v0.2.0 playtest)">
  <img src="img/shots/shot-01.jpg" width="45%" alt="Dam, rendered by the port (v0.2.0 playtest)">
</p>

<details>
<summary><strong>Full playtest gallery</strong>: 19 in-engine captures from the v0.2.0 Windows playtest, across the campaign</summary>

<p align="center">
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-01.jpg" width="100%" alt="In-engine: Dam (v0.2.0)">
    <div><small>Dam</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-02.jpg" width="100%" alt="In-engine: Facility (v0.2.0)">
    <div><small>Facility</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-03.jpg" width="100%" alt="In-engine: Runway (v0.2.0)">
    <div><small>Runway</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-04.jpg" width="100%" alt="In-engine: Surface (v0.2.0)">
    <div><small>Surface</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-05.jpg" width="100%" alt="In-engine: Surface (v0.2.0)">
    <div><small>Surface</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-06.jpg" width="100%" alt="In-engine: Bunker 1 (v0.2.0)">
    <div><small>Bunker 1</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-07.jpg" width="100%" alt="In-engine: Silo (v0.2.0)">
    <div><small>Silo</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-09.jpg" width="100%" alt="In-engine: Frigate (v0.2.0)">
    <div><small>Frigate</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-10.jpg" width="100%" alt="In-engine: Frigate (v0.2.0)">
    <div><small>Frigate</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-11.jpg" width="100%" alt="In-engine: Surface 2 (v0.2.0)">
    <div><small>Surface 2</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-13.jpg" width="100%" alt="In-engine: Bunker 2 (v0.2.0)">
    <div><small>Bunker 2</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-16.jpg" width="100%" alt="In-engine: Statue (v0.2.0)">
    <div><small>Statue</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-19.jpg" width="100%" alt="In-engine: Statue (v0.2.0)">
    <div><small>Statue</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-20.jpg" width="100%" alt="In-engine: Archives (v0.2.0)">
    <div><small>Archives</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-21.jpg" width="100%" alt="In-engine: Cradle (v0.2.0)">
    <div><small>Cradle</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-23.jpg" width="100%" alt="In-engine: Cradle (v0.2.0)">
    <div><small>Cradle</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-24.jpg" width="100%" alt="In-engine: Aztec (v0.2.0)">
    <div><small>Aztec</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-25.jpg" width="100%" alt="In-engine: Aztec (v0.2.0)">
    <div><small>Aztec</small></div>
  </div>
  <div style="width:32%;text-align:center;margin:4px">
    <img src="img/shots/shot-26.jpg" width="100%" alt="In-engine: Frigate (v0.2.0)">
    <div><small>Frigate</small></div>
  </div>
</p>
</details>

More captures may land here as playtesting continues.

## Play it, or take it apart

- **Try it yourself**: grab a bundle above, bring your own ROM, and play the
  campaign. It's an early cut for exactly this: if something breaks, an
  [issue](https://github.com/jkdansereau/goldeneye-pc-port/issues) with what
  you were doing is genuinely useful.
- **Read the code**: the game logic in `src/` is unmodified decompilation;
  every hardware surface (video, audio, input, save storage, and the
  software-RSP renderer) is shimmed in the MIT-licensed `port/` layer.
  [Internals](internals.md) is the map; [Porting notes](porting-notes.md) is
  the catalogue of N64→PC bug classes hit along the way, a good read even if
  you never touch the code.
- **Mod it**: the port layer, build system and `tools_pc/` helpers are MIT
  licensed and yours to extend: new video options, input tweaks, your own
  asset sidecar. [CONTRIBUTING](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/CONTRIBUTING.md)
  has the ground rules that keep it faithful to the original game.
- **AI disclosure**: development here was agentic — Claude Pro plus a local
  open-weight model on a single RTX 5090, as of August–September 2026. This
  project is as much a study of *that process* as it is a port: whether
  current LLMs can carry a codebase like this, and what actually goes wrong
  along the way. Game codebases have historically been a rough fit for
  LLMs — large, stateful, hardware-adjacent, unforgiving of a subtly wrong
  memory layout — more so for older local models, so this is a useful data
  point on where that stands now, for better or worse. Judge the result for
  yourself. I'm one person doing this in my spare time, not a team. See
  [the full write-up](dev/agentic-development.md) for the setup, timeline,
  handoff workflow, and an honest assessment of what did and didn't work.

## How it works

The R4300 game code is compiled completely unmodified; the decompilation's
control flow is ground truth. The N64's Reality Signal Processor, the graphics
coprocessor that builds and executes each frame's display list, is emulated in
software: the port interprets the GBI stream the game emits and translates it
to OpenGL, bypassing the RDP entirely. Everything else that would touch N64
hardware (video, audio, input, timers, save storage) is shimmed in a small
dedicated layer, following the architecture of the Perfect Dark PC port from
the same Rare engine family. Full detail: [Internals](internals.md); the bug
catalogue: [Porting notes](porting-notes.md).

## Honest status

- **Particle colours** can still drift through a rainbow palette
  intermittently: one genuine cause is fixed this release, a second hasn't
  been found yet (D252).
- **Some muzzle flashes draw an extra erroneous long flash** straight up from
  the gun, on top of the correct one. Cosmetic only (D303).
- Water levels show a moving seam between two water patterns (D245);
  occasional z-fighting on some geometry (D308); in-level security-camera
  props can occasionally face backwards (D307). Minor and cosmetic.
- **The front-end Nintendo logo renders as two white blobs** and the Rareware
  logo's texture filtering looks off (D75).
- Surface 1's 2D billboard trees render as a solid wall of tree texture
  instead of discrete sprites; under active investigation (D236).
- **Widescreen is stretched, not native**: 16:9 stretches the 4:3 frame to
  fill the display; automatic FOV scaling keeps the framing comfortable and
  gameplay is unaffected, but a distortion-free native widescreen render is
  still on the roadmap. Distant geometry can also drop out on the biggest
  open levels (D249).
- **Gunshot SFX can sound off during sustained/rapid fire** (D240/D241).
- **`All unlocked` is highly experimental** — don't enable it until you have
  at least one save written, or it can break audio and mouse aim
  (D257/D259/D281).
- Bond's cutscene positioning is fixed and right the large majority of the
  time now; on the rare occasion he's off it's a small drift — no more
  floating or spin-glitching (D173/D292/D243).
- No macOS/ARM support; no controller rebinding UI yet.

The full list, with root causes and fix status: the
[README's Status section](https://github.com/jkdansereau/goldeneye-pc-port#status)
and the [finding log](https://github.com/jkdansereau/goldeneye-pc-port/tree/main/docs/dev).

## Documentation

- [The two-agent development case study](dev/agentic-development.md): goal, setup, timeline, the handoff workflow, and an honest assessment of what did and didn't work.
- [Development process](dev-process.md): how work was scoped, partitioned, and budgeted across agents; the finding-log discipline.
- [Internals](internals.md): architecture, the software RSP-emulation approach, GoldenEye-vs-Perfect-Dark engine differences, the phased plan.
- [Porting notes](porting-notes.md): the recurring Nintendo 64 → PC bug classes hit during the port, with fixes.
- [Building](building.md): full build and asset-extraction guide.

---

<small>Non-commercial fan preservation/research project. No ROM or game assets
are distributed; you supply a ROM you already own. Not affiliated with or
endorsed by Nintendo, Rare, Microsoft, MGM, Danjaq, or EON Productions.</small>
