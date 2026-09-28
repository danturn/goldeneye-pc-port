#!/usr/bin/env python3
"""LOD-binding audit: which bg room draw classes combine G_TL_LOD (other_mode_H
bit 16) with a texture that has AUTHORED mip levels (explicit-LOD header,
imagelist control byte bit 7).

Why this matters (D236-class follow-up): on N64, `texWriteTileLods` places each
authored LOD level at successive TMEM addresses (tile n @ cumulative size) and
the RDP's G_TL_LOD sampling selects the coarser tile as geometry minifies.
The port folds every G_TL_LOD draw to tile 0 (D107, with the D236 type-1
exception) and lets GL generate driver mips from the finest level instead. For
ordinary textures that is a subtle approximation; for 1-bit-alpha cut-outs it
is the D236 smear mechanism via a different binding type.

GE's GBI dialect (modified fast3d, see gmain.s): G_SETOTHERMODE_L = 0xB9 with
w0 bits 8-15 = shift, bits 0-7 = num_bits; OML is shift 3 / 29 bits
(0xB900031D), OMH is shift 16 / 16 bits (0xBA001010).

Usage:  python tools_pc/bg_lod_audit.py [level ...]   (no args: all levels)
"""
import collections
import io
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools_pc"))

import bg_gdl_census as census
import texheader as th

G_NOOP = 0xC0


def walk_lod_binds(gdl):
    """Yield (oml, omh, texnum, type) for every G_NOOP texture bind.

    GE's modified fast3d encodes G_SETOTHERMODE_L/H (0xB9/0xBA) as partial
    field writes: w0 bits 8-15 = shift, bits 0-7 = num_bits.  For OMH the value
    sits at its ABSOLUTE bit position in w1 (fast3d calls
    gfx_sp_set_other_mode(shift+32, nbits, w1<<32) -- an identity map, see
    gfx_pc.cpp:3707-3708): e.g. w1=0x00010000 sets TEXTLOD (bit 16),
    w1=0x00040000 sets TEXTDETAIL=G_TD_DETAIL (bits 17-18), w1=0x00100000
    sets CYCLETYPE=G_CYC_2CYCLE (bit 20).  For OML fast3d ORs w1 in at bits
    3-31; the raw w1 is what probes log as `oml`, so keep it as the class key.
    The texture TYPE of a G_NOOP bind is w0 & 7 (tex.c `switch (in->words.w0
    & 7)`); texnum = w1 & 0xfff.
    """
    cur_oml, cur_omh = 0, 0
    n = len(gdl) // 8
    for k in range(n):
        w0, w1 = struct.unpack_from(">II", gdl, k * 8)
        op = w0 >> 24
        if op == 0xB9:
            cur_oml = (cur_oml & 7) | w1
        elif op == 0xBA:
            shift = (w0 >> 8) & 0xFF
            mask = ((1 << (w0 & 0xFF)) - 1) << shift
            cur_omh = (cur_omh & ~mask) | (w1 & mask)
        elif op == G_NOOP:
            yield cur_oml, cur_omh, w1 & 0xFFF, w0 & 7


def main():
    region = "ntsc-final"
    levels = sys.argv[1:] or None
    rom, rows = th.load(region)

    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        import d43_emit as m
    bg_files = sorted(k for k in m.fl_by_base if k.startswith("bg_") and k != "bgx")
    if levels:
        bg_files = ["bg_%s_all_p" % l for l in levels]

    grand = collections.Counter()
    alpha_hits = []
    for name in bg_files:
        level = name[3:-6]  # strip 'bg_' and '_all_p'
        try:
            data = census.load_rom_file(name, region)
        except SystemExit as e:
            print("%s: %s" % (name, e))
            continue
        if not data:
            print("%-28s (empty file-table entry -- level reuses another bg file)"
                  % name)
            continue
        rooms, _ = census.read_rooms(data)
        lod_explicit = collections.Counter()   # texnum -> binds under LOD-on
        lod_total = 0
        type1_explicit = collections.Counter()
        for i in range(1, len(rooms)):
            csz_pri, csz_sec = census.blob_sizes(rooms, i)
            for off_raw, csz in ((rooms[i][1], csz_pri), (rooms[i][2], csz_sec)):
                if not off_raw or csz <= 0:
                    continue
                out = census.inflate_blob(data, census.seg_off(off_raw), csz)
                if out is None or isinstance(out, tuple):
                    continue
                for oml, omh, texnum, ttype in walk_lod_binds(out):
                    if omh is None or not (omh & (1 << 16)):
                        continue
                    lod_total += 1
                    off, size = rows[texnum]
                    blob = rom[off:off + size]
                    if not blob:
                        continue
                    d = th.parse(blob[:5])
                    if d["explicit_lods"]:
                        lod_explicit[texnum] += 1
                        if ttype == 1:
                            type1_explicit[texnum] += 1
        n_exp = sum(lod_explicit.values())
        print("%-28s LOD-on binds=%-5d explicit-LOD texnums=%-3d binds=%-4d"
              % (name, lod_total, len(lod_explicit), n_exp))
        if lod_explicit:
            detail = []
            for t, c in lod_explicit.most_common(12):
                off, size = rows[t]
                d = th.parse(rom[off:off + 5][:5])
                tag = " T1" if t in type1_explicit else ""
                detail.append("tex%d(%s x%d%s)" % (t, d["format_name"], d["lods"] + 1, tag))
            print("    " + " ".join(detail))
        grand[level] = (lod_total, n_exp)
        for t in lod_explicit:
            off, size = rows[t]
            d = th.parse(rom[off:off + 5][:5])
            if d["gbi_fmt"] == 0 and d["gbi_siz"] == 2:
                alpha_hits.append((level, t))

    print("\nlevels with explicit-LOD textures bound under G_TL_LOD:")
    for lvl, (tot, n) in sorted(grand.items()):
        if n:
            print("  %-8s %d/%d LOD-on binds" % (lvl, n, tot))
    print("\nRGBA5551 (1-bit alpha) explicit-LOD textures bound under G_TL_LOD:")
    for lvl, t in sorted(set(alpha_hits)):
        print("  %-8s tex%d" % (lvl, t))


if __name__ == "__main__":
    main()
