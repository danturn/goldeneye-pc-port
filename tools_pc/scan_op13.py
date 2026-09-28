#!/usr/bin/env python3
"""D236 pass 17c (TEMP): dump every op-13 (ShadowRecord) record's raw bytes
from the ROM model files, to settle the field-layout question (does the ROM
put HeaderNode at +0x14 as the C struct says, or +0x18 as d43_emit.py's emit
assumes?). Prints per file: node offset, record offset, and the 16 bytes at
d+0x10..d+0x20 with both interpretations annotated. Remove once D236 concludes."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv = [sys.argv[0], "ntsc-final"] if len(sys.argv) < 2 else sys.argv
import d43_emit as m

def plaus_vma(v):
    # file-relative seg-5 vma: 0x05xxxxxx, or null
    return v == 0 or (v >> 24) == 5

def plaus_f32(b):
    f = struct.unpack_from(">f", b)[0]
    return abs(f) < 1e6 and f == f  # finite, sane magnitude

n_found = 0
for name in m.model_names:
    key, row = m.find_row(name)
    if not key:
        continue
    addr, size = row
    src = __import__("zlib").decompress(m.rom[addr:addr + size][2:], -15)
    D = len(src)
    hkey = None
    for cand in (key, key[1:], key[:-1], key[1:-1]):
        if cand in m.nsnt:
            hkey = cand
            break
    if not hkey:
        continue
    NS, NT = m.nsnt[hkey]
    nodes, R0 = m.build_nodes(src, D, NS, NT)
    if not nodes:
        continue
    placed = m.placement_order(nodes, src, R0)
    for (no, op) in placed:
        if op != 13:
            continue
        n_found += 1
        d = m.be32o(src, no + 4)
        raw = src[d + 0x10:d + 0x20]
        v_10 = struct.unpack_from(">I", raw, 0)[0] & 0xFFFFFF
        v_14 = struct.unpack_from(">I", raw, 4)[0] & 0xFFFFFF
        v_18 = struct.unpack_from(">I", raw, 8)[0] & 0xFFFFFF
        v_1c = struct.unpack_from(">I", raw, 12)[0] & 0xFFFFFF
        f_18 = struct.unpack_from(">f", raw, 8)[0]
        f_1c = struct.unpack_from(">f", raw, 12)[0]
        print(f"{name}: node={no:#x} rec={d:#x}")
        print(f"   +0x10 image      = {v_10:#08x}  vma?{plaus_vma(v_10)}")
        print(f"   +0x14 (A:Header) = {v_14:#08x}  vma?{plaus_vma(v_14)}")
        print(f"   +0x18 (B:Header) = {v_18:#08x}  vma?{plaus_vma(v_18)}   as f32={f_18!r}")
        print(f"   +0x1C (A:Scale)  = as f32={f_1c!r}   raw={v_1c:#08x} vma?{plaus_vma(v_1c)}")
print(f"\ntotal op13 records: {n_found}")
