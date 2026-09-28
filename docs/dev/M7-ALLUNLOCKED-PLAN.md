# M7 — Reversible AllUnlocked: investigation and fix plan

**v0.4.0 release decision (2026-09-28, user):** ship AllUnlocked as a
**loudly-documented, EXPERIMENTAL, irreversible** option for this release rather
than hiding/disabling it or landing a safe architecture. The toggle stays (default
OFF); the in-game row is now labelled **"All unlocked (EXPERIMENTAL)"** with the ON
value displayed as **"ON - UNSAFE"** in both the F10 overlay and the front PC
Options screen, and the README Known-issues bullet already tells users to back up
`data/ge007.eep` before enabling it. A proper fix (Candidate A port-only shadow or
Candidate B non-mutating virtual query) remains the v0.5.0 work item.

Status: **investigation, NO FIX APPROVED OR LANDED**. Root cause/repro: `docs/dev/findings.md` D387; original feature: D257, fresh-save repair: D281, old-CRC migration: D297. This plan supersedes the M7 "no-code proof" in `docs/dev/notes/MODERN-OPTIONS-PLAN.md`. Follow `AGENTS.md` rule 2 and `docs/dev-process.md` §7 before any `src/game` edit.

## User-visible contract to settle

Recommended: OFF (after restart, as presently documented) must display only genuinely earned mission progress/cheats; ON may *temporarily* expose every level, difficulty, 007 mode and cheat, without modifying durable progress. Achievements **earned while ON without an active cheat** should survive OFF, including difficulty inheritance, timed cheats and profile operations. Enabling/disabling must never silently delete existing genuine save records. Do not auto-repair an EEPROM already containing baked fake unlocks: the bytes lack provenance. The user has not explicitly signed off on a game-code alternative.

## Confirmed facts (source + isolated data)

- `port/src/libultra.c:geEepromRW` first migrates legacy CRCs on the five-slot block-4 read, then `geEepromPatchAllCheats` fills unlocked-cheat bits and every zero completion time with 0x3FF and re-CRCs a **temporary copy**. `fileValidateSaves` (`src/game/file2.c:527`) accepts that copy into `saves[]`. `fileWriteSave` (`file2.c:86`) writes the entire 96-byte live slot, not only the setting/progress that changed.
- Private data under `AppData/Local/Temp/ge-m7-20260927/`: the older `data/ge007 copy.eep` has two genuine times and only cheat `04 00 00` in active folder 0 (slot 4). ON or OFF boot without a save yields identical EEPROM bytes after the same legacy CRC migration. ON plus existing `GE_WSPROBE_FRONT=600` performs an ordinary Music-volume save to slot 4: cheats become `ff ff 0f`, 58 empty times become 0x3FF, two real times remain. OFF + restart validates the written CRC and retains all fakes. Full repro/logs: D387. Current `data/ge007.ini` and `data/ge007.eep` were never opened for write; copies and SHA-256s in that private test directory.
- `main.c` seeds debug unlock flags only at boot; an in-session OFF toggle leaves those RAM flags set until restart. `Game.AllUnlocked` defaults OFF; the relevant UI currently gives no explicit restart warning.
- More than a write-mask problem: a successful mission runs `end_of_mission_briefing` (`src/game/file.c:41–68`) → `fileUnlockStageInFolderAtDifficulty` (`file2.c:828`) → `fileOverwriteSaveSlotWithNewSave` (`file2.c:794`), **rotating the profile to a free EEPROM slot** and freeing the old slot. `fileCheckSaveStageDifficultyTime` only updates if the real time beats the synthetic 0x3FF. Genuine lower-difficulty completions and long (>1023-second) runs can also have legitimate 0x3FF times; value alone is not provenance. Fake cheat bits cause `fileGetIsCheatUnlocked` to return true, so `end_of_mission_briefing` skips a genuinely earned timed-cheat award. A port-only sanitizer keyed by physical slot or by `time==0x3FF` is incorrect.
- The current user EEPROM already has two nonfree slots with 60/60 max times and all cheat bits, consistent with an earlier write-back. Two separate older EEPROM backups exist in `data/`, but replacing or auto-merging them would risk later real progress; do not touch or auto-repair originals.

## Architecture decision before code

**Candidate A — port-only EEPROM shadow/merge:** keep authoritative *raw* progress per logical profile outside patched `saves[]` and translate every write to durable real-only state. Must cover direct settings writes, profile rotation, copy/delete/reset, earned times (including 0x3FF), timed cheat grants currently suppressed by fake reads, five-slot CRCs, and old-CRC migration. Physical-slot-only interception demonstrably fails. A read/write-only adapter may have insufficient information to recognize some genuine completions or cheat awards; prove a full event/provenance model in an isolated prototype before promising this path. No new sidecar that can be lost/desynced without a recovery design.

**Candidate B — non-mutating virtual query:** retire `geEepromPatchAllCheats`, keep raw `saves[]` and EEPROM intact, and make the *opt-in* UI/gameplay availability queries report AllUnlocked without changing saved bits/times. Likely small PORT-gated touch points: `fileGetIsCheatUnlocked` (`src/game/file2.c:391`, existing D225 developer-only query override), `fileIsStageUnlockedAtDifficulty` (`file2.c:681`; cheat-enabled mission selection requires `STAGESTATUS_COMPLETED`, not merely the debug-flag `UNLOCKED`), and the raw earned-cheat check in `end_of_mission_briefing` (`src/game/file.c:41–68`), which must still record real cheat awards while virtual availability is ON. Audit every caller and the Rare/PD analogue first. A live-only toggle may require restoring debug flags safely; simplest contract is **takes effect on restart** and an explicit UI label. This alters opt-in `src/game` behavior and is **not** the ABI/layout exception: no implementation or implicit sign-off. If port-only is ruled out, prepare the exact file/line, reference, reason, scope and precedent required by `docs/dev-process.md` §7; obtain explicit user approval and document the grant in a new RULE-2-SIGNOFF finding before edits.

**Release fallback:** if neither safe architecture is ready, do not present the current toggle as a reversible enhancement. Get an explicit release decision on hiding/disabling it vs shipping a loudly irreversible option; default-off alone does not protect users who toggle it on. Keep existing saves intact.

## Test matrix (private files ONLY)

1. Capture baseline hashes of copied current ini/EEPROM; separate valid older-progress, fully fresh zero EEPROM, already-contaminated, legacy-CRC, and mixed/multiple-folder fixtures. Do not copy old saves back into `data/`.
2. No-write ON boot then OFF restart: raw five slots unchanged except *explicit* D297 migration; correct UI availability in both states.
3. ON settings writes via `GE_WSPROBE_FRONT` (normal slot), in-stage F10 watch writes, and save-file chooser/reset: only real settings/intentional reset persist; CRC valid; no synthetic completions/cheats in raw file.
4. Real mission completion at agent and higher difficulty (including inherited lower difficulty), fast timed-cheat award, slow/capped 0x3FF completion, and cheat-enabled mission selection. Verify actual new earned records after OFF restart, no artificial ones. Build an isolated gameplay harness if manual replay is impractical; do not use a settings-write test as a substitute for mission completion.
5. Save-slot rotation (`fileOverwriteSaveSlotWithNewSave`), profile copy/delete, all four folders plus free slot, and reopen across process restart. Confirm all five CRCs via `GE_SAVELOG=1` and raw bit-level diff; compare copied baseline profile identity rather than just physical slot index.
6. Default-OFF NTSC/PAL/JP builds and post-landing 21-level default-render comparison; config/EEPROM originals hash-verified **after** all tests. Human test of ON/OFF UI text and behavior. Existing contaminated saves need an explicit *separate*, opt-in recovery plan; never silently rewrite them.

## Other wave item

M5 remains under user review: FPS cap and MSAA are independently exposed already; dropping the two-setting Low-end preset is a separate decision, not a dependency of this M7 investigation.
