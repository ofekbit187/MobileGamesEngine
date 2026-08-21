# Task 16.4 — the shoulder reweighting, measured. It halves the tear and does not reach the bar.

**From:** the character asset pipeline session · **To:** the architect, cc the animation and wearables sessions
**Implements:** ADR 0015 Ruling — reweight before spending a rig-version event
**Instrument:** the animation session's own per-edge strain gate, reproduced exactly
**Status: NOT LANDED.** The measurements are the deliverable. Three reasons below.

---

## 0. The instrument reproduces

Before changing anything I rebuilt ADR 0015's table with the animation session's metric on the
shipped body. ● real output, every figure identical to the ADR:

```
UpperArmR influences 195 vertices, 53 of them at weight exactly 1.00

   pitch      worst edges>50% edges>100%
     15deg      0.352        0        0
     30deg      0.690        5        0
     45deg      1.003       11        2
     90deg      2.413       47       21
    140deg      3.513       71       32
```

## 1. What the reweighting achieves

Bone heat plus the leakage prune leaves the shoulder with no gradient at all. The fix is
Laplacian relaxation of the weights over the mesh **welded by exact position** — the body is
exported one primitive per region, so on the raw index graph the arm smooths against nothing
across the shoulder seam, which is exactly where the band is needed. Best measured configuration:

```
UpperArmR influences 270 vertices, 5 of them at weight exactly 1.00

   pitch      worst edges>50% edges>100%     (shipped, for comparison)
     15deg      0.194        0        0       0.352   0    0
     30deg      0.393        0        0       0.690   5    0
     45deg      0.584        3        0       1.003  11    2
     90deg      1.087       34        2       2.413  47   21
    140deg      1.578       69       18       3.513  71   32
```

**Worst strain more than halves (3.513 → 1.578). The 1.00-weight cliff is gone (53 → 5).** At 30°,
where locomotion lives, the body goes from 5 edges over 50% to **zero**, and the animation
session's own layering test reports locomotion's worst edge strain dropping 0.311 → 0.265.

**It does not meet the acceptance number.** ADR 0015 asks for **zero edges over 100% at 140°**;
this is 18.

## 2. Why I stopped tuning — the search, not a guess

Every configuration below was built and measured, not reasoned about:

| what was tried | result at 140° |
|---|---|
| smooth dense mesh only, 6 rounds | **1.578 / 18** ← best |
| smooth dense + smooth each LOD (fixed round count) | 2.220 / 25 |
| tapered prune instead of a hard cut | 1.847 / 22 |
| smooth LOD with the prune after it | 2.178 / 21 |
| band width 6 cm / 10 cm / 16 cm / 26 cm | flat at 11 edges >100% @ 90° |
| smoothing rounds 4 / 8 / 14 / 20 | flat: 7 / 6 / 4 / 4 @ 90° |

Two of those deserve recording because they are counter-intuitive and cost real time:

**Smoothing the decimated LOD makes it worse.** Rounds are **hops**, and a hop is ~1.3 cm on the
dense body and ~3 cm on LOD0 — so the same eight rounds that build a 10 cm shoulder band upstream
push arm influence **24 cm down the torso** here. Measured, that put 0.31 of `UpperArmR` on a
vertex at the **waist** (y 1.109) and moved the worst edge there. It is the same trap ADR 0013
recorded for pit measurement: a hop is a different distance in every part of the body.

**A hard prune after smoothing trades one cliff for another.** Cutting at the margin left arm
influence going 0.22 → 0.00 between neighbours, and those edges tore exactly as the shoulder's own
1.00 → 0.31 cliff did.

**And it is not a triangle-density limit.** I tested that directly, because it was the obvious
next hypothesis: the same weights measured on the **full-resolution 21 582-triangle body** come
out **worse**, not better — 5.151 worst, 66 edges over 100% at 140°. Strain is a ratio, and a
denser mesh has shorter edges to divide by. More triangles will not buy this.

## 3. Why it is not landed — three findings, and the third is the important one

**(a) It misses its own acceptance number.** 18 edges over 100% against a required zero.

**(b) It introduces degenerate triangles.** LOD0 goes 2 388 → 2 390 and
`body_mesh_has_no_degenerate_triangles` fails. The cause is indirect and is finding (c) in another
costume: the face-split bisect operates on the set of faces the region rule calls "head", that set
moved, and the plane now grazes faces it previously missed, producing needle slivers.
`dissolve_degenerate` does not catch them — a needle has long edges and no area.

**(c) Changing skin weights silently re-cuts the masking regions.** This is the one worth a
ruling. `regionOf()` reads the **bone that moves a vertex most**, so a reweighting *is* a
re-segmentation: move the weights and the Face/Neck/Torso boundaries move with them. It surfaced
as the wearables session's `pit_measurement_ignores_local_curvature_and_finds_real_gaps` failing on
its `jaw` assertion — "the tightest spot on the template is under the jaw; if this stops being
true the body's head changed shape". The head did not change shape. The **region boundaries** did.

ADR 0015 scoped 16.4 as "data: no `Joint` enum edit, no shader change, no new joint". That is true
of the weights themselves and **not** true of their consequences: a reweight is a
contract-version event that also moves the region partition, so it lands against the wearables
gates as if the body had been re-modelled.

```
SEAM: Skin weights ⇄ region segmentation ⇄ wearables gates
NEED: A ruling on how a reweighting is allowed to move region boundaries — and, given (a),
      whether this reweighting should land at all before the clavicle question is settled.
BREAKS: Any reweight fails the wearables `jaw` pit assertion and re-cuts every region seam,
      which changes the UV chart's region boxes and the garment cut boundaries with it.
PROPOSAL: Either (i) hold 16.4 until the clavicle is decided and land ONE change — a reweight
      alone is a contract-version event that re-bakes six garments for a partial win, and
      ADR 0011's own rule is that such work rides an existing event rather than causing its
      own; or (ii) land the reweight now and dispatch the wearables `jaw` assertion to be
      re-baselined against the new partition. I lean (i), for the reason in section 4.
```

## 4. The clavicle, re-opened honestly — which is what ADR 0015 asked for

ADR 0015 says: *"If a properly weighted shoulder still tears at 140°, that is a clean measurement
that one joint cannot carry the rotation, and the clavicle gets decided on evidence rather than
inference."*

The shoulder is now properly weighted — no vertex-to-vertex cliff, a real falloff band, the
1.00-weight count down from 53 to 5 — and **it still tears at 140°: 18 edges over 100%.** The
arithmetic says why, and it is not about weighting quality. Under linear blending the positional
difference between two neighbouring vertices is approximately

```
|Δp| ≈ Δw × 2r × sin(θ/2)
```

At θ = 140° near the deltoid (r ≈ 0.16 m) that is `Δw × 0.30 m`. For an edge of length L to stay
under 100% strain you need `Δw ≤ L / 0.30`, and the worst edges there are **2.7 cm** long — so
neighbouring vertices may differ by at most **0.09** in weight. Going 1.0 → 0.0 then needs at
least eleven evenly spaced steps in a band that has roughly five vertices across it. Adding
triangles does not help (section 2). Widening the band pushes influence onto the torso, which
tears at the far end instead.

**A clavicle halves θ per joint**, and `sin(θ/2)` is where the whole term lives: two joints at 70°
each give `2 × sin(35°) = 1.15` against one joint's `2 × sin(70°) = 1.88` — a **39% reduction** in
the positional difference for the same weights, before any of the extra band a second joint's own
falloff would add.

I am not ruling on it and I have not implemented it — 17 → 18 joints is a rig-version event that
breaks every garment binding and the shader's palette, exactly as ADR 0015 says. But the condition
ADR 0015 set for re-opening the question has been met, and the numbers are above.

## 5. Reproducing this

The pipeline changes are not committed (section 3). The configuration that produced the best
result, for whoever picks this up:

- `WEIGHT_MARGIN` 0.10 → **0.22 m** (0.10 forbids the band outright: a deltoid vertex is ~0.02 m
  from the upper-arm bone and ~0.19 m from the chest bone, so every torso influence on it was
  pruned). The leak the rule exists to catch — thigh influencing the ankle — is 0.35 m past its
  nearest bone and is still caught.
- `smooth_weights()`: Laplacian relaxation over the mesh welded by exact position, **8 rounds,
  factor 0.5**, applied to the **dense body only**, immediately after the prune, followed by a
  second prune and clamp.
- `tests/test_body_mesh.cpp`'s `body_mesh_skin_weights_are_valid` hard-codes a **0.12 m** reach
  limit that must move with `WEIGHT_MARGIN`, or the band fails the gate that forbids it.

A working copy is at `tools/model/humanoid_template.py` as of this session's scratch; the diff is
~90 lines and re-deriving it from this section takes minutes.

---

# Addendum — ADR 0016 widened the scope, and the answer changed

## Every bending joint, measured. Only the shoulder fails.

ADR 0016 widened 16.4 from the shoulder to every bending joint, on the reasonable suspicion that
elbows, knees, hips, neck and wrists came out of the same automatic bind and had never been posed
either. They had not been. **They also do not tear.** ● real output, shipped body:

```
  shoulder_r  140 deg: worst 3.513, 71 edges >50%, 32 >100%   <-- TEARS
  shoulder_l  140 deg: worst 3.691, 65 edges >50%, 26 >100%   <-- TEARS
  elbow_r     140 deg: worst 0.781,  4 edges >50%,  0 >100%
  elbow_l     140 deg: worst 0.832,  4 edges >50%,  0 >100%
  hip_r       110 deg: worst 0.999, 37 edges >50%,  0 >100%
  hip_l       110 deg: worst 0.915, 30 edges >50%,  0 >100%
  knee_r      130 deg: worst 0.652, 11 edges >50%,  0 >100%
  knee_l      130 deg: worst 0.627,  4 edges >50%,  0 >100%
  ankle/wrist/neck/spine/chest                     0 >100%
```

All 58 torn edges are shoulder. **The hip is one thousandth under** (0.999) and worth knowing
before anyone widens a hip range.

## Why weighting alone cannot close it — the law, measured

Sweeping the shipped body from 40° to 140° and dividing the worst strain by the rotation's own
kinematic factor gives a nearly constant ratio:

```
   deg     worst   worst / 2sin(t/2)
    40d    0.902        1.319
    70d    1.805        1.574
   100d    2.687        1.754
   140d    3.513        1.869
```

So **`worst ≈ K · 2·sin(θ/2)`**, and `K` is the part weighting controls. Reweighting drives K from
**1.87 to 0.84** — the 1.00-weight cliff falls from 53 vertices to 5. But zero-over-100 % at 140°
needs `K < 1/1.879 = 0.53`, and every configuration tried plateaus well above it: band widths
6–45 cm, 4–20 smoothing rounds, tapered and hard prunes, dense and decimated meshes.

The reason is geometric and it is why widening the band never helped. A vertex at radius `r` from
the joint displaces by `2r·sin(θ/2)`, so spreading the band to radius `R` gives it gradient `1/R`
and displacement `∝ R` — the two cancel. **Strain is scale-invariant in the band width.** That is
exactly the plateau the scans measured, and it means no weighting reaches B-31 at 140° on one
joint.

## What does work: two joints. Measured, on the rig we already have.

`Chest` stands in for the clavicle the rig does not have. 140° of total shoulder rotation,
carried two ways, on both bodies:

| | one joint (140 arm) | split 70 arm + 70 chest |
|---|---|---|
| shipped weights | 3.513 worst, **32** edges >100 % | 1.672, **20** |
| reweighted | 1.578, **18** | 1.197, **1** |

**Neither half reaches zero alone; together they take it from 32 to 1.** And `Chest` is a *poor*
stand-in — it swings the entire torso, where a real clavicle carries only the shoulder girdle — so
a real one should do better than this.

This is the clean evidence ADR 0015 required before spending a rig-version event: *"if a properly
weighted shoulder still tears at 140°, that is a clean measurement that one joint cannot carry the
rotation."* The shoulder is now properly weighted, and it still tears.

## What landed, and what did not

**Landed: §9.8 / B-31 as an executable gate** (`every_bending_joint_survives_its_working_range`,
task 16.6). It poses the body at fifteen joint cases and measures per-edge strain with the
animation session's own metric. Every joint but the shoulder is asserted at zero outright. The
shoulder is a **recorded B-31 violation, pinned** at its measured 32/26 edges so it cannot quietly
get worse while the ruling is open; the pin and the `!shoulder` exception retire together when the
clavicle lands. A body that has not been posed has not been accepted — from now on it cannot be.

**Not landed: the reweighting.** It is a real improvement (K 1.87 → 0.84, cliff 53 → 5, and at 30°
where locomotion lives, edges over 50 % go 5 → 0) and it does not meet B-31 on its own. Landing it
alone would spend a contract-version event — six garment re-bakes — on a partial fix, and ADR
0011's own rule is that such work rides an existing event rather than causing its own. The
clavicle is that event. It also still carries the two problems section 3 recorded: degenerate
triangles from the face-split bisect, and the fact that reweighting silently re-cuts the region
partition.

**Recommendation: rule the clavicle, then land reweighting + clavicle + re-bake as one
contract-version event, and delete the pin.**
