# Golden baseline frames

`frame_NNNNNN.png` — reference `GE_PCDUMP` captures for `framediff.py`
(`-level_09` BUNKER1: end of the intro + settled gameplay). Compared structurally, not exactly — the port
is not frame-deterministic (see `docs/internals.md` "D117").

Regenerate after a deliberate visual change:

```sh
# data/ge007.ini must hold ONLY this (back yours up) — every other option at
# its compiled-in default; a personal FovScale/DrawDistance/MSAA fails the gate
printf '[Window]
Width = 640
Height = 480
' > data/ge007.ini
GE_PCDUMP="400-880:240" ./build-pc/ge007.x86_64.exe -level_09
python tools_pc/framediff.py ppm --update    # then git rm any stale stems
```

`tools_pc/verify.sh bunker1` does the ini pin + capture + diff for you.

## History

- **D168 (2026-09-01):** the `GE_PCDUMP` PPM writer emitted `glReadPixels` rows
  unreversed, so every capture — and this baseline set — was vertically
  flipped. Writer fixed (`port/fast3d/gfx_opengl.cpp`); this set regenerated
  from a fresh, correctly-oriented `-level_09` run.
- **2026-09-23 (v0.4.0):** re-baselined. The 2026-09-09 (D215) set was captured
  while the port still rendered at 30 fps (2 sim ticks per frame); D248 moved
  it to 60 fps (1 tick/frame), so frame N now lands at half the sim time, and
  frames 200/320/440 fell in the intro cutscene, whose camera is
  nondeterministic (D117); two fresh captures disagreed at 440. The window
  moved to `400-880:240`, the same sim ticks the original set sampled.
  Two independent captures of it agree 3/3. The capture ini is now
  defaults-only, and `verify.sh` pins it the same way (it used to rewrite
  only Width/Height, which kept personal settings).
