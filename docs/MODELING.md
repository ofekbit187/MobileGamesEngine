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

## 1. Style

**Grounded realism at low resolution.** Real proportions and real anatomy,
resolved coarsely — not stylized, not cartoon, not photoreal.

- **Proportions are measured, not felt.** A 1.75 m human is 7.4 heads tall,
  0.42 m across the shoulders, ~0.36 m across the hips. Deviating from human
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
| Wearable / garment | ≤ 600 | ≤ 350 | ≤ 150 |
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
- **Bone influences**: at most 4 per vertex (the hardware-skinning maximum),
  at most 2 in engine-shipped models.
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
5. **Region segmentation.** Parts a wearable can cover are separate closed
   shells, so masking a region leaves no hole (CHARACTERS.md §5.1).
6. **Smooth normals, welded across seams.** Weld by exact position, never by a
   hashed position: a hash collision averages two unrelated normals and shows
   up as a black facet somewhere unrelated.
7. **Deterministic output.** Same inputs, same vertices — models are content,
   and content that changes between builds cannot be diffed or trusted.

## 4. Rigging & animation

- One canonical humanoid rig (17 joints, `Joint` in `humanoid.h`). Everything
  humanoid binds to it; no per-model skeletons.
- Bind pose is the rig's rest pose. Model in bind space; never bake a pose.
- **Variants are palette scale, never new geometry** (ADR 0007). If a
  proportion cannot be expressed as a per-joint scale or a morph delta, it does
  not belong in the variant schema.
- Weights are authored per ring, not painted per vertex: predictable, diffable,
  and impossible to leave a stray influence behind.

## 5. UVs and textures

- The template UV chart is fixed: every region owns an island, and every skin
  texture ever made for the engine conforms to it (CHARACTERS.md §4).
- UVs are laid out even at 1:1 scale across regions — a texture must not be
  sharper on the hands than on the torso.
- UVs ship before textures do. Retro-fitting a chart invalidates every texture
  authored against the old one.

## 6. Definition of done

A model is done when all of the following hold, and each is a test, not an
opinion (see `tests/test_body_mesh.cpp` for the worked example):

- [ ] **Closed and consistently wound** — every directed edge appears once and
      its opposite exists, per shell.
- [ ] **No degenerate triangles.**
- [ ] **Normals point outward**, welded across seams.
- [ ] **Human-measured proportions** — checked numerically, not by eye.
- [ ] **Inside budget** at every LOD, with LODs strictly decreasing and
      silhouettes matching within 3 %.
- [ ] **Skin weights valid** — sum to 1, ≤ 4 influences, no influence from a
      joint far from the vertex.
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
