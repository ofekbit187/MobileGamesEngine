# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** working
**Updated:** 2026-08-21 — started 13.8

## Now
**Task 13.8** — the hem-loop table (`B-11`) and per-region vertex groups (`B-25`).
Landing it also flips the wearables `Hem-loop table` gate from BLOCKED to checked.

## Needs from the architect
Nothing yet. One likely seam request is coming and I am naming it early so it is not a
surprise: `gateHemLoops` in `engine/src/import/wearable_gates.cpp` is hard-coded
`Blocked` and is the wearables session's file. I can publish the table, but I cannot
wire their gate to it. Details will land here under `Needs:` when the table exists.

## Last landed
**13.7a + 13.7b** (`69e3d38`, merged to integration as `e528028`) — body hash
`1067c74324b6e091` (2097 verts, 2388 tris). `mge_uv_report --gate` reads
**CONFORMS (with waivers)** and exits 0, for the first time.
