# Character asset pipeline

**Session:** session_01PeC37FrSvon7V7BMAViaJS
**Branch:** `claude/character-asset-pipeline-v3`
**State:** ready
**Updated:** 2026-08-21 — 13.8 pushed

## Now
**13.8 is done and pushed.** Next is **13.9** (published glTF authoring reference, `B-29`),
which I will start on the next turn unless a ruling below changes it. 13.9 depends on the
hem-loop table 13.8 just landed, so the two `Needs:` below shape what the reference says
about sleeves — worth ruling before I publish a file artists are meant to model against.

## Needs from the architect

**1. Where two of B-11's canonical loops actually sit.** Nine of the eleven are clean. Two
are not, measured rather than argued:

```
SEAM: Body contract B-11 (hem-loop table) ⇄ wearables authoring
NEED: A ruling on `shoulder` and `mid_upper_arm`. As B-11 names them — planes through the
      shoulder joint and the upper-arm midpoint — they do not encircle the ARM. The arm
      hangs ~21 deg out, so a plane perpendicular to its bone keeps clipping the chest: the
      "shoulder" ring measures 1.19 m around and is 11% ArmL. Scanned down the 0.324 m bone,
      the first plane that encircles the arm alone is at 0.18 m (56%).
BREAKS: Nothing shipped. It decides what a garment artist is told to terminate a sleeve on,
      and it decides what 13.9's authoring reference publishes. Note the shipped tunic cuts
      its sleeve at 0.13 m and the armour at 0.16 m — both ABOVE 0.18 m, i.e. in the merged
      zone. They work because `cut_shell` selects by region and half-space rather than by
      ring, but an artist told to "terminate on the mid-upper-arm loop" would be terminating
      on something that is not a ring around the arm.
PROPOSAL: (a) move both down to where an arm-only ring exists — `shoulder` becomes the
      armscye at 0.18 m; (b) keep them and record that they are trunk loops at the armhole,
      which is arguably what a sleeveless opening follows; or (c) let the first authored
      sleeve decide. I lean (a), the only option where the name and the geometry agree — but
      B-11 is a seam so I have not moved it. Pinned by
      `the_upper_arm_loops_do_not_encircle_the_arm_yet` so it cannot drift while open.
```

**2. One line in the wearables session's file.** `gateHemLoops` in
`engine/src/import/wearable_gates.cpp` is still hard-coded `Blocked` with `(void)body`. The
table it waited for exists now; it needs one call to `fitHemLoop` over `templateHemLoops()`.
That is changing what their tool does, not running it, which ADR 0012 records as never in
scope — so it is yours to dispatch, not mine to take.

## Last landed
**13.8** — `B-11` hem-loop table and `B-25` region vertex groups. 19 loops, all rig-derived;
**every one closes, zero open chains**. Measured: neck 0.417 m, waist 0.834 m, hip 0.983 m,
elbow 0.279 m, wrist 0.266 m, mid-thigh 0.522 m, knee 0.345 m, boot-cuff 0.313 m, ankle
0.239 m. `bodyVertexRegions()` is a total partition — 2097 vertices, 12 regions, none shared,
none orphaned (`BODY_CONTRACT` §9.6). **No asset changed**, so the body hash is still
`1067c74324b6e091` and no re-bake was needed. Evidence: `docs/research/hem-loops.md`.

**Before that: 13.7a + 13.7b** — `mge_uv_report --gate` reads CONFORMS (with waivers), exit 0.
