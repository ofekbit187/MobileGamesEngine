# Task 13.8 — the hem-loop table, measured, and the two loops that are not where B-11 puts them

**From:** the character asset pipeline session · **To:** the architect, cc the wearables session
**Delivers:** `B-11` (hem-loop table), `B-25` (per-region vertex groups)
**Body hash:** `1067c74324b6e091` — **unchanged**; 13.8 adds engine API and tests, no asset moved, no re-bake

---

## 1. What shipped

`templateHemLoops()` publishes **19 loops** — B-11's eleven canonical names, with the eight limb
ones per side. Every one is **derived from the rig**, never chosen by eye:

- **trunk loops are horizontal planes through the joint that names them** — neck base at `Neck`,
  waist at `Spine`, hip at `Thigh`;
- **limb loops are perpendicular to the BONE**, because the template's arms hang about 21° out and
  a horizontal plane across one cuts an ellipse rather than a cuff. This is the same reason the
  shipped sleeves already cut along the bone instead of by height.

Deriving them from the rig also means the table cannot drift away from the skeleton every garment
and animation already binds to.

`fitHemLoop()` then measures what the body **actually does** at each declared plane, because a
loop that names a height nothing encircles is worse than no table at all — a garment authored
against it terminates on nothing.

● real output, `ctest`:

```
neck_base        0.417 m around,  41% of it Neck
waist            0.834 m around, 100% of it Torso
hip              0.983 m around,  14% of it Torso
elbow_l          0.279 m around, 100% of it ArmL
elbow_r          0.276 m around, 100% of it ArmR
wrist_l          0.266 m around,  60% of it HandL
wrist_r          0.262 m around,  56% of it HandR
mid_thigh_l      0.522 m around, 100% of it LegL
mid_thigh_r      0.521 m around, 100% of it LegR
knee_l           0.345 m around, 100% of it LegL
knee_r           0.347 m around, 100% of it LegR
boot_cuff_l      0.313 m around, 100% of it LegL
boot_cuff_r      0.313 m around, 100% of it LegR
ankle_l          0.239 m around,  77% of it FootL
ankle_r          0.240 m around,  50% of it FootR

region groups: 2097 vertices across 12 regions, none shared, none orphaned
```

**Every loop in the table closes. Zero open chains, all nineteen.** The shares below 100 % are not
defects: those loops sit *on* a region seam by design — a wrist is where the arm becomes the hand —
so the ring is legitimately made of both.

`B-25` lands as `bodyVertexRegions()`, a **total partition**: 2 097 vertices, twelve regions, none
shared, none orphaned, which is `BODY_CONTRACT.md` §9.6 exactly. It is a total function because
13.7 exports one primitive per region, so no vertex is shared between two — and that means
`false` from it signals a malformed body rather than an untagged one.

## 2. Three ways of choosing the ring, two of them wrong

Slicing is easy; deciding *which* ring is the loop is where this went wrong twice, and both
failures are worth keeping because both looked correct in code review.

1. **Filter triangles by distance from the loop's point.** Truncates the ring itself into open
   chains — every trunk loop reported two to five open chains that were really one ring cut up.
2. **Restrict to the loop's own region.** Breaks every loop that sits *on* a region boundary,
   which is most of the interesting ones: neck base, hip, shoulder, wrist and ankle all returned
   zero rings, because the ring simply continues into the neighbouring region.
3. **Slice the whole shell, attribute rings by region.** Works for all nineteen. The body is
   closed, so every chain closes; a plane makes one ring per limb or trunk it crosses; and the
   loop is the ring made mostly of the region it declares.

Two selection rules were tried inside (3) before the region vote:

- **Nearest ring.** A plane through the left upper arm also cuts the trunk, and the trunk's centre
  is *nearer* the shoulder joint than the arm ring's is — so this reported a **1.19 m shoulder**, a
  chest measurement wearing a shoulder's name.
- **Point-inside-ring**, by crossing test in the plane. Fails because the rig's knee sits **75 mm
  forward** of the leg's own cross-section centroid, so the joint is genuinely outside the ring it
  names. Measured, not guessed: ring centre (0.152, 0.422, 0.041) against a knee joint at
  (0.155, 0.430, −0.034).

## 3. Two loops are not where B-11 puts them, and that is the architect's call

**`shoulder_*` and `mid_upper_arm_*` do not encircle the arm.** A plane perpendicular to the
upper-arm bone keeps clipping the chest, because the arm hangs about 21° out of vertical. Scanned
down the bone (0.324 m long), ● real output:

```
 t(m)   frac  rings   circum
0.000   0.00      1    1.190     <- shoulder as B-11 places it: the chest
0.100   0.31      2    1.188
0.160   0.49      2    1.346
0.180   0.56      3    0.367     <- the first plane that encircles the ARM
0.220   0.68      3    0.353
0.320   0.99      3    0.278     <- the elbow
```

The arm only becomes a separable ring at **0.18 m, 56 % of the way down the upper arm**. Above
that there is no arm-only ring to name, and the measured "shoulder" is 1.19 m around with 11 % of
it actually `ArmL`.

Worth flagging alongside: the shipped tunic terminates its sleeve at `along_arm(0.13)` and the
armour at `0.16` — **both above 0.18 m**, i.e. in the merged zone. Those garments work, because
`cut_shell` selects faces by region and half-space rather than requiring a clean ring. But a
garment artist told to "terminate on the mid-upper-arm loop" would be terminating on something
that is not a ring around the arm.

```
SEAM: Body contract B-11 (hem-loop table) ⇄ wearables authoring
NEED: A ruling on where `shoulder` and `mid_upper_arm` sit. As B-11 names them — planes through
      the shoulder joint and the upper-arm midpoint — they do not encircle the arm on this body,
      measured above. The other nine canonical loops are clean.
BREAKS: Nothing shipped. It decides what a garment artist is told to terminate a sleeve on, and
      what the wearables hem-loop gate checks those two loops against.
PROPOSAL: One of — (a) move both down to where an arm-only ring exists, which makes `shoulder`
      the armscye line at 0.18 m and `mid_upper_arm` the midpoint of what remains; (b) keep them
      at the joint and record that they are trunk loops at the armhole rather than arm loops,
      which is arguably what a sleeveless garment's opening actually follows; or (c) leave them
      and let the first authored sleeve decide. I lean (a) because it is the only option under
      which the name and the geometry agree, but where a canonical loop sits is a B-11 question
      and B-11 is a seam, so I have not moved it. Pinned meanwhile by
      `the_upper_arm_loops_do_not_encircle_the_arm_yet` so the state cannot change silently.
```

## 4. The wearables gate still needs one line

`gateHemLoops` in `engine/src/import/wearable_gates.cpp` is hard-coded `Blocked` with `(void)body`.
The table it was waiting for now exists, but that file is the wearables session's and wiring it is
changing what their tool does, not running it — which ADR 0012 records as never in scope. It needs
one call to `fitHemLoop` over `templateHemLoops()`. Raised in `docs/status/` rather than done.

## 5. Evidence

```
ctest         12/12, including:
                the_hem_loop_table_names_every_canonical_loop
                every_hem_loop_closes_on_the_body          (19 loops, 0 open chains)
                hem_loops_measure_the_limb_they_name       (circumference + region share)
                the_upper_arm_loops_do_not_encircle_the_arm_yet
                region_vertex_groups_cover_every_vertex_exactly_once
host runner   steady-state heap allocations: 0
assets        unchanged — body hash still 1067c74324b6e091, no re-bake needed
```
