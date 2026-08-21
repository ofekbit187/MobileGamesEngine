# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** ready
**Updated:** 2026-08-21 — reweighting landed; **THE RIG IS FROZEN AT 17 JOINTS**

## THE RIG IS FROZEN AT 17 JOINTS
This is the ADR 0018 trigger you asked me to call out explicitly. **The joint list is final:
17 joints, no clavicle, exactly as `Joint` has stood since ADR 0007.** Nothing in this push
touches the `Joint` enum, the palette, or `skinned.vert` — so there is no collision with the
renderer session's texture work either. `mge_rig_export` (17.4) can publish, and the owner can
author animation against a skeleton that will not move under him.

## Now
Landed and pushed. **New body content hash: `be7b3618f965eb0f`** (2100 verts, 2388 tris).

**B-31 per motion: 44 edges over 100% strain -> 4.** Everything else held: hips still 0.999
worst / 0 edges, knees 0.652 / 0, vertices bound at exactly 1.00 down from 53 to 5.

**NOT in this push: task 13.6** (elbow/knee cut lines, ear sub-shell) and the `cut_shell`
tie-break. They were in your bundle because the clavicle was going to break every garment
binding anyway; without it this push is far smaller, and 13.6 is a substantial cut-geometry
change in its own right. It is next, and it is still a contract-version event — but I would
rather land the owner's priority verified than bundle it with work that has not started.

## What the four remaining edges are
The floor of what weighting can do. Shoulder margins 0.22-0.38 m and bands 0.10-0.34 m all
land on the same four; the clavicle did not move them either. Recorded in the gate with a
named retirement condition rather than asserted away.

## Needs from the architect
Nothing blocking. Still open, not blocking: 13.8's `shoulder`/`mid_upper_arm` loop positions,
and the one line wiring the wearables `gateHemLoops` to `fitHemLoop`.

## Two gates were measuring the wrong thing — worth your eye
- **`body_mesh_has_human_proportions` read body width through the Torso REGION**, which is a
  skinning label. Re-weighting moved the Torso/Leg boundary and it reported the hips as
  0.146 m wide **when not one vertex had moved**. Now measured from geometry at rig-derived
  heights: shoulders 0.466 m, waist 0.306 m, hips 0.347 m. This is the SECOND fault of that
  family in this one test — ADR 0012 removed a fixed-band sampling defect from it.
- **`body_mesh_skin_weights_are_valid` hard-coded the rule that forbade the fix.** Its 0.12 m
  reach limit is a leakage guard and it was also forbidding the shoulder's blend band. Now
  0.12 m everywhere, 0.22 m in the shoulder scope, mirroring the pipeline. Widening it
  globally instead is exactly what broke the knee.

## Last landed
**16.4** (this push) — the reweighting. **16.6** (`a5573aa`) — the B-31 gate, amended here to
measure per motion. **16.5** (`9c26494`) — `skinMesh` writes UVs. ctest 13/13, heap 0, two
independent pipeline runs byte-identical across all nine assets. Evidence:
`docs/research/shoulder-reweight.md`, third addendum.
