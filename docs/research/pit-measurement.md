# Measuring whether a body can be dressed

*Wearables session, 2026-08-21. Written while implementing task 13.10 (the
BODY_CONTRACT.md §9 acceptance gates). It records two corrections to the
contract and three ways of measuring that look right and are not — because
each of them produced a confident, specific, wrong number, and the next person
to touch this will reach for the same ones.*

---

## The question

A garment sits a few millimetres off the skin. Wherever two parts of the body
come close enough to each other, the garment on one side and the garment on
the other side collide — and no amount of good authoring fixes it, because the
space is not there. B-24 exists to measure that space before a catalogue is
built on top of it.

Call such a place a **pit**: two surfaces that are far apart *along the body*
but near in space. The armpit, the crotch, the underside of the jaw.

## Three wrong ways to measure it, in the order I tried them

**1. Offset the whole surface and look for folds.** This is what B-24
originally specified: push every vertex out along its normal by 35 mm and
check nothing self-intersects. It fails on every region of a perfectly good
body — 82 folded triangles on the face alone.

The reason is not a defect: a normal offset folds wherever the surface's
concave radius of curvature is smaller than the offset. A nose, a pair of
lips, the web between thumb and palm all have features under 35 mm. But a
garment does not conform to a nose — it *bridges* it. The test measures
surface curvature and then reports it as a dressability failure.

**2. Compare triangle centroids.** Fast, and it invents gaps that are not
there. It reported an armpit clearance of 11.7 mm, which sent me looking for a
fix to a problem the body does not have. Measured properly the armpit is
38.8 mm — the roomiest of the real pits. A centroid is up to half a triangle
away from the surface, and on a coarse mesh that is centimetres.

**3. Measure between regions.** The obvious framing — "how close does the arm
get to the torso?" — returns **0.0 mm for almost every pair**, and the zero is
correct. The delivered body is *one shell* whose regions partition its
triangles; neighbouring regions share their boundary edges, so the distance
between them is exactly zero at every partition line. (The ADR 0007 v2 body
*did* have per-region closed shells. The contract was written against that
body and quietly stopped applying when v3 imported an artist mesh.)

## What actually works

Exclude points that are close **along the surface** and keep the ones that are
far along the surface but near in space. That is what a pit *is*.

- Weld the mesh by exact position first, or the surface graph is cut at every
  UV seam and "far along the surface" is measured on a mesh full of holes.
- Run Dijkstra over **edge length**, not hop count. A hop crosses ~5 mm on the
  dense face and ~25 mm on a thigh, so a hop-count threshold measures a
  different thing in every region — I watched the answer swing between 16 mm
  and "nothing found" as I varied it. In metres the answer is stable.
- Exclude everything within a **120 mm geodesic radius**, then take the
  closest remaining triangle.

Implemented as `measurePits()` in `engine/src/import/wearable_gates.cpp`.
Runtime on the shipped body is ~0.19 s for all three shapes — cheap enough to
be a gate rather than an occasional audit.

## What the shipped body measures

Minimum across the template and both shape extremes, body `c4f91ac3fa2fcff5`:

| Pit | Clearance | Where |
|---|---|---|
| Inner thigh (LegL–LegR) | **1.0 mm** at max shape | y = 0.72 m |
| Arm to itself (shape min) | 12.0 mm | y = 0.99 m |
| Under the jaw (Face–Neck) | 14.7 mm | y = 1.54 m |
| Thumb to palm | 16.5 mm | y = 0.75 m |
| Across facial features | 18.8 mm | y = 1.55 m |
| Armpit (Torso–ArmL) | 38.8 mm | y = 1.23 m |

**None of these is a defect.** A heavy person's thighs touch; a jaw really
does overhang a throat. The shape range is clamped just short of
self-intersection, which is the correct place for it to stop. What they are is
a **published constraint on the catalogue**:

> Two *independent* garment layers cannot both sit across a pit. A garment
> covering both sides has to span it as a single surface — the way real
> trousers span a crotch, and real sleeves span an armpit.

The trouser and tunic archetypes already satisfy this by construction. It
becomes a live constraint the moment someone authors separate per-leg greaves
or a vest-plus-sleeves outfit at the same layer.

## The second correction: "no hole" is unachievable, "covered" is the point

§9.1 asked that masking any combination of regions leave "no hole and no
exposed backface". On a one-shell body, masking a region **necessarily** opens
a boundary — that is what cutting triangles out of a continuous surface does.
The gate as written could never pass.

What matters instead is that the rim a mask opens is **covered by the outfit
that opened it**: the cut line lives under the cloth, exactly as a hidden
geoset boundary does in the games that pioneered this.

That also has to be judged **per outfit, not per garment**. Judged alone, the
armour leaves 67 mm of rim bare at the hip — but armour is an outer layer
meant to sit over a tunic, and the tunic covers that rim. Judged as outfits,
every combination the catalogue can produce is covered, worst case 42 mm
against a 45 mm allowance.

*That 42 mm is worth watching.* It is the trouser hem at the ankle with no
boots on, and it leaves only 3 mm of headroom. The first authored trouser
(13.11) should reach further down the shin than the current placeholder does.

## For whoever touches this next

- The gates live in one place and are run from two: `tools/wearable_gates`
  (self-serve, P12) and `tests/test_wearable_gates.cpp` (CI). Keep both.
- `recordedPits()` is a *measurement of a specific body*. When the body
  changes it must be re-measured, exactly like the `.mgefit` bindings — the
  gate fails and names the tool.
- If a gate ever seems to fail everywhere at once, suspect the measurement
  before the body. All three mistakes above did precisely that.
