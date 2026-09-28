#!/usr/bin/env python3
"""D236 pass 24: decode GE texture pixels offline, straight from the ROM.

Ports the subset of `src/game/image.c` needed to get real pixels out of a
texture without building or running the game:

  * `texReadBits`            (image_bank.c:99)  -- MSB-first bit reader
  * `texInflateNonZlib`      (image.c:896)      -- per-image header loop
  * `texBuildLookup`         (image.c:1669)     -- colour lookup table
  * `texInflateHuffman`      (image.c:1434)     -- huffman tree + decode
  * `texInflateRle`          (image.c:1596)     -- RLE directives
  * `texInflateLookupFromBuffer` (image.c:2090) -- indices -> pixels

That covers compmethods UNCOMP0/1 (trivially), LOOKUP, HUFFLOOKUP and
RLELOOKUP.  The channel-based methods (HUFFMAN, HUFFMANPERCHANNEL, RLE,
*BLUR) need `texChannelsToPixels` and are not implemented -- `decode()`
raises for those rather than returning something plausible-looking.

The point of this tool is the ALPHA channel: D236's tree cards are
`TEXFORMAT_RGBA16` = RGBA5551, and `g_TexFormatHas1BitAlpha[RGBA16] == 1`
(image.c:90), so their silhouette lives entirely in one bit per texel.

Usage:
    python tools_pc/texdecode.py 1198            # all LODs, alpha art
    python tools_pc/texdecode.py 1198 --lod 0 --ppm out.ppm
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools_pc"))
import texheader  # noqa: E402

# image.c per-format tables
NUM_CHANNELS = [4, 3, 3, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1]
HAS_1BIT_ALPHA = [0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]
CHANNEL_SIZES = [0x100, 0x20, 0x100, 0x20, 0x100, 0x10, 8, 0x100, 0x10,
                 0x100, 0x10, 0x100, 0x10]
BITS_PER_PIXEL = [0x20, 0x10, 0x18, 0x0F, 0x10, 8, 4, 8, 4, 0x10, 0x10, 0x10, 0x10]

F_RGBA32, F_RGBA16, F_RGB24, F_RGB15, F_IA16, F_IA8, F_IA4, F_I8, F_I4 = range(9)
LOOKUP_METHODS = {5: "LOOKUP", 6: "HUFFLOOKUP", 7: "RLELOOKUP"}


class Bits(object):
    """texSetBitstring / texReadBits."""

    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos
        self.acc = 0
        self.n = 0

    def align_after_image(self):
        """image.c:1096 -- at the end of each LOD image the bitstream is
        byte-aligned: any partial byte is discarded, and if the reader happens
        to sit exactly on a boundary a whole byte is skipped anyway.  Omitting
        this desynchronises every LOD after the first (found as a constant
        +8-bit drift while walking tex1198)."""
        if self.n == 0:
            self.p += 1
        else:
            self.n = 0

    def read(self, count):
        while self.n < count:
            self.acc = (self.d[self.p] | (self.acc << 8)) & 0xFFFFFFFFFFFF
            self.p += 1
            self.n += 8
        self.n -= count
        return (self.acc >> self.n) & ((1 << count) - 1)


def build_lookup(bs, bitsperpixel):
    """texBuildLookup -> (entries, count)."""
    n = bs.read(11)
    if bitsperpixel <= 24:
        return [bs.read(bitsperpixel) for _ in range(n)], n
    return [(bs.read(24) << 8) | bs.read(bitsperpixel - 24) for _ in range(n)], n


def inflate_huffman(bs, numiterations, chansize):
    """texInflateHuffman -- ported branch for branch."""
    NB = 2048
    freq = [0] * NB
    nodes = [[-1, -1] for _ in range(NB)]
    for i in range(chansize):
        freq[i] = bs.read(8)

    def two_smallest():
        f1 = f2 = 9999
        i1 = i2 = -1
        for i in range(chansize):
            if freq[i] < f1:
                if f2 < f1:
                    f1, i1 = freq[i], i
                else:
                    f2, i2 = freq[i], i
            elif freq[i] < f2:
                f2, i2 = freq[i], i
        return f1, i1, f2, i2

    f1, i1, f2, i2 = two_smallest()
    rootindex = 0
    while not (f1 == 9999 or f2 == 9999):
        s = freq[i1] + freq[i2] or 1
        freq[i1] = 9999
        freq[i2] = 9999
        if nodes[i1][0] < 0 and nodes[i1][1] < 0:
            nodes[i1][0] = i1 + 10000
            rootindex = i1
            freq[i1] = s
            nodes[i1][1] = (i2 + 10000) if (nodes[i2][0] < 0 and nodes[i2][1] < 0) else i2
        elif nodes[i2][0] < 0 and nodes[i2][1] < 0:
            nodes[i2][0] = i2 + 10000
            rootindex = i2
            freq[i2] = s
            nodes[i2][1] = (i1 + 10000) if (nodes[i1][0] < 0 and nodes[i1][1] < 0) else i1
        else:
            rootindex = 0
            while nodes[rootindex][0] >= 0 or nodes[rootindex][1] >= 0 or freq[rootindex] < 9999:
                rootindex += 1
            freq[rootindex] = s
            nodes[rootindex][0] = i1
            nodes[rootindex][1] = i2
        f1, i1, f2, i2 = two_smallest()

    out = []
    for _ in range(numiterations):
        v = rootindex
        while v < 10000:
            v = nodes[v][bs.read(1)]
        out.append(v - 10000)
    return out


def inflate_rle(bs, blockstotal):
    """texInflateRle (image.c:1596) -- ported exactly.

    Header: 3 bits backtrack-field size, 3 bits runlen-field size, 4 bits
    block size.  Then directives: a 0 bit introduces a literal block; a 1 bit
    introduces a run (backtrack distance, run length), and a run is ALWAYS
    followed by one literal with no marker bit of its own.

    The `fudge` added to every run length is derived, not guessed -- short
    runs cost more than literals, so they are never encoded and the field is
    biased by however many blocks the directive header itself costs.
    """
    btfieldsize = bs.read(3)
    rlfieldsize = bs.read(3)
    blocksize = bs.read(4)
    cost = btfieldsize + rlfieldsize + blocksize + 1
    fudge = 0
    while cost > 0:
        cost -= blocksize + 1
        fudge += 1

    out = []
    guard = blockstotal * 4 + 64
    while len(out) < blockstotal and guard > 0:
        guard -= 1
        if bs.read(1) == 0:
            out.append(bs.read(blocksize))
        else:
            start = len(out) - bs.read(btfieldsize) - 1
            runlen = bs.read(rlfieldsize) + fudge
            for k in range(runlen):
                out.append(out[start + k] if 0 <= start + k < len(out) else 0)
            out.append(bs.read(blocksize))  # a run is always followed by a literal
    return out[:blockstotal]


def _stride16(width):
    return (width + 3) & 0xFFC


def decode_image(bs, fmt, width, height, compmethod):
    """Return (rows, kind) where rows is a list of per-row pixel lists."""
    if compmethod not in LOOKUP_METHODS:
        raise NotImplementedError(
            "compmethod %d needs texChannelsToPixels, not ported" % compmethod)
    lookup, ncol = build_lookup(bs, BITS_PER_PIXEL[fmt])
    npix = width * height
    if compmethod == 6:
        idx = inflate_huffman(bs, npix, ncol)
    elif compmethod == 7:
        idx = inflate_rle(bs, npix)
    else:  # plain LOOKUP: indices are read straight off the bitstream
        # texGetBitSize(ncol) == (ncol-1).bit_length(), and is 0 for a
        # single-colour table -- do NOT clamp to 1, that desynchronises.
        nbits = (ncol - 1).bit_length()
        idx = [bs.read(nbits) for _ in range(npix)]
    rows = []
    for y in range(height):
        row = [lookup[i] if i < len(lookup) else 0
               for i in idx[y * width:(y + 1) * width]]
        rows.append(row)
    return rows


def decode(num, region="ntsc-final", max_lods=None):
    """Decode every LOD image of one texture. Returns (header, [image, ...])."""
    rom, rows = texheader.load(region)
    off, size = rows[num]
    blob = rom[off:off + size]
    hdr = texheader.parse(blob)
    if hdr["zlib"]:
        raise NotImplementedError("zlib/paletted textures not handled here")
    bs = Bits(blob, 1)
    numimages = hdr["lods"] if hdr["explicit_lods"] else 1
    if max_lods:
        numimages = min(numimages, max_lods)
    imgs = []
    for _ in range(numimages):
        fmt = bs.read(4)
        w = bs.read(8)
        h = bs.read(8)
        cm = bs.read(4)
        if w * h > 0x2000 or w == 0 or h == 0:
            break
        try:
            rows_ = decode_image(bs, fmt, w, h, cm)
        except NotImplementedError as e:
            # Later LODs often switch to a channel-based codec; stop cleanly
            # and report, rather than pretending the chain ended here.
            imgs.append({"format": fmt, "width": w, "height": h,
                         "compmethod": cm, "rows": None, "error": str(e)})
            break
        imgs.append({"format": fmt, "width": w, "height": h, "compmethod": cm,
                     "rows": rows_})
        bs.align_after_image()
    return hdr, imgs


def alpha_art(img):
    """ASCII art of the 1-bit alpha channel of an RGBA5551 image."""
    if img["format"] != F_RGBA16:
        return None
    return ["".join("#" if (p & 1) else "." for p in row) for row in img["rows"]]


def rgb_art(img):
    """Coarse luminance art, to sanity-check the colour channels."""
    ramp = " .:-=+*#%@"
    out = []
    for row in img["rows"]:
        line = ""
        for p in row:
            r, g, b = (p >> 11) & 31, (p >> 6) & 31, (p >> 1) & 31
            line += ramp[min(9, (r + g + b) * 10 // 96)]
        out.append(line)
    return out


def write_ppm(img, path):
    w, h = img["width"], img["height"]
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        for row in img["rows"]:
            for p in row:
                r, g, b, a = (p >> 11) & 31, (p >> 6) & 31, (p >> 1) & 31, p & 1
                # transparent texels rendered magenta so the silhouette is obvious
                f.write(bytes((r << 3, g << 3, b << 3)) if a else b"\xff\x00\xff")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    num = int(argv[1], 0)
    only = None
    ppm = None
    if "--lod" in argv:
        only = int(argv[argv.index("--lod") + 1])
    if "--ppm" in argv:
        ppm = argv[argv.index("--ppm") + 1]
    hdr, imgs = decode(num)
    print("tex%d: explicit_lods=%d lods=%d -> decoded %d image(s)"
          % (num, hdr["explicit_lods"], hdr["lods"], len(imgs)))
    for i, img in enumerate(imgs):
        if only is not None and i != only:
            continue
        print("\n  LOD %d: %s %dx%d compmethod=%s"
              % (i, texheader.TEXFORMAT.get(img["format"], img["format"]),
                 img["width"], img["height"],
                 texheader.COMPMETHOD.get(img["compmethod"], img["compmethod"])))
        if img["rows"] is None:
            print("    NOT DECODED: %s" % img["error"])
            continue
        a = alpha_art(img)
        if a:
            opaque = sum(r.count("#") for r in a)
            total = img["width"] * img["height"]
            print("    alpha: %d/%d opaque (%.1f%%)  ['#'=opaque '.'=transparent]"
                  % (opaque, total, 100.0 * opaque / total))
            for r in a:
                print("    " + r)
        else:
            for r in rgb_art(img):
                print("    " + r)
        if ppm and (only is None or i == only):
            write_ppm(img, ppm)
            print("    wrote %s" % ppm)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
