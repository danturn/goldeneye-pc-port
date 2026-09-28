#!/usr/bin/env python3
"""D236 pass 22: evaluate N64 render-mode words offline, straight from the repo.

Two jobs:

1. `decode(word)` -- turn a raw `G_SETOTHERMODE_L` value (the `oml` the live
   `GE_D2xx` probes print) into the flag names and blender equation it stands
   for, and name it if it matches a `G_RM_*` composite in `include/PR/gbi.h`.

2. `applied_luts()` -- evaluate the `DL_LUT_*` tables in `src/game/bg.c` into
   plain (from -> to) word maps, for the four LUT indices `bg.c` actually
   applies at room load (1/5/6/7).  This is what lets a *static* census of a
   bg file's authored render modes be compared against the words a *live*
   probe logs, which are the post-rewrite ones.

Everything is parsed from the real headers/sources rather than hardcoded, so
it cannot drift from the build.  No C compiler required.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GBI_H = os.path.join(ROOT, "include", "PR", "gbi.h")
BG_C = os.path.join(ROOT, "src", "game", "bg.c")

# Order matters for readable decoding: mask-style fields first.
_FIELDS = [
    ("CVG_DST", 0x300, {0x000: "CVG_DST_CLAMP", 0x100: "CVG_DST_WRAP",
                        0x200: "CVG_DST_FULL", 0x300: "CVG_DST_SAVE"}),
    ("ZMODE", 0xC00, {0x000: "ZMODE_OPA", 0x400: "ZMODE_INTER",
                      0x800: "ZMODE_XLU", 0xC00: "ZMODE_DEC"}),
]
_BITS = [(0x8, "AA_EN"), (0x10, "Z_CMP"), (0x20, "Z_UPD"), (0x40, "IM_RD"),
         (0x80, "CLR_ON_CVG"), (0x1000, "CVG_X_ALPHA"),
         (0x2000, "ALPHA_CVG_SEL"), (0x4000, "FORCE_BL")]

_BL_P = {0: "CLR_IN", 1: "CLR_MEM", 2: "CLR_BL", 3: "CLR_FOG"}
_BL_A = {0: "A_IN/1MA", 1: "A_FOG/A_MEM", 2: "A_SHADE/1", 3: "0"}


def _flag_defines():
    """Simple `#define NAME <int literal>` pairs from gbi.h."""
    out = {}
    for line in open(GBI_H, encoding="utf-8", errors="replace"):
        m = re.match(r"#define\s+(\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*(/\*.*)?$", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def _macro_bodies():
    """`#define NAME(args) body` and `#define NAME body` with continuations."""
    src = open(GBI_H, encoding="utf-8", errors="replace").read()
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = src.replace("\\\n", " ")
    fn, obj = {}, {}
    for line in src.split("\n"):
        m = re.match(r"#define\s+(\w+)\(([^)]*)\)\s+(.*)$", line)
        if m:
            fn[m.group(1)] = ([a.strip() for a in m.group(2).split(",")], m.group(3).strip())
            continue
        m = re.match(r"#define\s+(\w+)\s+(.+)$", line)
        if m:
            obj[m.group(1)] = m.group(2).strip()
    return fn, obj


class RenderModes(object):
    def __init__(self):
        self.flags = _flag_defines()
        self.fn, self.obj = _macro_bodies()
        self.by_name = {}
        for name in self.obj:
            if not name.startswith("G_RM_"):
                continue
            try:
                v = self.eval_name(name)
            except Exception:
                continue
            if v is not None:
                self.by_name[name] = v & 0xFFFFFFFF
        self.by_value = {}
        for n, v in self.by_name.items():
            self.by_value.setdefault(v, []).append(n)

    def _expand(self, text, depth=0):
        if depth > 12:
            raise ValueError("macro recursion")
        # GBL_c1/GBL_c2 are the only function-like macros we need.
        def gbl(m):
            which, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
            vals = [self._value(a) for a in args]
            sh = (30, 26, 22, 18) if which == "1" else (28, 24, 20, 16)
            return "(%d)" % sum(v << s for v, s in zip(vals, sh))
        prev = None
        while prev != text:
            prev = text
            text = re.sub(r"GBL_c([12])\s*\(([^()]*)\)", gbl, text)
        return text

    def _value(self, tok):
        tok = tok.strip()
        if re.match(r"^(0x[0-9a-fA-F]+|\d+)$", tok):
            return int(tok, 0)
        if tok in self.flags:
            return self.flags[tok]
        if tok in self.obj:
            return self.eval_name(tok)
        raise ValueError("unknown token %r" % tok)

    def eval_name(self, name, _depth=0):
        if _depth > 12:
            raise ValueError("recursion")
        body = self.obj[name]
        # G_RM_FOO -> RM_FOO(1) / G_RM_FOO2 -> RM_FOO(2)
        m = re.match(r"^(RM_\w+)\((\d)\)$", body)
        if m:
            macro, clk = m.group(1), m.group(2)
            if macro not in self.fn:
                return None
            _args, mbody = self.fn[macro]
            mbody = mbody.replace("GBL_c##clk", "GBL_c%s" % clk)
            return self._eval_expr(mbody)
        return self._eval_expr(body)

    def _eval_expr(self, expr):
        expr = self._expand(expr)
        expr = re.sub(r"\b([A-Za-z_]\w*)\b",
                      lambda m: str(self._value(m.group(1))), expr)
        return eval(expr, {"__builtins__": {}}, {}) & 0xFFFFFFFF  # noqa: S307

    def decode(self, word):
        word &= 0xFFFFFFFF
        low = word & 0xFFFF
        parts = [n for b, n in _BITS if low & b]
        for _label, mask, table in _FIELDS:
            parts.append(table[low & mask])
        c1 = "(%s*%s + %s*%s)" % (_BL_P[(word >> 30) & 3], _BL_A[(word >> 26) & 3],
                                  _BL_P[(word >> 22) & 3], _BL_A[(word >> 18) & 3])
        c2 = "(%s*%s + %s*%s)" % (_BL_P[(word >> 28) & 3], _BL_A[(word >> 24) & 3],
                                  _BL_P[(word >> 20) & 3], _BL_A[(word >> 16) & 3])
        names = self.by_value.get(word, [])
        return {"word": word, "flags": parts, "cyc1": c1, "cyc2": c2, "names": names}

    def name_of(self, word):
        n = self.by_value.get(word & 0xFFFFFFFF, [])
        return "/".join(n) if n else ""


# ---------------------------------------------------------------- bg.c LUTs

LUT_NAMES = {1: "DL_LUT_PRIMARY_ADDFOG", 5: "DL_LUT_SECONDARY_ADDFOG",
             6: "DL_LUT_PRIMARY", 7: "DL_LUT_SECONDARY"}


def _lut_entries(body, rm):
    """Evaluate a DL_LUT_* initializer body into a flat list of (w0, w1)."""
    body = re.sub(r"//[^\n]*", " ", body)
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.S)
    words = []
    for tok in re.finditer(r"gsDPSetRenderMode\s*\(([^,]+),([^)]+)\)|(0x[0-9a-fA-F]+|\b0\b)",
                           body):
        if tok.group(1):
            a = rm._eval_expr(tok.group(1).strip())
            b = rm._eval_expr(tok.group(2).strip())
            words.append((0xB900031D, (a | b) & 0xFFFFFFFF))
        else:
            words.append(("RAW", int(tok.group(3), 0)))
    # Flatten raw scalar runs into (w0, w1) pairs the same way C brace-elision
    # fills Gfx.words.w0/w1.
    out, pend = [], []
    for kind, val in words:
        if kind == "RAW":
            pend.append(val)
            if len(pend) == 2:
                out.append((pend[0], pend[1]))
                pend = []
        else:
            if pend:
                out.append((pend[0], 0))
                pend = []
            out.append((kind, val))
    if pend:
        out.append((pend[0], 0))
    return out


def applied_luts(rm=None):
    """{lut_index: {from_word: to_word}} for the LUTs bg.c actually applies."""
    rm = rm or RenderModes()
    src = open(BG_C, encoding="utf-8", errors="replace").read()
    maps = {}
    for idx, name in LUT_NAMES.items():
        m = re.search(r"Gfx\s+%s\[\]\s*=\s*\{(.*?)\n\};" % re.escape(name), src, re.S)
        if not m:
            continue
        ents = _lut_entries(m.group(1), rm)
        pairs = {}
        # (match, replacement) pairs, terminated by an entry with w0 == 0.
        i = 0
        while i + 1 < len(ents) and ents[i][0] != 0:
            (aw0, aw1), (bw0, bw1) = ents[i], ents[i + 1]
            if aw0 == 0xB900031D and bw0 == 0xB900031D:
                pairs[aw1] = bw1
            i += 2
        maps[idx] = pairs
    return maps


if __name__ == "__main__":
    import sys
    rm = RenderModes()
    if len(sys.argv) > 1:
        for a in sys.argv[1:]:
            d = rm.decode(int(a, 16))
            print("0x%08x %s" % (d["word"], "/".join(d["names"]) or "(no G_RM_* match)"))
            print("   flags: %s" % " | ".join(d["flags"]))
            print("   cyc1 %s   cyc2 %s" % (d["cyc1"], d["cyc2"]))
    else:
        print("known G_RM_* composites: %d" % len(rm.by_name))
        for idx, pairs in sorted(applied_luts(rm).items()):
            print("\n%s (index %d): %d rewrite pairs" % (LUT_NAMES[idx], idx, len(pairs)))
            for a, b in sorted(pairs.items()):
                print("   0x%08x %-26s -> 0x%08x %s"
                      % (a, rm.name_of(a), b, rm.name_of(b)))
