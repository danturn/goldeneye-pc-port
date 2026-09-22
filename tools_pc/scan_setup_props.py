#!/usr/bin/env python3
"""D236 pass 21: list the PROP_* models a level's setup actually places.

Parses the PROP enum out of src/bondconstants.h and the object records out of
one or more assets/obseg/setup/Usetup*Z.c files, then reports which props each
setup instantiates.  Used to decide, statically, whether a given piece of level
scenery can possibly be a prop model at all -- if the setup places none of the
candidate models, the geometry must come from the level's bg file instead.

Note: only the record types that carry a real ObjectRecord header (StandardProp,
Door, Collectable, ...) have a meaningful `obj` field; Guard/Tag rows reuse the
same word layout for something else and are reported but should not be trusted
as model placements.

Usage:  python tools_pc/scan_setup_props.py [setup.c ...]
        (default: UsetupsevxZ.c UsetupsevxbZ.c -- Surface 1 / Surface 2)
"""
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONSTANTS = os.path.join(ROOT, "src", "bondconstants.h")
SETUP_DIR = os.path.join(ROOT, "assets", "obseg", "setup")

# One object record: the PropDefHeaderRecord word, then _mkword(obj, pad).
RECORD_RE = re.compile(
    r"/\* Type = (\w+); index = \d+ \*/\s*"
    r"_mkword\(\s*-?\d+,\s*_mkshort\(\s*-?\d+,\s*-?\d+\s*\)\s*\),\s*"
    r"_mkword\(\s*(-?\d+)\s*,")

FOLIAGE_RE = re.compile(r"PROP_(JUNGLE\d*_TREE|PALM|PALMTREE|PLANT\d*B?)$")


def load_prop_enum(path=CONSTANTS):
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    # Exact match only: "typedef enum PROPFLAG" / "PROPDEF_TYPE" / "PROP_TYPE"
    # all share the prefix, and picking one of those silently yields an enum
    # with no PROP_* members at all (every obj then resolves to "?").
    i = next(n for n, l in enumerate(lines) if l.strip() == "typedef enum PROP")
    while "{" not in lines[i]:
        i += 1
    i += 1
    val, out = 0, {}
    while not lines[i].strip().startswith("}"):
        m = re.match(r"(PROP_[A-Z0-9_]+)\s*(?:=\s*(-?\w+))?\s*,?", lines[i].strip())
        if m:
            if m.group(2):
                val = int(m.group(2), 0)
            out[val] = m.group(1)
            val += 1
        i += 1
    return out


def scan(path, enum):
    txt = open(path, encoding="utf-8", errors="replace").read().replace("\r", "")
    counts = collections.Counter()
    for m in RECORD_RE.finditer(txt):
        counts[(m.group(1), int(m.group(2)))] += 1
    declared = txt.count("/* Type = ")
    return counts, declared


def main(argv):
    enum = load_prop_enum()
    # Self-test / positive control: a "0 foliage" result is only meaningful if
    # the detector can fire at all.  Print the foliage props the enum defines
    # and that FOLIAGE_RE recognises, so a zero count below reads as "not
    # placed", never as "the matcher never matched anything".
    known = sorted(n for n in enum.values() if FOLIAGE_RE.match(n))
    print("PROP enum: %d entries; foliage props recognised (%d): %s"
          % (len(enum), len(known), ", ".join(known)))
    if not known:
        print("ERROR: foliage detector matches nothing -- results below are meaningless")
        return 1
    names = argv[1:] or ["UsetupsevxZ.c", "UsetupsevxbZ.c"]
    for name in names:
        path = name if os.path.exists(name) else os.path.join(SETUP_DIR, os.path.basename(name))
        counts, declared = scan(path, enum)
        print("=" * 70)
        print(os.path.basename(path), "-- %d object records matched of %d total records"
              % (sum(counts.values()), declared))
        foliage = 0
        for (typ, obj), n in sorted(counts.items(), key=lambda kv: -kv[1]):
            nm = enum.get(obj, "?")
            tag = "  <== FOLIAGE" if FOLIAGE_RE.match(nm) else ""
            if tag:
                foliage += n
            print("  %4d  %-22s obj=%4d %s%s" % (n, typ, obj, nm, tag))
        print("  FOLIAGE PROPS PLACED: %d" % foliage)


if __name__ == "__main__":
    main(sys.argv)
