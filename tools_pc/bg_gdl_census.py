#!/usr/bin/env python3
"""D236 pass 22: offline census of a level's bg room display lists.

Decodes `bg/bg_<level>_all_p.seg` straight out of the ROM and walks every
room's primary and secondary GDL, reporting the distinct render-mode
(`G_SETOTHERMODE_L`, i.e. the `oml` words the live `GE_D2xx` probes log) and
texture-image classes each room draws.  This answers "which bg draw class
carries the trees, and which room draws it" without building or running the
game.

Pipeline (mirrors `bg.c` exactly):
  * the .seg file is stored UNCOMPRESSED in ROM (obLoadBGFileBytesAtOffset
    random-accesses it by byte offset); only the per-room blobs inside it are
    deflated;
  * word[1] of the decompressed file is the segment offset of the
    `bg_room_data` array (`bg.c:865`), 24 bytes/record:
    u32 pPointTableBin, u32 pPriMappingBin, u32 pSecMappingBin, coord3d pos;
  * a room's compressed DL size is the distance to the *next* non-zero
    mapping-bin offset, taking primary and secondary as one interleaved
    stream (`bg.c:944-990` + `getPri/SecMappingBinCount`);
  * each blob is itself raw-deflate behind its own 2-byte header
    (`bgDecompress` -> `decompressdata`).

The words reported are the ones **as authored in the file**, i.e. BEFORE
`bgApplyDynamicCCRMLUT` rewrites them at room load.  The `->` column shows
what the applied LUT turns each one into, so the output can be compared
directly against what the runtime probes log.  Only LUTs 1/5/6/7 are ever
applied (`bg.c:2797/2804/2812/2819`); see D236 pass 21.

Usage:  python tools_pc/bg_gdl_census.py [sevx] [ntsc-final]
"""
import collections
import contextlib
import io
import os
import struct
import sys
import zlib

import rendermode
import texheader

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools_pc"))

G_SETOTHERMODE_L = 0xB9
G_SETTIMG = 0xFD
G_NOOP = 0xC0
G_ENDDL = 0xB8
G_DL = 0xDE
# gsDPSetRenderMode's w0: SETOTHERMODE_L, shift 3, length 29 -> the render-mode
# field.  Other 0xB9 commands (alpha compare, z source) use different shifts
# and are counted separately.
RENDERMODE_W0 = 0xB900031D

IM_FMT = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}
IM_SIZ = {0: "4b", 1: "8b", 2: "16b", 3: "32b"}


def load_rom_file(name, region):
    """Decompress one ROM file table entry by name."""
    sys.argv = ["", region]
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        import d43_emit as m
    key, row = m.find_row(name)
    if not key:
        raise SystemExit("not in ROM file table: %s" % name)
    addr, size = row
    raw = m.rom[addr:addr + size]
    # Model files are raw-deflate behind a 2-byte 0x1172 header, but the bg
    # .seg files are stored UNCOMPRESSED in ROM -- they have to be, because
    # obLoadBGFileBytesAtOffset() random-accesses them by byte offset to pull
    # one room's blob at a time.  Only the per-room blobs inside are deflated.
    try:
        return zlib.decompress(raw[2:], -15)
    except zlib.error:
        return raw


def seg_off(v):
    """Segment-0x0F address -> file offset (bg.c's BG_SEG_TO_PTR)."""
    return v & 0x00FFFFFF


def read_rooms(data):
    """Parse the bg_room_data array: [(point, pri, sec, pos), ...]."""
    list_off = seg_off(struct.unpack_from(">I", data, 4)[0])
    rooms = []
    off = list_off
    while off + 24 <= len(data):
        point, pri, sec = struct.unpack_from(">III", data, off)
        pos = struct.unpack_from(">fff", data, off + 12)
        rooms.append((point, pri, sec, pos))
        # The array is terminated by the sentinel record whose pPriMappingBin
        # is 0 AND which is not just an interior room with no primary DL, so
        # walk until all three offsets are zero.
        if len(rooms) > 1 and point == 0 and pri == 0 and sec == 0:
            break
        off += 24
    return rooms, list_off


def next_nonzero(rooms, start, field):
    i = start
    while i < len(rooms) and rooms[i][field] == 0:
        i += 1
    return i if i < len(rooms) else None


def blob_sizes(rooms, i):
    """csize_primary / csize_secondary for room i, per bg.c:944-990."""
    _point, pri, sec, _pos = rooms[i]
    csz_pri = csz_sec = 0
    if pri:
        pidx = next_nonzero(rooms, i + 1, 1)
        sidx = next_nonzero(rooms, i, 2)
        if pidx is not None and (sidx is None or pidx <= sidx):
            csz_pri = seg_off(rooms[pidx][1]) - seg_off(pri)
        elif sidx is not None:
            csz_pri = seg_off(rooms[sidx][2]) - seg_off(pri)
    if sec:
        pidx = next_nonzero(rooms, i + 1, 1)
        sidx = next_nonzero(rooms, i + 1, 2)
        if pidx is not None and (sidx is None or pidx <= sidx):
            csz_sec = seg_off(rooms[pidx][1]) - seg_off(sec)
        elif sidx is not None:
            csz_sec = seg_off(rooms[sidx][2]) - seg_off(sec)
    return csz_pri, csz_sec


def inflate_blob(data, off, size):
    if size <= 2:
        return None
    try:
        return zlib.decompress(data[off + 2:off + size], -15)
    except zlib.error as e:
        return ("ERR", str(e))


def walk_gdl(gdl):
    """Walk an 8-byte big-endian N64 Gfx stream.

    Returns (rendermodes, textures, opcodes, ncmd) where rendermodes maps an
    oml word to how many draw-setup commands used it, and textures maps
    (fmt, siz) to the set of image addresses set while it was active.
    """
    rms = collections.Counter()
    texs = collections.Counter()
    ops = collections.Counter()
    pairs = collections.Counter()
    cur_rm = None
    n = len(gdl) // 8
    for k in range(n):
        w0, w1 = struct.unpack_from(">II", gdl, k * 8)
        op = w0 >> 24
        ops[op] += 1
        if op == G_SETOTHERMODE_L and w0 == RENDERMODE_W0:
            cur_rm = w1
            rms[w1] += 1
        elif op == G_NOOP:
            # bg room DLs never carry G_SETTIMG -- they reference textures by
            # NUMBER in a G_NOOP, which texLoadFromGdl() resolves into real
            # texture commands at room load (tex.c:779, `case G_NOOP:
            # texnum = in->words.w1 & 0xfff`).
            texs[w1 & 0xFFF] += 1
            pairs[(cur_rm, w1 & 0xFFF)] += 1
    return rms, texs, ops, pairs, n


def report(level, region):
    name = "bg_%s_all_p" % level  # file-table basename (no dir/.seg)
    data = load_rom_file(name, region)
    print("%s: %d bytes decompressed" % (name, len(data)))
    rooms, list_off = read_rooms(data)
    print("room table at file offset 0x%x, %d records\n" % (list_off, len(rooms)))

    all_rms = collections.Counter()
    all_pairs = collections.Counter()
    rm_rooms = collections.defaultdict(set)
    tex_rooms = collections.defaultdict(set)
    bad = 0
    for i in range(1, len(rooms)):
        csz_pri, csz_sec = blob_sizes(rooms, i)
        for label, off_raw, csz in (("pri", rooms[i][1], csz_pri),
                                    ("sec", rooms[i][2], csz_sec)):
            if not off_raw or csz <= 0:
                continue
            out = inflate_blob(data, seg_off(off_raw), csz)
            if out is None:
                continue
            if isinstance(out, tuple):
                bad += 1
                print("room %3d %s: INFLATE FAILED at 0x%x size %d (%s)"
                      % (i, label, seg_off(off_raw), csz, out[1]))
                continue
            rms, texs, ops, pairs, ncmd = walk_gdl(out)
            all_rms.update(rms)
            all_pairs.update(pairs)
            for w in rms:
                rm_rooms[w].add(i)
            for t in texs:
                tex_rooms[t].add(i)
            print("room %3d %s: csize=%-6d usize=%-6d cmds=%-5d rm=%d tex=%d  %s"
                  % (i, label, csz, len(out), ncmd, len(rms), sum(texs.values()),
                     " ".join("0x%08x" % w for w in sorted(rms))))

    print("\n%s" % ("=" * 78))
    rm = rendermode.RenderModes()
    luts = rendermode.applied_luts(rm)
    print("RENDER-MODE CENSUS (as authored in the file, pre-LUT)")
    print("  the '->' column is what bgApplyDynamicCCRMLUT rewrites the word to at")
    print("  room load when fog is on (LUT 1 primary / LUT 5 secondary) -- i.e. the")
    print("  value a live GE_D2xx probe would log.  With fog OFF, LUTs 6/7 rewrite")
    print("  COMBINE modes only, so render modes stay exactly as authored.")
    print("=" * 78)
    for w, n in all_rms.most_common():
        rr = sorted(rm_rooms[w])
        to = luts.get(1, {}).get(w) or luts.get(5, {}).get(w)
        print("  0x%08x -> %-12s %4d setups %3d rooms  rooms=%s"
              % (w, ("0x%08x" % to) if to else "(unchanged)", n, len(rr),
                 ",".join(map(str, rr[:14])) + ("..." if len(rr) > 14 else "")))
        print("      %s" % " | ".join(rm.decode(w)["flags"]))

    print("\n%s" % ("=" * 78))
    print("TEXTURES PER RENDER-MODE CLASS (texture numbers from G_NOOP w1)")
    print("  ROM header decoded offline; 'live' is what a runtime probe prints.")
    print("=" * 78)
    rom, trows = texheader.load(region)
    bytex = collections.defaultdict(collections.Counter)
    for (w, t), _n in all_pairs.items():
        bytex[w][t] = len(tex_rooms[t])
    for w, _n in all_rms.most_common():
        print("  rm=0x%08x" % w)
        for t, nrooms in bytex[w].most_common():
            try:
                desc = texheader.describe(t, rom, trows)
            except Exception:
                desc = "tex%-5d (not in image list)" % t
            print("      %2d rooms  %s" % (nrooms, desc))

    if bad:
        print("\n%d blob(s) failed to inflate -- size derivation may be off for those."
              % bad)


if __name__ == "__main__":
    lvl = sys.argv[1] if len(sys.argv) > 1 else "sevx"
    reg = sys.argv[2] if len(sys.argv) > 2 else "ntsc-final"
    report(lvl, reg)
