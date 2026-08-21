# The template body contract — requirements from the wearables system

What the remade humanoid template body must provide — and must never silently
change — for wearables to be authored natively against it and simply work: on
every variant, in every pose, through every animation, hair included.

**Audience:** the project manager (scope, process, open decisions) and the
body-modeling session (precise technical requirements). **Author:** the
wearables engineering session. **Basis:** CHARACTERS.md §4–§6 (dictated
behaviour), MODELING.md (the model standard), ADR 0007 (the current body's
architecture), and `docs/research/wearables.md` (the industry study behind
every requirement here — each cites its lesson).

Requirements are numbered `B-*` for traceability. **MUST** items gate
acceptance; **SHOULD** items are strong recommendations with the trade-off
stated; **DECIDE** items need a PM/owner ruling before modeling starts.

---

## 0. Why the body model owns these requirements

A wearable is authored *once, against the template body*, and the engine makes
it fit every variant (CHARACTERS.md §5.1). Every mechanism that delivers that
promise — weight transfer, surface binding, region masking, layer offsets —
reads the body as its reference data. The body is therefore not just a model;
it is **the authoring contract of the entire wearables catalog**. The research
found the two costliest failures in this domain (WoW's unretrofittable hair,
Second Life's unfittable clothes) were both *base-body decisions*, made before
the first garment existed. This document exists so we make those decisions on
purpose.

## 1. Fixed engine interfaces (not negotiable by modeling)

These are the interfaces the body plugs into. Changing any of them is an
engine-design event owned by the owner's dictation, not a modeling choice.

- **B-1 (MUST)** — Bind to the **canonical 17-joint rig** (`Joint` in
  `engine/include/mge/character/humanoid.h`: Hips, Spine, Chest, Neck, Head,
  UpperArm/Forearm/Hand L+R, Thigh/Shin/Foot L+R). No added, removed, or
  reordered joints. Finger joints do not exist (see D-2).
- **B-2 (MUST)** — Skinning: **≤ 4 influences per vertex** (hardware maximum),
  **≤ 2 in the shipped body** (MODELING.md §2); weights sum to 255 in the
  uint8 encoding; no influence from a joint far from the vertex.
- **B-3 (MUST)** — Vertex format is the 36-byte `SkinVertex` (position,
  normal, uint16 UV, 4 joint indices, 4 uint8 weights). Nothing the model
  needs may require a different layout.
- **B-4 (MUST)** — Proportions live in the **skinning palette, never in
  geometry** (ADR 0007): one mesh serves every character; variants are
  per-joint scale + morph deltas. If a body feature cannot survive per-joint
  scaling, it must be redesigned or expressed as a morph.
- **B-5 (MUST)** — Triangle budgets at the MODELING.md caps: body
  **≤ 2 400 / 1 300 / 650** at LOD0/1/2, LODs strictly decreasing,
  silhouettes matching within 3 %. (LOD0 raised from 2 200 by ADR 0012 to fund
  B-9's authored hairline loop: the crowd draws LOD1/LOD2, so the budget P1
  actually cares about is unchanged. LOD1 and LOD2 do NOT move — a cap raised at
  every level would be the standard bending to fit the content. The current body
  ships 2 200/1 200/560.)
- **B-6 (MUST)** — Delivered through the engine's import path as
  `SkinnedMeshData` (glTF in): the generator is replaced *behind the same
  interfaces* — nothing downstream may need to know the body is now imported.

## 2. Region segmentation — the masking contract

Masking is how clothes never clip: a covered region is simply not drawn
(index-range skip). This only works if regions are cut correctly *in the
model*.

- **B-7 (MUST)** — Every body region is a **closed, watertight, consistently
  wound shell**, mapped to a contiguous index range (`MeshPart`). Dropping any
  region whole must leave **no hole and no exposed backface** — shells overlap
  *inside* the body where seams would show.
- **B-8 (MUST)** — Minimum region set = the current twelve
  (`BodyRegion`): Scalp, Face, Neck, Torso, ArmL/R, HandL/R, LegL/R, FootL/R.
- **B-9 (MUST)** — The **scalp is a proper cap**: closed cranium geometry that
  always renders acceptably on its own (the "bald fallback"). Every hairstyle
  and helmet interaction degrades to this cap; it can never look broken. The
  hairline (cap/face boundary) is an authored loop, not an accident.
- **B-10 (SHOULD)** — Cut **additional shell seams at the elbow and the knee**
  (upper-arm/forearm, thigh/shin as separately maskable closed sub-shells).
  This is the WoW lesson applied to limbs: without these cuts, a short-sleeve
  tunic can only mask the *whole* arm, so it must cover the whole arm forever.
  Cost: one duplicated ring of vertices per cut (small); benefit: sleeve- and
  hem-length freedom for the entire future catalog. If accepted, the engine's
  `BodyRegion` set will be extended to match (wearables-session work).
- **B-11 (MUST)** — **Hem loops**: garments end on edge loops shared with the
  body, or they leave gaps when masking removes the limb beneath (the
  hem-and-boot rule already in MODELING.md). The body must publish a named
  table of canonical loops — neck base, shoulder, mid-upper-arm, elbow,
  wrist, waist, hip, mid-thigh, knee, boot-cuff (ankle + ~0.16 m), ankle —
  with their heights on the template. Wearable authoring terminates garment
  openings on these loops.
- **B-12 (DECIDE, D-1)** — **Ears.** If the new head has ear geometry, ears
  must be their own maskable sub-shells (helmets hide ears — WoW masks them
  per-helmet). If ears stay texture-only, no requirement. PM to rule with the
  head design.

## 3. Topology is a versioned contract

The import-time fitting pipeline (research §4) bakes, per garment, skin-weight
transfer and a **surface binding**: each garment vertex stores a body triangle
index + barycentric coordinates + offset. Morph deltas likewise address the
body's vertices by index. Consequence:

- **B-13 (MUST)** — **Frozen topology and vertex order.** The delivered body's
  triangulation and vertex ordering are part of the engine's data contract.
  Re-exporting with different topology/order — even "the same shape" —
  invalidates every garment binding and every morph in existence. Topology
  changes are **format-version events**: announced, versioned, and paired
  with a re-bake of all wearables. Never silent.
- **B-14 (MUST)** — **Deterministic export**: same source in, byte-identical
  mesh out (MODELING.md §3.7). The contract version is a content hash the
  build records; the wearables pipeline refuses a binding whose body hash
  doesn't match.
- **B-15 (MUST)** — Topology quality per MODELING.md: quad-derived loops
  following musculature, **three loops across every bending joint** with the
  joint loop weighted 50/50, smooth normals welded by exact position, no
  degenerate triangles.
- **B-16 (MUST)** — **Joint-extreme integrity**: at every joint's limit pose,
  the joint loop keeps **≥ 75 % of its girth** and no vertex flies away
  (existing gate). For wearables this is load-bearing, not cosmetic: a
  collapsing knee collapses every trouser layered on it.

## 4. Bind pose, rig fit, and attachment points

- **B-17 (MUST)** — Model **in the rig's bind pose**; never bake a pose into
  the mesh. The bind-pose joint positions (`templateBindPositions()`) are the
  reference; if the artist body genuinely needs different bind offsets (limb
  placement, A-pose adjustments), that is a **rig-version event** coordinated
  with the wearables and animation work — every animation and garment binds
  to it.
- **B-18 (MUST)** — Model at the **template proportions** (`templateVariant()`:
  1.75 m, 7.4 heads, 0.42 m biacromial — the measured-human rule of
  MODELING.md §1). Every variant is this body rescaled; garments are authored
  against exactly this shape.
- **B-19 (MUST)** — **Attachment points survive remodeling**: palm grip
  points L/R (held items), and sheath anchors `hip_l`, `hip_r`, `back`
  (CHARACTERS.md §6.1). The research settles the convention (Roblox/Unreal
  pattern): socket transform on the skeleton side, grip transform on the item
  side, same-named frames aligned at attach. The modeler verifies socket
  placement against the new palm/hip/back geometry; the hand must be shaped
  so a gripped item's origin sits naturally in it.
- **B-20 (SHOULD)** — Reserve **head accessory anchor points** (crown, face
  front) even if unused now — rigid head accessories (masks, circlets,
  lanterns on helms) attach more cheaply than they skin.

## 5. Morphs and the variant envelope

- **B-21 (MUST)** — All face/chest/feature variation beyond per-joint scale
  is delivered as **sparse morph deltas on the template mesh** — same
  topology, per-vertex offsets, named, each with an affected-region manifest.
  Morphs are what the surface binding re-fits garments against; a morph that
  arrives as "a different head mesh" breaks the entire mechanism (B-13).
- **B-22 (MUST)** — Morphs stay **inside the fittable envelope**: no morph
  may self-intersect the body or fold its surface (a folded surface has no
  well-defined offset for a garment to bind to). The face sub-schema
  (CHARACTERS.md §4.1) grows within this rule.
- **B-23 (SHOULD)** — Facial-hair and brow regions: if the face gains
  geometry for brows/beard placement, keep those surface patches as
  identifiable vertex ranges — beards/eyebrows arrive later as face-slot
  wearables riding morphs (research §3.5).

## 6. Surface quality for garment offsetting

Garments sit at their layer's offset from the body: **base ≈ 8 mm, mid
≈ 19 mm, outer ≈ 30 mm** (current engine values; each layer additionally
clears the layers beneath).

- **B-24 (MUST, corrected 2026-08-21)** — The body must not close a **pit**
  — two surfaces that are far apart *along the body* but near in space — to
  the point of self-intersection, on the template *and* at the shape
  extremes. The classic pits: armpit, crotch, neck/chin, elbow and knee
  creases, between the fingers if the hand gains fingers.

  > **This clause originally read "tolerate a normal offset up to ~35 mm
  > without self-intersection", and that was wrong.** Measured against the
  > delivered body it fails everywhere for a reason that is not a defect: a
  > 35 mm offset folds around any feature whose curvature radius is under
  > 35 mm, so a nose, a thumb and a pair of lips all "fail" while being
  > perfectly dressable — garments bridge such features rather than
  > conforming to them. The clause now states the property it always
  > *meant*, which is the one the named examples point at.
  >
  > Two ways of measuring it also give confident wrong answers, both of
  > which this session tried first: comparing triangle **centroids**
  > invents gaps that are not there (it produced a fictitious 11.7 mm
  > armpit), and measuring between **regions** returns 0 mm everywhere,
  > because the delivered body is one shell whose regions share their
  > boundary edges. The measurement that means something excludes points
  > within a **geodesic radius** (120 mm along the surface) and keeps the
  > rest — implemented in `measurePits()`, gated by `gatePitClearance()`.

  **Measured on the shipped body**, minimum across template and both shape
  extremes: inner thigh **1.0 mm** at max shape (thighs nearly touch — real
  anatomy, and the shape range is clamped just short of intersecting), under
  the jaw **14.7 mm**, thumb-to-palm **16.5 mm**, armpit **38.8 mm**. These
  are *published constraints, not defects*: a modeller cannot open them
  without making the body stop looking like a person. What they mean for
  wearables is that **two independent layers cannot both sit across a pit** —
  a garment covering both sides must span it as one surface, the way real
  trousers span a crotch.
- **B-25 (MUST)** — **Vertex groups per region** exported with the body: each
  body vertex tagged with its region name. This is MakeHuman's guard applied
  to our pipeline — a garment vertex may only bind to body triangles of its
  permitted group, so a sleeve can never snap to the torso.
- **B-26 (SHOULD)** — Keep ring/loop **density roughly even** along limbs and
  trunk in coverable areas: the binding projects garment vertices onto the
  nearest body surface, and a density cliff turns into visible garment
  distortion at that height.

## 7. UV chart

- **B-27 (MUST)** — Fixed UV islands per region, even texel scale across
  regions (MODELING.md §5); **UVs ship with the body, before any texture**.
  Every skin texture ever made conforms to this chart, and the future option
  of texture-space masking (research §3.3: garment alpha masks in body UV
  space) depends on the chart being stable. UV changes are contract-version
  events exactly like topology changes (B-13).
- **B-28 (MUST)** — The scalp cap owns its own island (hair/helmet masking
  and future hair textures address it independently of the face).

## 8. Published authoring reference

The deliverable is not only a runtime asset — it is the file wearable artists
model against.

- **B-29 (MUST)** — Publish the **authoring reference export** (glTF): the
  template body at template proportions in bind pose, with the rig, named
  region vertex groups, hem-loop markers (B-11), attachment points (B-19),
  and — as they land — the morph set. This file is versioned with the same
  content hash as the runtime body (B-14).
- **B-30 (SHOULD)** — Include in the reference three posed variants (template,
  max-bulk, min-height) as *reference only* — the fastest way for a garment
  artist to sanity-check their work is to see it on the extremes they must
  survive.

## 9. Acceptance — how the body is judged done

The existing MODELING.md definition of done applies in full
(`tests/test_body_mesh.cpp` is the worked example). The wearables system adds
these gates, which the wearables session will implement as tests against the
delivered body:

1. **Mask integrity** — the rim a mask opens is **covered by the outfit that
   opened it**. (Corrected: this clause said "leaves no hole", which assumed
   the per-region closed shells of the ADR 0007 v2 body. The delivered body
   is **one shell** whose regions partition its triangles, so masking a
   region necessarily *opens* a boundary — closed is unachievable and
   covered is the property that matters. Judged per outfit, not per garment:
   armour is an outer layer meant to sit over a tunic, and a trouser hem is
   met by a boot cuff.)
2. **Pit clearance** — no pit closes to self-intersection, and every recorded
   pit still measures what was recorded, on template and both shape extremes
   (B-24, corrected above).
3. **Hem-loop table** — every named loop exists, closed, at its declared
   height (B-11).
4. **Scalp-cap fallback** — the body renders acceptably with all hair hidden
   (B-9): this is a *looked-at* gate, per MODELING.md §6.
5. **Contract hash** — export is deterministic; hash recorded; a second build
   is byte-identical (B-14).
6. **Groups & anchors** — region vertex groups cover 100 % of vertices with
   no overlap (B-25); all attachment points present and sanely placed (B-19).
7. **Existing gates** — closed/wound, proportions, budgets, LOD silhouettes,
   weight validity, joint extremes (B-15/16), and the render review: front,
   three-quarter, side, back, head and hip close-ups — *looked at*, dressed
   and bare.

## 10. Division of labor

| Who | Owns |
|---|---|
| **Body modeler** | Everything in §1–§8: the mesh, shells, loops, weights, UVs, morphs, exports. |
| **Wearables session** | The import fitting pipeline (weight transfer + surface binding), the §9 gates as tests, `BodyRegion` extension if B-10 is accepted, garment re-bakes on contract-version bumps, and the first wearable set proving the contract. |
| **PM / owner** | The DECIDE items below, catalog priorities, and the ruling on any contract-version event. |

## 11. Decisions needed before modeling starts (PM/owner)

- **D-1 — Ears** (B-12): geometry (own maskable shells) or texture-only?
- **D-2 — Fingers**: the current hand is palm+thumb; finger joints would cost
  rig slots (a rig-version event, B-1/B-17) and crowd budget. Wearables only
  need gloves to work — a mitten-topology hand is fully sufficient. Ruling:
  keep palm+thumb unless the owner dictates articulated fingers.
- **D-3 — Limb cut lines** (B-10): accept the elbow/knee sub-shells? (Recommendation: yes.)
- **D-4 — First catalog**: which wearables ship first (tunic, armor, pants,
  boots, two hairstyles exist as parametric stand-ins today)? This decides
  which regions/loops get validated hardest and what the §9 gates run against.
- **D-5 — Bind-pose change**: does the artist body keep the current bind
  offsets, or is a bind-pose adjustment wanted (B-17)? Must be ruled before
  weights are authored.

---

*Every requirement above traces to a mechanism or a documented failure in
`docs/research/wearables.md`. When this contract and that research disagree,
this contract wins — it is the operative handoff; the research is its
evidence.*
