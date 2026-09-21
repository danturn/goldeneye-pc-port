# AGENTS.md — GoldenEye 007 PC Port

## What this is

- `n64decomp/007`: WIP decompilation of GoldenEye 007 (N64), byte-matches US/EU/JP ROMs.
- Active work: **PC port** modelled on the Perfect Dark PC port (same Rare "Indy" engine family).
- **Reference docs:** `docs/internals.md` — architecture, GE-specific RSP deltas, phased plan (§1–§10). `docs/dev/findings.md` — the `Dxx` finding log (§F + §H). **Look up findings via `docs/dev/findings-index.csv` (label, one-liner, status; regenerate with `tools_pc/gen_findings_index.py`), then read only the specific `## Dxx` entry (multi-pass labels like D202/D176(a) have large sections — grep within the section or read with offset/limit, don't slurp it whole). Never linear-read.** `docs/porting-notes.md` — the recurring N64→PC bug classes (dense; skim the headers, read what's relevant).
- **Current status:** the README "Status" section, and `docs/dev/LEVEL-STATUS.md` for the per-level sweep. Current task + environment: `docs/HANDOFF.md` (a rolling local working file — may be absent in a fresh clone; fall back to the README "Status" section).
- **Dispatching subagents?** `docs/dev-process.md` — task budgets/deadlines, file partitioning, pre-flight, the standard brief template. Every investigation subagent reads `docs/porting-notes.md` first and appends to it.

## Non-negotiables

1. **N64 build untouched.** `Makefile`, `tools/`, `rsp/`, `ld/` belong to the N64 build. Never modify them for the PC port.
2. **Game logic is unmodified.** The decomp's control flow and behavior are ground truth for 1:1 fidelity — never change them. All N64 *hardware* dependencies are satisfied by the `port/` layer; if a game file seems to need a behavioral change, stop and check the exception below before assuming the fix belongs in `port/`. **Diagnosis is never restricted — only the fix.** Trace a bug as deep into `src/game` state as the evidence leads, and name the exact struct/field/logic responsible, before deciding which of three buckets it falls in (full framing: `docs/dev-process.md`). **Narrow exception (ABI/layout only):** the 32→64-bit pointer-width transition forces a small class of mechanical, semantics-preserving edits that cannot be isolated in `port/` — **any struct-layout or pointer-width-driven misread caused by 32→64-bit widening**, whether in a ROM-serialized record (a struct with a 32-bit-pointer field misaligns when read as 64-bit) or a **live runtime struct/union** (a raw-byte offset alias into a union arm whose true field shifted because an earlier member in the same union widened — the D209/D210/D255 pattern; see `docs/porting-notes.md` §A1 for the full catalogue and its diagnostic tells). These follow the PD ground-truth pattern (store the embedded address as `u32` and cast to a real pointer at the use site; or read the correctly-named/typed field instead of a raw-offset/mistyped alias), change no logic or behavior, and are each documented in `docs/dev/findings.md` §F/D3x, cross-tagged to §A1 where that pattern applies. **A genuine behavioral difference** — the decomp's byte-identical code, given verified-correct inputs, still diverging from real N64 behavior — is not covered by this exception and requires the rule-2 sign-off procedure (`docs/dev-process.md`) before any `src/game` edit. No other game-code edits are permitted.
3. **Region macros mirror the Makefile.** `CMakeLists.txt` `REGION_DEFS` must match the N64 Makefile's per-region macro set exactly (finding A1). Divergence = silent branch divergence + link failures.
4. **`src/libultrare/Makefile.libultrare` is ground truth** for original-vs-Rare libultra files (finding B3). The PC build compiles: `libultra/audio`, `libultrare/audio` (drvrNew/env/reverb), `libultra/gu`, and `libultrare/io/vitbl.c` only. All other `io/` + `os/` files are excluded and shimmed in `port/src/libultra.c`.
5. **`rsp/graphics/gmain.s` is the RSP ground truth** — the authoritative reference for which GBI commands GE emits (modified fast3d, 1545 lines). We do not run it on PC; `port/fast3d/` replaces it. Use it to validate the software RSP's command decoding and the custom CC/RM modes.

## Critical files

| File | Role |
|---|---|
| `docs/internals.md` | Architecture + RSP deltas + phased plan (§1–§10). Reference, not a linear read. |
| `docs/dev/findings.md` | The `Dxx` finding log (§F/§H); lookups via `docs/dev/findings-index.csv`. |
| `CMakeLists.txt` | PC build (parallel to the N64 Makefile). Source list + `REGION_DEFS` live here. |
| `port/src/` | Shims: `libultra.c` (OS API), `gesched.c` (scheduler), `n64stubs.c` (boot/TLB/FPU/rmon), `random.c` (PRNG ported verbatim from `random.s`), `ucode.c` (microcode segment markers), `main.c`, `video.c`, … |
| `port/fast3d/` | Software RSP (adapted from the PD port). The main Phase 2 work. |
| `rsp/graphics/gmain.s` | GE's RSP ucode — ground truth for GBI/CC/RM. |
| `reference/mouse-injector/README.md` | **Stub only** — the vendored GEPD-Edition Mouse Injector source (GPLv2) was removed from the public repo 2026-09-19 (license hygiene: GPL code + prebuilt binary in an MIT project; it was never compiled here). The stub records provenance + how to re-vendor locally (gitignored). The ported mouse-aim model lives in `port/src/input.c` (D194 lineage); design record: `docs/dev/GEPD-INPUT-PLAN.md`. |
| A local **Perfect Dark PC port** checkout ([fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)) | **Standing reference** — consult it whenever a work item has a PD analogue (same Rare engine family): port-layer ground truth (`port/fast3d/`, crash/system/video), plus copy candidates `port/src/preprocess/` (N64→PC asset conversion; `filemodel.c` is the D43 near-analogue) and `mixer.c`/`input.c`/`fs.c`. Port-layer files only; same family ≠ identical format — validate per field. Full audit: `docs/internals.md` §2.4. |

## Build

```sh
./build-pc.sh ntsc-final   # or pal-final / jpn-final
```

Needs CMake + SDL2 + zlib + OpenGL, and must run from the MSYS2 MINGW64 shell
(see `build-pc.sh` header and `docs/building.md`). ROM goes in `./data/`
(not distributed); assets must be extracted from it first (`docs/building.md`).

## Verification ritual (after any build-affecting change)

1. **Undefined symbols.** Every symbol referenced by the compiled set (see `CMakeLists.txt`: `SRC_GAME`, `SRC_ENGINE`, `SRC_LIBAUDIO`, `SRC_LIBULTRARE_AUDIO`, `SRC_LIBULTRARE_DATA`, `SRC_GU`, `SRC_PORT*`) must be defined exactly once in the compiled set or in `port/`. Symbols that live in EXCLUDED files (`libultra/io/*`, `libultrare/io/*` except `vitbl.c`, `libultra/os/*`, `libultrare/os/*`, `sched.c`, `rmon.c`, `vi.c`, `src/*.s`) must be provided by `port/src/libultra.c`, `gesched.c`, `n64stubs.c`, `random.c`, or `ucode.c`.
2. **Duplicates.** No symbol defined twice across the compiled set (watch `sp_*` stacks, `rmon*`, `os*` shims, segment markers).
3. **Syntax.** Every touched file must parse; `./build-pc.sh` is the final word.

Run `/linkcheck` for this sweep. Record new findings in `docs/dev/findings.md` §F/§H style (next `Dxx` label after the last used) and add the label to the §F index.

## Phase status (summary — see README + `docs/dev/findings.md` for detail)

- **Phase 0–1.5:** done. Build system, boot chain, OS-shim layer, fast3d
  integration, first frames, full intro rendering.
- **Phase 2 (rendering):** in progress. All 20 solo missions (plus the
  ending-credits sequence, `-level_54`) load + render + survive an
  unattended window; front end (menu → mission select → briefing →
  start) is functional; file-backed EEPROM saves work. Cosmetic defects are
  parked in `docs/dev/GRAPHICS-BACKLOG.md`.
- **Phase 3 (audio + input):** input layer done (`port/src/input.c`); polish
  bugs open (D118* mouse-look residuals; interactive feel-checks owed). Audio
  mixer done (libaudio → SDL software mixer, D198–D201); D204 tempo drift
  fixed + measured; D202 stuck door loop root-caused with a port-side
  expiration (M-66b) awaiting by-ear verification.
- **Phase 4 (saves + polish):** file-backed EEPROM done; widescreen, config,
  rebinding UI outstanding.
