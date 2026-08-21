# Task 13.7 — the Face shell, and the two numbers ADR 0011 asked for

**From:** the character asset pipeline session · **To:** the architect, cc the owner, the wearables and textures sessions
**Subject:** `BodyRegion::Face` ships. `chart_regions_required` passes, `stretch_above_max` is now the lone refusal — ADR 0011's named trigger — and it moved for a reason ADR 0011 did not anticipate.
**Closes:** the `B-8` debt ADR 0008 logs against v3 · **Evidence:** `mge_uv_report`, `ctest`, `mge_body_preview`

---

## 1. What shipped

**New body content hash: `c4f91ac3fa2fcff5`** (1 961 vertices, 2 200 triangles, 12 parts).
Trio + re-cut garments + re-baked `.mgefit` bindings, one atomic push, as ADR 0008 requires.

`BodyRegion::Face` is real geometry on all three LODs — 301 triangles at LOD0, one shell,
its own UV island, 100 % usable. Twelve of twelve regions now own geometry.

## 2. The rig cannot answer this, so the asset says it

Worth recording because it is the whole shape of the solution. `regionOf()` reads the bone
that moves a vertex, and the rig has **one Head joint** (B-1 fixes the 17). So every head
vertex was Scalp *by construction* and `Face` had been empty since the body was first
imported — not an oversight, a structural impossibility. A Face joint would be a
rig-version event buying a region that is not a moving part: **the face is a masking
division of the head, not an articulated one.**

So the asset labels its regions and `gltf_skin_import` reads the label, falling back to the
rig when a primitive is unlabelled.

**All twelve regions are labelled, not just Face** — and that was not the first attempt.
Labelling Face alone left the other eleven derived from the rig on the engine's side while
the chart was packed from the rig on this side: two inferences that have to agree, and they
did not. `chart_islands_disjoint` failed on Scalp/Neck over a handful of triangles at the
seam — the same class of failure that cost three attempts during the repack, because region
bounding boxes are unforgiving and "usually agrees" is not good enough. With all twelve
labelled there is nothing left to disagree about: **the region a triangle is packed into is
the region the engine reads back.** It also gives `B-25`'s per-region vertex groups
(task 13.8) their door.

**One bug this exposed, fixed here:** glTF hangs morph targets off each *primitive*, and the
importer emitted one `MorphTarget` per (primitive, parameter). A body split by material
therefore turned 15 shape parameters into 25 targets, with `face.jawWidth` appearing twice
and `mesh.morph()` returning whichever came first — half the jaw would have moved. Targets
are now merged across primitives, with the quantization scale taken over all of them. The
single-primitive path is byte-identical: the previously shipped body re-imports to the same
1 916 vertices / 15 targets / 4 282 deltas it always did.

## 3. The stretch numbers ADR 0011 asked for — and a correction to its reasoning

ADR 0011 deferred the `stretch_above_max` decision to 13.7's completion on the grounds that
"13.7 re-topologises the head, which may move the head region's stretch reading on its own."

**It did move, and not for that reason. 13.7 does not re-topologise anything** — it
classifies existing geometry into two regions. The reading moved because splitting the head
stopped *averaging* a low-stretch cranium together with a high-stretch face:

| | before 13.7 | after 13.7 |
|---|---|---|
| Scalp (whole head, then cranium only) | 1.69 | **1.36** |
| Face | — (did not exist) | **2.09** |
| worst region on the body | Hands 1.97 | **Face 2.09** |
| regions over the 1.50 rule | 8 of 11 | 8 of 12 |
| mean density | 505 px/m | 480 px/m |

So the split **improved** the cranium and **exposed** a face that was always the worst part
of the source's unwrap, hidden inside an average. The body did not get worse; the
measurement got honest. The worst reading on the body is now 2.09×, in the region a player
looks at most.

```
SEAM: Body contract (UV chart) ⇄ textures standard — ADR 0011 Ruling 1, now due
NEED: `chart_regions_required` passes, so `stretch_above_max` is the LONE refusal and
      the gate is red for exactly one known reason — the state ADR 0011 says must not
      persist. The numbers it wanted before deciding are above.
BREAKS: Nothing shipped. It gates whether a skin texture may be authored.
PROPOSAL: The waiver ADR 0011 already designed (per-asset, in
      `assets/standards/skin_texture.mgestd`, surfacing as `WAIVED`, naming asset, rule,
      measured value, reason and retirement condition). I would set its retirement
      condition on the FACE specifically: 2.09x is inherited from the CC0 source's
      forehead-and-nose unwrap and is the one region where a commissioned re-unwrap
      would clearly pay — and per ADR 0011 that re-unwrap rides a future
      contract-version event rather than causing its own.
```

## 4. What is missing, and why I did not just take it

**`B-9`'s authored hairline loop is not delivered.** The boundary is classified per face —
each head face goes to Face or Scalp whole, by which side of the planes its centre falls on
— so it steps one face wide instead of following a real edge loop.

I implemented the cut and measured it rather than arguing about it:

- bisecting the hairline plane costs **+74 triangles**, the ear plane **+128**;
- LOD0's `B-5` budget is **2 200** and the body already sits exactly at it;
- paying for them by decimating to 1 990 first **fails `body_mesh_has_human_proportions`**.

That last one is the interesting measurement, and it is not a rounding error. The gate
samples the Torso region's width in a 60 mm band at 0.82–0.88 m. Measured across decimation
targets, with region assignment held constant:

```
target 2200: hips 0.3468 m   <- passes (rule: 0.30 < hips < 0.44)
target 2100: hips 0.1457 m   <- fails
target 1990: hips 0.1457 m   <- fails
```

The collapse is **discrete**: below 2 200 the Torso region stops reaching into the band at
all and the gate measures a much narrower part of the body. Two things follow. First, the
loop cannot be bought out of this budget — it needs either a larger LOD0 cap (`B-5`, an
architect ruling) or the head's own triangles spent on it, and both are budget decisions
above this session's pay grade, which is exactly the budget question ADR 0010 anticipated
"when I get there". Second, and worth a line of its own:

> **`body_mesh_has_human_proportions` passes by one vertex.** The body's hips do not change
> width between 2 200 and 2 100 triangles; only whether a Torso-labelled vertex happens to
> land inside a 60 mm sampling band. Any future change to the LOD0 budget will trip this
> gate for a reason that has nothing to do with the body's proportions. I have not touched
> it — a gate that moves to fit the content is the thing ADR 0011 just refused — but it is
> measuring a sampling artifact and somebody should know that before it fires again.

Everything task 13.7 exists for is delivered without the loop: the region is real,
independently maskable, and owns its own texture space. `docs/TASKS.md` 13.7 is `[~]` with
the loop named as the remainder.

## 5. Evidence

```
mge_uv_report   6 of 7 rules pass; stretch_above_max the lone refusal
                12 of 12 regions own geometry, every region at 480 px/m, 0% spread
                chart_islands_disjoint: no two regions share texture space
                chart_min_utilisation: 64.0% (floor 55%)
ctest           12/12, including the two new Face tests
host runner     steady-state heap allocations: 0
mge_garment_fit all six garments re-baked against c4f91ac3fa2fcff5
```

New tests, and the exemption that is gone:

- `body_mesh_covers_every_region` — the `if (region == Face) continue;` skip is **deleted**.
  It had been there since v1. If it ever fails again the body has regressed, and no skip is
  coming back.
- `the_face_is_a_real_region_in_front_of_the_scalp` — Face exists, sits forward of the
  Scalp, shares its height band, and owns the most forward point of the head. (Deliberately
  the head, not the body: the toes reach further forward than the nose, −0.187 m against
  −0.169 m, which the first version of this test got wrong and the test caught.)
- `a_visor_can_hide_the_face_without_hiding_the_scalp` — both directions: dropping Face
  leaves the bald cap standing (`B-9`'s fallback), dropping Scalp leaves the face.
