#!/usr/bin/env python3
"""D236 pass 19 (TEMP): count op-12 (GunfireRecord / dogfnegx billboard) and
op-13 (ShadowRecord / doshadow) nodes per model file, and dump op-12 record
fields (Offset/Size/Image) for files that contain them. Remove once D236 concludes."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.argv = [sys.argv[0], "ntsc-final"] if len(sys.argv) < 2 else sys.argv
import d43_emit as m

def plaus_vma(v):
    return v == 0 or (v >> 24) == 5

counts12, counts13 = {}, {}
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
    n12 = n13 = 0
    for (no, op) in placed:
        if op == 12:
            n12 += 1
            d = m.be32o(src, no + 4)
            raw = src[d:d + 0x28]
            ox, oy, oz = struct.unpack_from(">fff", raw, 0)
            sx, sy, sz = struct.unpack_from(">fff", raw, 12)
            img = struct.unpack_from(">I", raw, 24)[0] & 0xFFFFFF
            print(f"{name}: op12 node={no:#x} rec={d:#x} offset=({ox:.1f},{oy:.1f},{oz:.1f}) size=({sx:.1f},{sy:.1f},{sz:.1f}) img={img:#08x} vma?{plaus_vma(img)}")
        elif op == 13:
            n13 += 1
    if n12 or n13:
        print(f"== {name}: op12={n12} op13={n13}")
        counts12[name] = n12
        counts13[name] = n13

print(f"\ntotal files with op12: {len(counts12)}, total op12 nodes: {sum(counts12.values())}")
print(f"total files with op13: {len(counts13)}, total op13 nodes: {sum(counts13.values())}")
