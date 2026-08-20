# Modelling guide — the engine's 3D model standard

The rules every model shipped with this engine follows: what the models look
like, what they may cost, and what must be true before one is called done.
Governed by the [principles](PRINCIPLES.md) — **P1 (memory efficiency) decides
every tie** — and by [CHARACTERS.md](CHARACTERS.md) for anything a character
wears or is.

The first model built to this standard is the humanoid template body
([ADR 0007](adr/0007-humanoid-template-body.md)); it is the reference for the
ones that follow.

---

## 0. Where models come from (owner ruling)

**Models are sourced, not originated.** No session sculpts a mesh, lays out a UV chart by
eye, or hand-authors an animation curve — a session cannot see, and the experiment of
having one model the body proved it (slow, expensive, and the chart shipped broken).
Content arrives from outside: CC0/appropriately-licensed libraries, the P5 fulfillment
pipeline, or a commissioned artist. Sessions do the measurable half — validation against
this document's gates, mechanical processing, rigging, retargeting, export. The aesthetic
verdict belongs to the owner, on renders, via the review board.

## 0. Where geometry comes from

**Look for a good base mesh before modelling one.** Two revisions of the
humanoid body were generated from code and both read as mannequins; the third
is Blender Studio's CC0 base mesh and reads as a person. Anatomy that took
character artists years to get right is not worth re-deriving from a profile
table.

When a suitable base mesh exists, the pipeline is: **place it, rig it, reduce
it, export it** — and *never edit its geometry*. The corollary is the rule that
governs everything in `tools/model/`:

> **Fit the rig to the mesh. Never bend the mesh onto the rig.**

A rig is cheap to move and free to get wrong twice. A base mesh warped towards
an idealised skeleton is destroyed, and the damage is hard to see until it is
animated. If the model's authored stance is a relaxed A-pose, then that stance
*is* the engine's bind pose, and the engine's `buildSkeleton()` is what changes.

Base meshes must be **CC0 or otherwise unencumbered**, and are **build-time
inputs**: the large source file is not committed, the baked `.mgeskin` asset is,
and the download is documented in the authoring script. Credits live in §7.

---

## 1. Style

**Grounded realism at low resolution.** Real proportions and real anatomy,
resolved coarsely — not stylized, not cartoon, not photoreal.

- **Proportions are measured, not felt.** A 1.75 m human is ~7.3 heads tall,
  ~0.46 m deltoid to deltoid (0.37 m shoulder joint to shoulder joint), ~0.35 m
  across the hips, with the ankle ~0.12 m off the ground. Deviating from human
  measurement is what makes a model read as a toy.
- **Silhouette carries the model.** Every triangle should buy outline: the
  deltoid, the calf, the jaw, the arch of a foot. Detail that only exists in
  shading is a texture's job, not geometry's.
- **No feature the budget cannot finish.** Eyes and mouths modelled at 2k
  triangles read worse than a clean, featureless face that a texture completes.
  Better to omit a feature than to suggest it badly.
- **The engine's visual identity is medieval, book-and-paper** (P6, owner
  verdict). Props and wearables lean handmade — cloth, leather, plate, wood —
  never sci-fi or glossy.

## 2. Budgets

Triangle budgets are per model at LOD0, on the assumption that several are on
screen at once:

| Content | LOD0 | LOD1 | LOD2 |
|---|---|---|---|
| Humanoid body (crowd content) | ≤ 2 200 | ≤ 1 300 | ≤ 650 |
| Wearable / garment | ≤ 900 | ≤ 450 | ≤ 200 |
| Held item (weapon, tool) | ≤ 400 | ≤ 200 | ≤ 80 |
| Prop, small (crate, stool) | ≤ 300 | ≤ 150 | — |
| Prop, large (building shell) | ≤ 1 500 | ≤ 700 | ≤ 250 |

Published guidance for mobile puts hero characters at 5k–20k triangles, and
recommends staying under ~5k for a steady 60 fps on mid-range Android. These
budgets sit deliberately below that: this engine draws crowds in an open
streamed world, and P1 ranks the working set above per-model fidelity.

Also budgeted:

- **Vertex cost**: 36 B skinned (position, normal, uint16 UV, 4 joints, 4 uint8
  weights), 24 B static. Vertex *count* matters as much as triangles — a UV
  seam or a hard edge duplicates vertices.
- **Bone influences**: at most 4 per vertex (the hardware-skinning maximum).
  Shipped models should average close to 2 — the template body averages 2.2 —
  because the skinning inner loop runs once per influence.
- **Draw calls**: one model is one draw per material. Splitting a body into
  more materials than it has textures is a regression.

## 3. Topology rules

1. **Quad-derived loops.** Model in rings/loops around the form; triangulate
   at the end. Loops follow the muscles under the skin.
2. **Three loops across a bending joint** — above, at, below — with the loop at
   the joint weighted 50/50 across the two bones. Fewer collapses the joint.
3. **Closed shells.** Every part is watertight and consistently wound. Open
   edges become backfaces the moment a camera or an animation moves.
4. **Caps must be hidden or rounded.** A flat end-cap that pokes out of the
   surface it was meant to hide reads as a plate stuck on the model — the
   commonest defect in part-assembled bodies.
5. **Region segmentation.** A model is one watertight shell; the regions a
   wearable can cover are a *partition of its triangles*, derived from each
   vertex's dominant bone. Masking is then an index-range decision, and it
   leaves no hole because the garment that triggered it was cut from the very
   triangles being removed (CHARACTERS.md §5.1).
6. **Smooth normals, welded across seams.** Weld by exact position, never by a
   hashed position: a hash collision averages two unrelated normals and shows
   up as a black facet somewhere unrelated.
7. **Deterministic output.** Same inputs, same vertices — models are content,
   and content that changes between builds cannot be diffed or trusted.

## 4. Rigging & animation

- One canonical humanoid rig (17 joints, `Joint` in `humanoid.h`). Everything
  humanoid binds to it; no per-model skeletons.
- **The model's authored stance is the bind pose** — for the humanoid, the
  base mesh's relaxed A-pose. The rig is fitted to it (§0); the mesh is never
  posed to match a rig, and no animation is ever baked into the geometry.
- **Variants are palette scale, never new geometry** (ADR 0007). If a
  proportion cannot be expressed as a per-joint scale or a morph delta, it does
  not belong in the variant schema.
- **Variant scale is applied in the bone's own frame**, +Y along the bone. In
  an A-pose bind no limb is axis-aligned, so scaling in character axes makes a
  bulky character's arms longer instead of thicker.
- **What scaling cannot say is a morph target** (ADR 0009): a ribcage, a
  belly, a brow. Author them PARAMETRICALLY — a region measured against
  landmarks found on the model, and a displacement over it — never as a hand
  sculpt. Parametric targets are deterministic, diffable, re-derivable when
  the base mesh changes, smooth by construction (so they cannot tear the
  mesh), and a test can read them. Generate them per LOD, AFTER decimation.
- **A morph target stores one direction.** The engine negates it for the other
  half of the range, so a target that is not an exact mirror of itself is a
  bug — and storing both halves is a P1 regression.
- **Weights are pruned, not just normalised.** Bone-heat weighting is a
  diffusion solve and it leaks across joints — on the template body it left the
  ankle ~10 % *thigh* influence, which reads as the foot swimming when the knee
  bends. Keep an influence only if its bone is within ~10 cm of the closest
  bone influencing that vertex.
- **Re-clamp and re-prune after every decimation.** Collapsing an edge merges
  the influence sets of the two vertices, so a reduced LOD silently regains
  both a fifth influence and the leakage the full mesh was cleaned of. The
  export is what ships, so the export is what must be checked.
- Measure a weight's reach against the **bone** (joint to its first child), not
  against the joint: a long femur legitimately moves vertices far from the hip,
  but nothing it moves is far from the femur.

## 5. UVs and textures

- The template UV chart is the base mesh's own unwrap, carried through import
  and fixed from then on: every skin texture ever made for the engine conforms
  to it (CHARACTERS.md §4).
- UVs are laid out even at 1:1 scale across regions — a texture must not be
  sharper on the hands than on the torso.
- UVs ship before textures do. Retro-fitting a chart invalidates every texture
  authored against the old one.
- **The current chart does not meet the rule above.** `tools/uv_report`
  measures it: density runs 387–1377 px/m across regions (3.6×, hands 2.5×
  sharper than the torso), arms and neck stretch over 3× inside their island,
  the two hand shells overlap on identical texels, and the `Face` island holds
  no geometry. The measurements, the consequences, and the fix are in
  [TEXTURING.md §6](TEXTURING.md#6-the-template-uv-chart--measured). Because
  no texture has been authored yet, this is the moment to fix it — the chart
  belongs to the model, so the fix is modelling work.

Everything downstream of the chart — the map set, colour spaces, compression,
budgets, texel density, and what makes a texture *done* — is
[TEXTURING.md](TEXTURING.md).

## 6. Definition of done

A model is done when all of the following hold, and each is a test, not an
opinion (see `tests/test_body_mesh.cpp` for the worked example):

- [ ] **Closed and consistently wound** — every directed edge appears once and
      its opposite exists, over the whole mesh, and the signed volume is
      positive and physically plausible.
- [ ] **No degenerate triangles.**
- [ ] **Normals point outward**, welded across seams.
- [ ] **Human-measured proportions** — checked numerically, not by eye.
- [ ] **Inside budget** at every LOD, with LODs strictly decreasing and
      silhouettes matching within 3 %.
- [ ] **Skin weights valid** — sum to exactly 255, ≤ 4 influences, and no
      influence whose bone is far from the vertex compared with the nearest
      bone influencing it. Checked on the *baked* asset, not on the source.
- [ ] **Survives its extremes** — bend every joint to its limit; the joint loop
      keeps ≥ 75 % of its girth and no vertex flies away.
- [ ] **Wearables fit and layer** — every layer encloses the one beneath, on
      every variant, with no bare gap between garments.
- [ ] **Deterministic** — two builds are byte-identical.
- [ ] **Looked at.** Render it — front, three-quarter, side, back, plus a
      close-up of the head and of the hips — and *look*. Every defect fixed in
      the template body (missing neck, boxy pelvis, plates on the shoulders,
      palms facing the wrong way, a gap between hem and boot) was found by
      looking at a render, not by reading numbers. `tools/body_preview` exists
      for exactly this.


## 7. Third-party geometry

| Model | Source | Licence |
|---|---|---|
| Humanoid template body | [Blender Studio — Human Base Meshes](https://www.blender.org/download/demo-files/) bundle v1.4.1, object `GEO-body_male_realistic` | CC0 (public domain) |

CC0 requires no attribution; the credit is here because knowing where a model
came from is part of being able to re-derive it. The bundle is downloaded from
<https://download.blender.org/demo/asset-bundles/human-base-meshes/> and placed
in `tools/model/vendor/` (or pointed at with `MGE_HUMAN_BASE_BLEND`); it is not
committed — the baked assets in `assets/models/` are.
