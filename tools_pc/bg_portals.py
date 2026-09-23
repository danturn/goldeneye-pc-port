#!/usr/bin/env python3
"""D294: offline dump + structural analysis of a level's bg portal graph.

Decodes `bg/bg_<level>_all_p.seg` straight out of the ROM (same pipeline as
bg_gdl_census.py) and walks the portal table that `bg.c` uses for its
per-frame room-visibility walk (`bgDetermineVisibleRooms` ->
`sub_GAME_7F0B7F84` / `sub_GAME_7F0B5864`).

File layout (decompressed .seg, big-endian):
  word[0]  format flag (0 = normal path)
  word[1]  offset of the bg_room_data array (24 B/record; see bg_gdl_census)
  word[2]  offset of the g_BgPortals table: 8-byte records
            u32 offset_portal   (segment-0x0F file offset of the point data,
                                 0 = end of table)
            u8  connectedRoom1
            u8  connectedRoom2
            u8  controlbytes1   (bit0 PORTALFLAG_DISABLED, bit1 _SPECIAL)
            u8  controlbytes2
  word[3]  envdata table offset (not needed here)

Point data at `offset_portal & 0x00FFFFFF` (bg_portal_entry):
  u8 numPoints; u8 pad[3]; then numPoints * coord3d (f32 x,y,z).

The analysis section checks the walk's hard limits against the graph:
  * portal count vs PORTMAX (200, src/game/bg.h)
  * undirected BFS depth from every room vs the walk's `depth >= 16` /
    D_8004489C (=0xF) cut-offs (sub_GAME_7F0B7F84)
  * per-room portal degree vs the `bgIncrementRoomPortalVisitCount >= 9`
    enqueue drop (bgQueuePortalTraversal, depth >= 2)
  * worst-case single-frame enqueue volume vs BG_PORTAL_QUEUE_LEN (500 NTSC /
    250 EU) -- a rough upper bound: every portal enqueued once per endpoint
    room visit.

Usage:  python tools_pc/bg_portals.py stat [ntsc-final]
"""
import os
import struct
import sys
import collections
import contextlib
import io

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools_pc"))

PORTMAX = 200
QUEUE_LEN_NTSC = 500
QUEUE_LEN_EU = 250


def load_bg_file(name, region):
    sys.argv = ["", region]
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        import d43_emit as m
    key, row = m.find_row("bg_%s_all_p" % name)
    if not key:
        raise SystemExit("not in ROM file table: bg_%s_all_p" % name)
    addr, size = row
    raw = m.rom[addr:addr + size]
    import zlib
    try:
        return zlib.decompress(raw[2:], -15)
    except zlib.error:
        return raw


def seg_off(v):
    return v & 0x00FFFFFF


def main():
    level = sys.argv[1] if len(sys.argv) > 1 else "stat"
    region = sys.argv[2] if len(sys.argv) > 2 else "ntsc-final"
    data = load_bg_file(level, region)

    w0, roomlist_off, portal_off = struct.unpack_from(">III", data, 0)
    print("word0=%#x rooms@%#x portals@%#x filelen=%d" % (w0, seg_off(roomlist_off), seg_off(portal_off), len(data)))

    # --- room array (24 B records; same walk as bg_gdl_census.read_rooms) ---
    nrooms = 0
    off = seg_off(roomlist_off)
    while off + 24 <= len(data):
        point, pri, sec = struct.unpack_from(">III", data, off)
        if nrooms > 1 and point == 0 and pri == 0 and sec == 0:
            break
        nrooms += 1
        off += 24
    print("rooms: %d" % nrooms)

    # --- portal table (8 B records, N64 stride; offset_portal==0 terminates) ---
    portals = []
    poff = seg_off(portal_off)
    while poff + 8 <= len(data):
        optr, r1, r2, cb1, cb2 = struct.unpack_from(">IBBBB", data, poff)
        if optr == 0:
            break
        portals.append((optr & 0x00FFFFFF, r1, r2, cb1, cb2))
        poff += 8
    print("portals: %d  (PORTMAX=%d%s)" % (len(portals), PORTMAX, "  <-- OVER" if len(portals) >= PORTMAX else ""))

    # --- point data per portal ---
    adj = collections.defaultdict(list)   # room -> [portal index]
    bad = 0
    for i, (poff2, r1, r2, cb1, cb2) in enumerate(portals):
        if poff2 + 4 > len(data):
            print("portal %d: point data %#x past EOF" % (i, poff2))
            bad += 1
            continue
        npts = data[poff2]
        pts = struct.unpack_from(">%df" % (3 * npts), data, poff2 + 4) if npts else ()
        for r in (r1, r2):
            adj[r].append(i)
    if bad:
        print("WARNING: %d portals with unreadable point data" % bad)

    # flag census
    flags = collections.Counter()
    for i, (_, _, _, cb1, _) in enumerate(portals):
        f = []
        if cb1 & 0x01:
            f.append("DIS")
        if cb1 & 0x02:
            f.append("SPE")
        flags["+".join(f) or "-"] += 1
    print("flags:", dict(flags))

    # point-count census (sub_GAME_7F0B5864's points[19] holds numPoints +
    # near-plane clip points; >12 authored points is close to the edge)
    ptc = collections.Counter()
    for i, (poff2, _, _, _, _) in enumerate(portals):
        if poff2 + 4 <= len(data):
            ptc[data[poff2]] += 1
    print("numPoints census:", dict(sorted(ptc.items())))

    # per-room degree vs the visit-count limit of 9
    hot = [(r, len(ps)) for r, ps in sorted(adj.items()) if len(ps) >= 9]
    if hot:
        print("rooms with >=9 portals (visit-count drop risk at depth>=2): %s" % hot)
    else:
        print("max room portal degree: %d (limit 9)" % max((len(p) for p in adj.values()), default=0))

    # undirected BFS min-depth from every room
    rooms = sorted(adj.keys())
    depth_from = {}
    for s in rooms:
        d = {s: 0}
        q = collections.deque([s])
        while q:
            u = q.popleft()
            for pi in adj[u]:
                _, r1, r2, _, _ = portals[pi]
                v = r1 ^ r2 ^ u
                if v not in d:
                    d[v] = d[u] + 1
                    q.append(v)
        depth_from[s] = d
    worst = []
    for s in rooms:
        d = depth_from[s]
        missing = [r for r in rooms if r not in d]
        maxd = max(d.values()) if d else -1
        if maxd >= 15 or missing:
            worst.append((s, maxd, missing))
    if worst:
        print("rooms unreachable (<16) from some start room (depth>=15 or disconnected):")
        for s, maxd, missing in worst:
            print("  from room %d: maxdepth=%d missing=%s" % (s, maxd, missing))
    else:
        allmax = max(max(d.values()) for d in depth_from.values())
        print("all rooms pairwise reachable; max BFS depth over all start rooms: %d (walk cut-off 16)" % allmax)

    # worst-case enqueue volume: each portal can be enqueued once per frame
    # from each endpoint room's visit; the walk visits a room at most once per
    # portal path... conservative bound = 2 * nportals.
    print("conservative enqueue bound 2*P = %d vs queue %d (NTSC) / %d (EU)" %
          (2 * len(portals), QUEUE_LEN_NTSC, QUEUE_LEN_EU))

    # dump the full portal table for eyeballing
    print()
    print("%-4s %-6s %-6s %-4s %-4s %-3s  points" % ("idx", "room1", "room2", "cb1", "cb2", "n"))
    for i, (poff2, r1, r2, cb1, cb2) in enumerate(portals):
        npts = data[poff2] if poff2 + 4 <= len(data) else -1
        print("%-4d %-6d %-6d %#-5x %#-5x %-3d" % (i, r1, r2, cb1, cb2, npts))


if __name__ == "__main__":
    main()
