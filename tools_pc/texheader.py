#!/usr/bin/env python3
"""D236 pass 23: read GE texture headers straight out of the ROM, offline.

A texture's ROM blob starts with a control byte (`image.c:2568`):

    bit 7  hasExplicitLods   bit 6  iszlib   bits 0-5  lod (image count)

and the bitstream begins at byte 1.  Forgetting that control byte shifts
every subsequent field by 8 bits and yields a header that still *parses* --
plausible-looking formats and dimensions -- so `census()` below reports how
many entries come out implausible as a built-in sanity check (expect ~1 of
2699 for U; a few hundred means the offset is wrong again).

Two payload layouts follow, per `image.c`:
  * iszlib=0 -> `texInflateNonZlib`: a 24-bit bitstream header per image,
    `ffff wwwwwwww hhhhhhhh cccc` (format / width / height / compmethod).
  * iszlib=1 -> `texInflateZlib`: format u8, numcolours-1 u8, then the
    palette, then per-image width u8 / height u8.

Only headers are decoded here -- the pixel payloads use Rare's huffman/RLE/
lookup codecs, which are not implemented.

Usage:  python tools_pc/texheader.py [texnum ...]      (no args: census)
"""
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TEXFORMAT = {0: "RGBA32", 1: "RGBA16", 2: "RGB24", 3: "RGB15", 4: "IA16",
             5: "IA8", 6: "IA4", 7: "I8", 8: "I4", 9: "RGBA16_CI8",
             10: "RGBA16_CI4", 11: "IA16_CI8", 12: "IA16_CI4"}
COMPMETHOD = {0: "UNCOMP0", 1: "UNCOMP1", 2: "HUFFMAN", 3: "HUFFMANCHAN",
              4: "RLE", 5: "LOOKUP", 6: "HUFFLOOKUP", 7: "RLELOOKUP",
              8: "HUFFBLUR", 9: "RLEBLUR"}
# image.c g_TexFormatGbiMappings / g_TexFormatDepths -- what the runtime
# reports, i.e. what a live GE_TEXI/GE_D2xx probe prints as fmt=/siz=.
GBI_FMT = {0: 0, 1: 0, 2: 0, 3: 0, 4: 3, 5: 3, 6: 3, 7: 4, 8: 4,
           9: 2, 10: 2, 11: 2, 12: 2}
GBI_SIZ = {0: 3, 1: 2, 2: 3, 3: 2, 4: 2, 5: 1, 6: 0, 7: 1, 8: 0,
           9: 1, 10: 0, 11: 1, 12: 0}
GBI_FMT_NAME = {0: "RGBA", 2: "CI", 3: "IA", 4: "I"}


def load(region="ntsc-final"):
    """(rom bytes, [(offset, size), ...] indexed by texture number)."""
    rom = open(os.path.join(ROOT, "data", "ge007.%s.z64" % region), "rb").read()
    rows = []
    csv = os.path.join(ROOT, "imagelist.%s.csv" % ("u" if region.startswith("ntsc") else region[0]))
    for line in open(csv):
        if not line.strip():
            continue
        f = line.split(",")
        rows.append((int(f[0]), int(f[1])))
    return rom, rows


def parse(blob):
    ctrl = blob[0]
    d = {"lods": ctrl & 0x3F, "zlib": (ctrl >> 6) & 1, "explicit_lods": (ctrl >> 7) & 1}
    if d["zlib"]:
        d["format"] = blob[1]
        d["numcolours"] = blob[2] + 1
    else:
        v = (blob[1] << 16) | (blob[2] << 8) | blob[3]
        d["format"] = (v >> 20) & 0xF
        d["width"] = (v >> 12) & 0xFF
        d["height"] = (v >> 4) & 0xFF
        d["compmethod"] = v & 0xF
    f = d["format"]
    d["format_name"] = TEXFORMAT.get(f, "?%d" % f)
    d["gbi_fmt"] = GBI_FMT.get(f)
    d["gbi_siz"] = GBI_SIZ.get(f)
    return d


def implausible(d):
    if d["format"] > 12:
        return True
    if d["zlib"]:
        return False
    return d["compmethod"] > 9 or d["width"] == 0 or d["height"] == 0


def describe(num, rom, rows):
    off, size = rows[num]
    d = parse(rom[off:off + 5])
    live = "fmt=%d(%s) siz=%d" % (d["gbi_fmt"], GBI_FMT_NAME.get(d["gbi_fmt"], "?"), d["gbi_siz"])
    if d["zlib"]:
        body = "ZLIB    %-11s ncol=%-4d" % (d["format_name"], d["numcolours"])
    else:
        body = "NONZLIB %-11s %3dx%-3d %-11s" % (d["format_name"], d["width"], d["height"],
                                                 COMPMETHOD.get(d["compmethod"], d["compmethod"]))
    return "tex%-5d size=%-5d %s lods=%d explicit_lods=%d  -> live %s" % (
        num, size, body, d["lods"], d["explicit_lods"], live)


def census(rom, rows):
    fmts, bad = collections.Counter(), 0
    for num in range(len(rows)):
        off, size = rows[num]
        if size < 5:
            continue
        d = parse(rom[off:off + 5])
        fmts[("zlib " if d["zlib"] else "") + d["format_name"]] += 1
        if implausible(d):
            bad += 1
    return fmts, bad


if __name__ == "__main__":
    rom, rows = load()
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if args:
        for a in args:
            print(describe(int(a, 0), rom, rows))
    else:
        fmts, bad = census(rom, rows)
        print("%d textures, %d implausible headers (sanity check: expect ~1)" % (len(rows), bad))
        for name, n in fmts.most_common():
            print("  %-16s %5d" % (name, n))
