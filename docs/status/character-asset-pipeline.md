# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** ready
**Updated:** 2026-08-21 — 13.7a/13.7b merged to integration by the architect

## Now
Awaiting dispatch to **13.8** (hem-loop table `B-11`, per-region vertex groups `B-25`) then
**13.9** (published glTF authoring reference `B-29`). 13.8 now also unblocks the one wearables
acceptance gate reporting BLOCKED.

## Needs from the architect
Nothing outstanding. ADR 0012 answered all three open questions and ADR 0013 ratified the
contract corrections that came alongside.

## Last landed
**13.7a + 13.7b** (`69e3d38`) — body hash `1067c74324b6e091` (2097 verts, 2388 tris).
`mge_uv_report --gate` reads **CONFORMS (with waivers)** and exits 0; it has never been green
before (four rules refused before the repack, two after, one after 13.7, none now). The 200
triangles ADR 0012 funded went to the hairline boundary and nowhere else; LOD1/LOD2 still ship
1200/560. The loop is proved by topology rather than by eye, deliberately: the body is untextured
and smooth-shaded, so a render would have been evidence of nothing. The waiver mechanism's drift
guard fired on its own change, refusing to cover HandL when the bisect moved it 1.93x → 1.96x.
