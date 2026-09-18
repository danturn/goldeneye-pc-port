#!/usr/bin/env python3
"""
gen_romassets.py — generate absolute ROM asset symbols for the PC port.

For a given region this emits a GAS file (port/src/romassets_<r>.s) defining
every ROM asset symbol as an ABSOLUTE cart address (0x10000000 + rom_offset),
matching the N64 linker layout (ge007.ld). The PC port maps the loaded .z64
into host memory at exactly 0x10000000 (see port/src/romdata.c), so:

  * `&symbol` in game code yields the cart address, and romCopy()/
    osPiStartDma(OS_READ, ...) shims memcpy from that address (Phase 1).
  * file_resource_table.inc.c entries stay in ROM order, so ob.c's
    `table[i+1].hw_address - table[i].hw_address` size math is correct.

Sources of truth (all in-repo, N64 build untouched):
  scripts/filelist.<r>.csv                rom offset + size per asset file
  assets/obseg/file_resource_table.inc.c  ordered obseg symbol list
  assets/ramrom/ramrom.s                  region-aware (.ifdef/.else/.endif)
  assets/music/{music,sfx.ctl,sfx.tbl,instruments.ctl,instruments.tbl}.s
  ge007.ld                                segment markers (BEGIN_SEG/END_SEG)

Code segments (header/boot/start/code/cdata/inflate/game) have no CSV entry;
their offsets are best-effort values derived from the disassembly comments in
ge007.ld and are marked as such. They are only consumed by the N64 boot path,
which the PC port does not run.

Usage:
  python3 scripts/gen_romassets.py u          # -> port/src/romassets_u.s
  python3 scripts/gen_romassets.py e          # (EU manifest quirks: see docs)
"""

import os
import re
import sys

CART_BASE = 0x10000000
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Absolute symbols inside the "virtual" DRAM region (port/src/dram.c maps one
# 8 MB backing store at BOTH V1 0x70000000 and the KSEG0 mirror 0x80000000).
# These are N64/DRAM offsets; --base (PORT_ADDR_BASE) is added on Darwin.
DRAM_SYMS = [
    ("cfb_16", 0x70000000,
     "replaces src/cfb.c (excluded from the build): the two 320x240x16-bit\n"
     " * framebuffers, 0x4B000 bytes total."),
    ("_bssSegmentEnd", 0x70050000,
     "end-of-.bss marker; boss.c:217 starts the mempools at\n"
     " * PHYS_TO_K0(osVirtualToPhysical(&_bssSegmentEnd))."),
    ("animations_frame_buffer", 0x707FFD30,
     "D59: animation-frame scratch buffer (src/game/initanitable.c). The game\n"
     " * addresses it through s32 fields (Model.unk34/38/64/68) and\n"
     " * loadAnimationFrame computes its address with a (u32) cast, so on PC it\n"
     " * must live below 4 GiB; the BSS copy at 0x140xxxxxx truncates to wild\n"
     " * 0x40xxxxxx. Parked at the top of DRAM V1 (above the mempool sentinel\n"
     " * at +0x2F4400)."),
]


def die(msg):
    print(f"gen_romassets: ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


# --------------------------------------------------------------------------
# CSV manifest
# --------------------------------------------------------------------------

class Csv:
    def __init__(self, region):
        path = os.path.join(ROOT, "scripts", f"filelist.{region}.csv")
        self.by_name = {}   # basename -> (offset, size, fullpath)
        self.by_path = {}   # fullpath  -> (offset, size)
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.strip().split(",")
                if len(parts) < 3:
                    continue
                try:
                    off, size = int(parts[0]), int(parts[1])
                except ValueError:
                    continue
                full = parts[2]
                name = full.rsplit("/", 1)[-1]
                self.by_name[name] = (off, size, full)
                self.by_path[full] = (off, size)
                # Scanner-prefixed names: ge007.<r>.<hexoffset>.<realname>
                m = re.match(r"^ge007\.[uje]\.[0-9a-fA-F]+\.(.+)$", name)
                if m:
                    self.by_name.setdefault(m.group(1), (off, size, full))

    def lookup(self, candidates):
        """Try candidate basenames in order; return (offset, size, matched)."""
        for cand in candidates:
            if cand in self.by_name:
                off, size, full = self.by_name[cand]
                return off, size, cand
            # also allow an exact full-path hit (dir-qualified lookups)
            if cand.startswith("assets/") and cand in self.by_path:
                off, size = self.by_path[cand]
                return off, size, cand
        return None

    @staticmethod
    def name_candidates(basename):
        """Manifest names are inconsistent across regions: with/without .bin,
        trailing Z added/dropped. Try the plain name first, then variants."""
        cands = [basename]
        stem = re.sub(r"\.(bin|seg)$", "", basename)
        if stem != basename:
            cands.append(stem)
        else:
            cands.append(basename + ".bin")  # CSV names carry .bin
        for s in (stem, stem[:-1] if stem.endswith("Z") else stem + "Z"):
            cands.append(s)                  # trailing Z added/dropped
            cands.append(s + ".bin")
            cands.append(s + ".seg")          # ob__ob_end.seg style
        return cands


# --------------------------------------------------------------------------
# Region-aware .s parsing (ob_seg.s / ramrom.s)
# --------------------------------------------------------------------------

class RegionParser:
    """Parses asset .s files, honouring .ifdef VERSION_XX / .else / .endif.

    Yields, in file order, either:
      ("file", symbol, incbin_path)   — a .global label + .incbin pair
      ("label", symbol)               — a bare label (segment marker etc.)
    for the requested target region.
    """

    COND_RE = re.compile(r"^\s*\.ifdef\s+(VERSION_\w+)")
    GLOBAL_RE = re.compile(r"^\s*\.global\s+(\w+)")
    LABEL_RE = re.compile(r"^(\w+):")
    INCBIN_RE = re.compile(r'^\s*\.incbin\s+"([^"]+)"')

    def __init__(self, target_region):  # "u" | "e" | "j"
        self.target = {"u": "VERSION_US", "e": "VERSION_EU",
                       "j": "VERSION_JP"}[target_region]
        self.stack = []   # entries: (cond, in_else)

    def _active(self):
        for cond, in_else in self.stack:
            if (cond == self.target) != (not in_else):
                return False
        return True

    def parse(self, path):
        """State: a `.global X` arms `pending`; the following `X:` label moves
        it to `expecting` (its own label line); an `.incbin` then makes it a
        file entry, otherwise the next `.global` flushes it as a bare marker."""
        out = []
        pending = None    # symbol from .global, label line not yet seen
        expecting = None  # label seen; waiting for .incbin (file) or next
                          # .global (bare marker)
        with open(path, encoding="utf-8", errors="replace") as f:
            for raw in f:
                line = raw.rstrip("\n")
                m = re.match(r"^\s*\.ifdef\s+(\w+)", line)
                if m:
                    self.stack.append((m.group(1), False))
                    continue
                if re.match(r"^\s*\.else\b", line):
                    if self.stack:
                        cond, _ = self.stack[-1]
                        self.stack[-1] = (cond, True)
                    continue
                if re.match(r"^\s*\.endif\b", line):
                    if self.stack:
                        self.stack.pop()
                    continue
                if not self._active():
                    continue
                m = self.GLOBAL_RE.match(line)
                if m:
                    if expecting is not None:
                        out.append(("label", expecting))
                    expecting = None
                    pending = m.group(1)
                    continue
                m = self.INCBIN_RE.match(line)
                if m and expecting is not None:
                    out.append(("file", expecting, m.group(1)))
                    expecting = None
                    continue
                m = self.LABEL_RE.match(line)
                if m:
                    sym = m.group(1)
                    if sym == pending:
                        expecting = sym
                        pending = None
                    else:
                        out.append(("label", sym))
        if expecting is not None:
            out.append(("label", expecting))
        return out


# --------------------------------------------------------------------------
# Per-source extraction
# --------------------------------------------------------------------------

def parse_resource_table(region):
    """assets/obseg/file_resource_table.inc.c -> ordered [(symbol, pathstr)].

    The table is C: entries for other regions are wrapped in
    `#ifdef VERSION_XX` (e.g. all L*P text rows under VERSION_EU). Honour
    those conditionals for the target region."""
    path = os.path.join(ROOT, "assets", "obseg", "file_resource_table.inc.c")
    cond_region = {"VERSION_US": "u", "VERSION_EU": "e", "VERSION_JP": "j"}
    entries = []
    seen = set()
    stack = []   # condition names (only VERSION_* occur in this file)
    rx = re.compile(r'\{\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*&(\w+)\s*\}')
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^\s*#ifdef\s+(\w+)", line)
            if m:
                stack.append(m.group(1))
                continue
            if re.match(r"^\s*#endif\b", line):
                if stack:
                    stack.pop()
                continue
            active = all(cond_region.get(c, "?") == region for c in stack)
            if not active:
                continue
            m = rx.search(line)
            if not m:
                continue
            sym = m.group(3)
            if sym in seen:
                continue
            seen.add(sym)
            entries.append((sym, m.group(2)))
    return entries


def resolve_obseg(entries, csv):
    """Map each resource-table symbol to (offset, size)."""
    resolved = []   # (symbol, offset, size, matched_name)
    missing = []
    for sym, pathstr in entries:
        base = pathstr.rsplit("/", 1)[-1]
        hit = csv.lookup(Csv.name_candidates(base))
        if hit is None:
            # fall back to the symbol name itself (pathstr may be stale,
            # e.g. BG_ELD -> bg_imp_all_p_seg; strip a trailing _seg)
            cands = Csv.name_candidates(sym)
            if sym.endswith("_seg"):
                cands += Csv.name_candidates(sym[:-4])
            hit = csv.lookup(cands)
        if hit is None:
            missing.append((sym, pathstr))
            continue
        off, size, matched = hit
        resolved.append((sym, off, size, matched))
    return resolved, missing


def parse_ramrom_region(region, csv):
    rp = RegionParser(region)
    path = os.path.join(ROOT, "assets", "ramrom", "ramrom.s")
    items = rp.parse(path)
    files = []   # (symbol, offset, size)
    markers = {}  # bare label -> index of next file symbol (resolved later)
    pending_marker = None
    for kind, a, *rest in items:
        if kind == "file":
            sym, incbin = a, rest[0]
            base = incbin.rsplit("/", 1)[-1]
            hit = csv.lookup(Csv.name_candidates(base))
            if hit is None:
                die(f"ramrom: no CSV entry for {sym} ({incbin})")
            off, size, _ = hit
            files.append((sym, off, size))
            pending_marker = None
        else:  # bare label
            if a.endswith("_end"):
                continue  # handled via start+size below
            markers[a] = len(files)  # value = offset of next file
    return files, markers


def parse_music_tracks(region, csv):
    """music.s: music_file <name> lines (region blocks inside the macro)."""
    path = os.path.join(ROOT, "assets", "music", "music.s")
    tracks = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"^\s*music_file\s+(\w+)", line)
            if m:
                tracks.append(m.group(1))
    resolved = []
    for name in tracks:
        hit = csv.lookup(Csv.name_candidates(f"{name}.bin"))
        if hit is None:
            die(f"music: no CSV entry for track {name}")
        off, size, _ = hit
        resolved.append((name, off, size))
    return resolved


def parse_music_sections(csv):
    """sfx.ctl.s / sfx.tbl.s / instruments.ctl.s / instruments.tbl.s:
    each defines _<x>SegmentRomStart + incbin + _<x>SegmentRomEnd."""
    out = []
    for stem in ("sfx.ctl", "sfx.tbl", "instruments.ctl", "instruments.tbl"):
        path = os.path.join(ROOT, "assets", "music", f"{stem}.s")
        start_sym = end_sym = incbin = None
        with open(path, encoding="utf-8") as f:
            for line in f:
                m = re.match(r"^\s*\.global\s+(\w+)", line)
                if m:
                    sym = m.group(1)
                    if sym.endswith("SegmentRomStart"):
                        start_sym = sym
                    elif sym.endswith("SegmentRomEnd"):
                        end_sym = sym
                    continue
                m = re.match(r'^\s*\.incbin\s+"([^"]+)"', line)
                if m:
                    incbin = m.group(1)
        if not (start_sym and end_sym and incbin):
            die(f"music section {stem}: expected start/end/incbin, got "
                f"{start_sym}/{end_sym}/{incbin}")
        base = incbin.rsplit("/", 1)[-1]
        hit = csv.lookup(Csv.name_candidates(base))
        if hit is None:
            die(f"music section {stem}: no CSV entry for {incbin}")
        off, size, _ = hit
        out.append((start_sym, end_sym, off, size))
    return out


# --------------------------------------------------------------------------
# Linker-script segments (ge007.ld)
# --------------------------------------------------------------------------

def build_segments(region, csv, obseg_resolved, ramrom_files, music_tracks,
                  music_sections, rom_size):
    """Assign (offset, size) to every BEGIN_SEG segment in ge007.ld.

    Asset-backed segments come from the CSV (exact). Code segments use
    best-effort values from the disassembly comments in ge007.ld.
    """
    seg = {}  # name -> (offset, size, exact?)

    def csv1(basename):
        hit = csv.lookup(Csv.name_candidates(basename))
        if hit is None:
            die(f"segment lookup failed for {basename}")
        return hit[0], hit[1]

    # --- code segments (approximate; N64 boot path only) -----------------
    seg["header"] = (0x0, 0x40, False)
    seg["boot"] = (0x40, 0x1000 - 0x40, False)        # start @0x1000 assumed
    seg["start"] = (0x1000, 0x50, False)              # RAM size 0x50 (ld cmt)
    seg["code"] = (0x1050, 0x21990 - 0x1050, False)   # ld comment 001050-021990
    # inflate size 0x29BD (ld comment [29BD]); game ends at fontdl start.
    fontdl_off, _ = csv1("jfont_dl.bin")
    game_end = fontdl_off                              # ld: fontdl @ game end
    game_size = 0xE2D51                                # ld comment [E2D51]
    seg["game"] = (game_end - game_size, game_size, False)
    inflate_size = 0x29BD
    seg["inflate"] = (seg["game"][0] - inflate_size, inflate_size, False)
    seg["cdata"] = (seg["code"][0] + seg["code"][1],
                    seg["inflate"][0] - (seg["code"][0] + seg["code"][1]),
                    False)

    # --- asset segments (exact, from CSV) --------------------------------
    off, size = csv1("jfont_dl.bin")
    seg["fontdl"] = (off, size, True)
    off, size = csv1("jfont_chardata.bin")
    seg["jfontchardata"] = (off, size, True)
    off, size = csv1("efont_chardata.bin")
    seg["efontchardata"] = (off, size, True)
    off, size = csv1("animationtable_entries.bin")
    seg["animation_entries"] = (off, size, True)
    off, size = csv1("animationtable_data.bin")
    seg["animation_data"] = (off, size, True)
    off, size = csv1("Globalimagetable.bin")
    # D39 (docs/internals.md): the CSV asset is truncated to the 17 Gfx
    # display lists (0xAC8). The N64 linker segment (ge007.ld) places ALL of
    # oddtextures.o (.data) here — Gfx DLs + 32 sImageTableEntry tables =
    # 0x13F8 bytes. texReset() copies End-Start, so the marker must span the
    # full linked .data or the image tables are never loaded. Verified by
    # byte-tiling all 49 symbols against ROM [off, off+0x13F8).
    seg["Globalimagetable"] = (off, 0x13F8, True)
    off, size = csv1("rarewarelogo.bin")
    seg["rarewarelogo"] = (off, size, True)
    off, size = csv1("usedby7F008DE4.bin")             # assets/romfiles2.s blob
    seg["romfiles2"] = (off, size, True)

    if ramrom_files:
        first = min(o for _, o, _ in ramrom_files)
        last_end = max(o + s for _, o, s in ramrom_files)
        seg["ramromfiles"] = (first, last_end - first, True)

    # font objects = kerning + chartable, contiguous in ROM (verified).
    k_off, k_size = csv1("fontBankGothic_kerning.bin")
    c_off, c_size = csv1("fontBankGothic_fontchartable.bin")
    if k_off + k_size != c_off:
        die("fontBankGothic kerning/chartable not contiguous in CSV")
    seg["fontbankgothic"] = (k_off, k_size + c_size, True)
    k_off, k_size = csv1("fontZurichBold_kerning.bin")
    c_off, c_size = csv1("fontZurichBold_fontchartable.bin")
    if k_off + k_size != c_off:
        die("fontZurichBold kerning/chartable not contiguous in CSV")
    seg["fontzurichbold"] = (k_off, k_size + c_size, True)

    # musicfiles = sfx.ctl .. last M* track end.
    m_off = min(o for _, _, o, _ in music_sections)
    m_end = max(o + s for _, o, s in music_tracks)
    seg["musicfiles"] = (m_off, m_end - m_off, True)

    # obseg = first resource-table symbol .. end of ob__ob_end_seg.
    if obseg_resolved:
        first = min(o for _, o, _, _ in obseg_resolved)
        last_end = max(o + s for _, o, s, _ in obseg_resolved)
        seg["obseg"] = (first, last_end - first, True)

    # images = everything after obseg to the end of the ROM.
    if "obseg" in seg:
        img_off = seg["obseg"][0] + seg["obseg"][1]
        seg["images"] = (img_off, rom_size - img_off, True)

    return seg


# --------------------------------------------------------------------------
# Emission
# --------------------------------------------------------------------------

def emit(region, csv, rom_size, darwin=False, base=0, out=None):
    lines = []
    ap = lines.append
    ap("/*")
    ap(f" * AUTO-GENERATED by scripts/gen_romassets.py ({region.upper()}) -- do not edit.")
    ap(" *")
    ap(" * Absolute ROM asset symbols for the PC port. Each symbol is the N64")
    ap(" * cart address (0x10000000 + rom_offset) of the corresponding asset in")
    ap(f" * scripts/filelist.{region}.csv, so `&symbol` / romCopy() work exactly as")
    ap(" * on hardware once romdata.c has mapped the .z64 at 0x10000000.")
    if darwin:
        ap(" *")
        ap(" * Mach-O/Darwin variant: `.data` section, `_`-prefixed symbols, and")
        ap(f" * every value biased by --base (0x{base:X}) so the N64 address space")
        ap(" * lives at PORT_ADDR_BASE in the host address space (see")
        ap(" * port/include/port_addr.h and docs/dev/MACOS-ARM64-PLAN.md).")
    ap(f" * Regenerate: python3 scripts/gen_romassets.py {region}"
       + (f" --darwin --base 0x{base:X}" if darwin else ""))
    ap(" */")
    # Mach-O assembles `.data`; ELF wants `.section .data`. `_` prefix on every
    # symbol: Darwin's C symbol convention (game code references the bare name,
    # the toolchain adds the underscore).
    ap(".data" if darwin else ".section .data")
    ap("")

    def sym(name, value):
        n = ("_" + name) if darwin else name
        ap(f".globl {n}" if darwin else f".global {n}")
        ap(f".set {n}, 0x{base + value:X}")

    # --- obseg (file_resource_table order = ROM order) -------------------
    entries = parse_resource_table(region)
    resolved, missing = resolve_obseg(entries, csv)
    if missing:
        for s, p in missing:
            print(f"gen_romassets: MISSING {s} ({p})", file=sys.stderr)
        die(f"{len(missing)} obseg symbols unresolved")

    # Invariants the game relies on (ob.c size math = table[i+1]-table[i]).
    # Offsets must be non-decreasing; zero-size placeholder files may share
    # an offset (bg_ash/bg_imp/bg_sho all sit at one address in the US ROM).
    for i in range(1, len(resolved)):
        prev_sym, prev_off, prev_size, _ = resolved[i - 1]
        sym2, off2, _, _ = resolved[i]
        if off2 < prev_off:
            die(f"resource table not in ROM order: {prev_sym} ({prev_off:#x}) "
                f"> {sym2} ({off2:#x})")
        if prev_off + prev_size > off2:
            print(f"gen_romassets: WARNING overlap {prev_sym}+{prev_size} "
                  f"> {sym2}", file=sys.stderr)

    ap("/* --- assets/obseg (file_resource_table.inc.c order) ------------- */")
    for symname, off, size, matched in resolved:
        sym(symname, CART_BASE + off)
        sym(f"end_{symname}", CART_BASE + off + size)
    ap("")

    # --- ramrom -----------------------------------------------------------
    ramrom_files, ramrom_markers = parse_ramrom_region(region, csv)
    if ramrom_files:
        ap("/* --- assets/ramrom (demo replays) ---------------------------- */")
        for i, (symname, off, size) in enumerate(ramrom_files):
            sym(symname, CART_BASE + off)
            sym(f"{symname}_end", CART_BASE + off + size)
        for marker, idx in ramrom_markers.items():
            if idx < len(ramrom_files):
                sym(marker, CART_BASE + ramrom_files[idx][1])
        ap("")

    # --- music ------------------------------------------------------------
    tracks = parse_music_tracks(region, csv)
    sections = parse_music_sections(csv)
    hit = csv.lookup(Csv.name_candidates("number_music_samples"))
    if hit is None:
        die("music: no CSV entry for number_music_samples")
    sample_tbl_off = hit[0]
    first_track_off = min(o for _, o, _ in tracks)

    ap("/* --- assets/music ------------------------------------------------ */")
    sym("_musicsampletblSegmentRomStart", CART_BASE + sample_tbl_off)
    sym("number_music_samples", CART_BASE + sample_tbl_off)
    sym("number_music_samples_end", CART_BASE + sample_tbl_off + 4)
    sym("table_music_data", CART_BASE + sample_tbl_off + 4)
    sym("_musicsampletblSegmentRomEnd", CART_BASE + first_track_off)
    sym("table_music_data_end", CART_BASE + first_track_off)
    for start_sym, end_sym, off, size in sections:
        sym(start_sym, CART_BASE + off)
        sym(end_sym, CART_BASE + off + size)
    for name, off, size in tracks:
        sym(name, CART_BASE + off)
        sym(f"end_{name}", CART_BASE + off + size)
    ap("")

    # --- romfiles2 ---------------------------------------------------------
    hit = csv.lookup(Csv.name_candidates("usedby7F008DE4.bin"))
    if hit is not None:
        unknown2_off, unknown2_size = hit[0], hit[1]
        ap("/* --- assets/romfiles2.s -------------------------------------- */")
        sym("unknown2", CART_BASE + unknown2_off)
        sym("unknown2_end", CART_BASE + unknown2_off + unknown2_size)
        ap("")

    # --- linker-script segment markers -------------------------------------
    segs = build_segments(region, csv, resolved, ramrom_files, tracks,
                          sections, rom_size)
    ap("/* --- ge007.ld segment markers ------------------------------------ */")
    ap("/* RomStart/RomEnd are exact for asset segments; Start/End mirror them")
    ap("   (game code only ever uses End-Start size differences for these).")
    ap("   Code segments are approximate (see generator header comment). */")
    for name in ("header", "boot", "start", "code", "cdata", "inflate",
                 "game", "fontdl", "jfontchardata", "efontchardata",
                 "animation_entries", "animation_data", "Globalimagetable",
                 "rarewarelogo", "romfiles2", "ramromfiles", "fontbankgothic",
                 "fontzurichbold", "musicfiles", "obseg", "images"):
        if name not in segs:
            continue
        off, size, exact = segs[name]
        note = "" if exact else "  /* approximate */"
        sym(f"_{name}SegmentRomStart", CART_BASE + off)
        sym(f"_{name}SegmentRomEnd", CART_BASE + off + size)
        sym(f"_{name}SegmentStart", CART_BASE + off)
        sym(f"_{name}SegmentEnd", CART_BASE + off + size)
        if note:
            lines[-1] += note
    # alt-start (ld: _startSegmentRomStart + 0x100000)
    s_off = segs["start"][0]
    sym("_alt_startSegmentRomStart", CART_BASE + s_off + 0x100000)
    sym("_alt_startSegmentStart", CART_BASE + s_off + 0x100000)

    out = out or os.path.join(ROOT, "port", "src", f"romassets_{region}.s")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    n_syms = sum(1 for l in lines if l.startswith(".set "))
    print(f"gen_romassets: wrote {os.path.relpath(out, ROOT) if out.startswith(ROOT) else out} "
          f"({n_syms} symbols, obseg={len(resolved)}, ramrom={len(ramrom_files)}, "
          f"tracks={len(tracks)}, segments={len(segs)})")


def emit_dram(darwin=False, base=0, out=None):
    """Emit the DRAM-region absolute symbols (see DRAM_SYMS) in ELF or Mach-O
    syntax. Same --base biasing as emit()."""
    lines = []
    ap = lines.append
    ap("/*")
    ap(" * AUTO-GENERATED by scripts/gen_romassets.py --dram-out -- do not edit.")
    ap(" *")
    ap(" * Absolute symbols inside the \"virtual\" (s32-safe) DRAM view. See")
    ap(" * port/src/dram.c: one 8 MB backing store is mapped at BOTH 0x70000000")
    ap(" * (V1, where these live) and 0x80000000 (V2, the KSEG0 mirror). These are")
    ap(" * committed host addresses, so they are live pointers on the PC.")
    if darwin:
        ap(" *")
        ap(f" * Mach-O/Darwin variant: `.data`, `_`-prefixed symbols, +0x{base:X} base.")
    ap(" * Regenerate: python3 scripts/gen_romassets.py <region> --dram-out "
       "port/src/dram_syms.s" + (f" --darwin --base 0x{base:X}" if darwin else ""))
    ap(" */")
    ap(".data" if darwin else ".section .data")
    ap("")
    for name, value, note in DRAM_SYMS:
        ap(f"/* {note} */")
        n = ("_" + name) if darwin else name
        ap(f".globl {n}" if darwin else f".global {n}")
        ap(f".set {n}, 0x{base + value:X}")
        ap("")
    out = out or os.path.join(ROOT, "port", "src", "dram_syms.s")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"gen_romassets: wrote {os.path.relpath(out, ROOT) if out.startswith(ROOT) else out} "
          f"({len(DRAM_SYMS)} dram symbols)")


def main():
    import argparse
    p = argparse.ArgumentParser(
        description="Generate absolute ROM asset symbols for the PC port.")
    p.add_argument("region", choices=["u", "e", "j"], help="ROM region")
    p.add_argument("--darwin", action="store_true",
                   help="emit Mach-O syntax: `.data` section and `_`-prefixed "
                        "symbols (required for the macOS arm64 build)")
    p.add_argument("--base", type=lambda s: int(s, 0), default=0,
                   help="host address of the N64 address space, i.e. "
                        "PORT_ADDR_BASE (default 0; macOS uses 0x100000000000)")
    p.add_argument("--out", default=None,
                   help="output path (default port/src/romassets_<region>.s)")
    p.add_argument("--dram-out", default=None, metavar="PATH",
                   help="also emit the DRAM-region absolute symbols (cfb_16, "
                        "_bssSegmentEnd, animations_frame_buffer) to PATH")
    p.add_argument("--rom-size", type=lambda s: int(s, 0), default=None,
                   help="override the ROM size used for the `images` segment "
                        "end (normally read from baserom.<r>.z64; the US ROM "
                        "is 0xC00000). Only _imagesSegmentRomStart is consumed "
                        "at runtime, so this is cosmetic.")
    args = p.parse_args()
    region = args.region
    csv = Csv(region)

    rom_size = None
    rom_name = {"u": "baserom.u.z64", "e": "baserom.e.z64",
                "j": "baserom.j.z64"}[region]
    rom_path = os.path.join(ROOT, rom_name)
    if args.rom_size is not None:
        rom_size = args.rom_size
    elif os.path.exists(rom_path):
        rom_size = os.path.getsize(rom_path)
    else:
        print(f"gen_romassets: note: {rom_name} not found; "
              f"images segment sized from CSV max", file=sys.stderr)
        rom_size = max(o + s for o, s in csv.by_path.values())
    emit(region, csv, rom_size, args.darwin, args.base, args.out)
    if args.dram_out:
        emit_dram(args.darwin, args.base, args.dram_out)


if __name__ == "__main__":
    main()
