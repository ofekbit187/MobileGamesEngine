# ADR 0008 — Wearable fitting on an artist-made body

**Status:** accepted (architect ruling) · **Date:** 2026-08-20
**Supersedes:** nothing · **Depends on:** ADR 0007 (humanoid template body)
**Evidence:** `docs/research/wearables.md` · **Operative handoff:** `docs/BODY_CONTRACT.md`

## Context

Wearables were generated from the same code profiles as the body, so "authored once, fits
every variant" held *by construction* — both sides came out of the same generator. ADR 0007
v3 replaced the generated body with an imported artist mesh. That ends the trick: the
fitting guarantee now has to survive on meshes nobody generated.

The owner commissioned the wearables session to research how this is done rather than have
the architect rule from first principles. The result is a survey of every shipped approach
(`docs/research/wearables.md`) and a thirty-requirement handoff to the modeling session
(`docs/BODY_CONTRACT.md`).

## Decision

**The runtime architecture does not change.** One rig, one shared skinning palette, a single
skinning path, closed-shell region masking by index range, layered slots. The research
confirms this is the proven mobile pattern; nothing about the frame path moves.

**What is added is an import-time fitting pipeline.** Per garment, baked once, offline:

1. **Skin weights** — accepted when authored; otherwise transferred from the body by
   confidence-gated nearest-surface copy (position *and* normal agreement), with weight
   inpainting across the vertices the gate rejects. Loose garments are exactly the case
   naive nearest-point transfer gets wrong, and inpainting is what fixes it.
2. **Surface binding** — per garment vertex: body triangle index + barycentric coordinates
   + offset, constrained to the region's vertex group so a sleeve can never snap to the
   torso.
3. **Variants** — bone-scale variants ride the palette for free, as today. *Morph*-driven
   variants re-evaluate the binding at spawn/equip on job lanes, cached per
   (garment, morph-set). Never per frame.
4. **Layering** — layer *k* binds against layer *k−1*'s outer surface offset by its
   thickness, resolved offline or at equip. No runtime cages, no runtime RBF.

**Rejected, on P1 grounds:** runtime cloth simulation, runtime cage/RBF solves, per-part
pose copying (we already share one palette). **Deferred:** texture-composited clothing, which
needs the texture pipeline before it can even be evaluated.

The frame path after all of this is what it is today: one pose evaluation, one palette
upload, one skin pass over body plus visible garments. That is the whole point.

## Why this and not the alternatives

It is the only option that keeps *every* dictated property while moving the fitting
guarantee from *generation* to *baked data* — the only form that survives artist meshes.
Two documented failures decided it:

- **Second Life** shipped rigged clothing with no fitting mechanism. Clothes ignored the
  avatar's shape sliders, the promised fix never shipped, and the content ecosystem
  fractured into per-brand body standards permanently. *The fitting guarantee is the
  product; it ships with the first wearable or it never ships.*
- **World of Warcraft** made hair one geoset per style, so a helmet can only hide all of it.
  The retrofit was quoted at roughly 4,900 items × races × genders and never happened.
  *Granularity is cheap before content exists and impossible after.*

## Rulings on the open decisions

The contract left five decisions to the PM/owner. Four are engineering consequences and are
ruled here so modeling is not blocked; one is a content-priority question and goes to the
owner.

| # | Decision | Ruling |
|---|---|---|
| **D-1** | Ears: geometry or texture-only | **Geometry, with their own maskable sub-shell** — at LOD0 only; LOD1/2 may merge them into the head. A medieval game will have helmets, and a helmet that cannot hide ears is the WoW lesson repeating on a smaller part. The LOD restriction keeps the crowd budget honest (P1). |
| **D-2** | Fingers: articulate or keep palm+thumb | **Keep palm+thumb.** Articulating is a rig-version event (B-1/B-17) that invalidates every weight and animation, costs joints in a 17-joint palette sized for crowds, and buys nothing the wearables system needs — gloves work on mitten topology. Revisit only if the owner dictates first-person hands or fine manipulation. |
| **D-3** | Elbow/knee cut lines (B-10) | **Accepted.** One duplicated vertex ring per cut, now, versus a catalog in which every tunic must cover the whole arm forever. This is the WoW retrofit lesson applied to limbs, and it is the cheapest insurance in the document. `BodyRegion` extends to match — **superseded by ADR 0010: this belongs to the character asset pipeline session**, since the cut lines are geometry on the body it produces. |
| **D-4** | Which wearables ship first | **Ruled: tunic, trousers, boots, short hair — chosen to stress the mechanism, not to look good.** The owner granted decision authority and, in the same breath, the standard to decide by (P12): the first set exists to prove the *template*, so it is picked for coverage of hard cases, not for a nice screenshot. Together these four exercise every masking region (torso, arms, legs, feet, scalp), both hem classes (a boot cuff meeting a trouser leg, a sleeve ending mid-arm), all three layers, and the one interaction that has sunk other engines — hair under headwear. A cloak or a full plate suit would look more impressive and prove less. |
| **D-5** | Bind pose | **Frozen at the v3 A-pose.** The imported body's authored stance is the bind pose and the rig was fitted to it. Changing it is a rig-version event invalidating every weight, garment binding and animation in existence; v3 has just landed and the owner's verdict on it was favourable. No change without an explicit architect ruling. |

## Two additional rulings from architect review

**The baked asset is the contract, not the script that produced it.** B-13/B-14 freeze
topology and require deterministic export, but the body is produced by a pipeline that
decimates — and decimation is a classic source of run-to-run drift. Rather than depend on
that pipeline being bit-reproducible forever, the **committed `.mgeskin` is canonical**:
bindings are baked against the committed asset and validated against its content hash. The
generator is an input to the contract, not the contract. A regenerated body that differs is
a contract-version event whether or not anyone meant it.

**The contract already has debt against the shipped body.** `docs/BODY_CONTRACT.md` reads as
greenfield ("before modeling starts"), but a body has already shipped, and it does not meet
every MUST:

- **B-8 (minimum region set)** — `BodyRegion::Face` is **empty** in the delivered v3 body:
  the imported head is a single shell, and `tests/test_body_mesh.cpp` explicitly skips Face
  in its region checks. Recorded honestly by the body session, and harmless *until* the
  first mask, visor, or face-covering helm exists — at which point it blocks that item
  outright.
- **B-11 (hem-loop table)**, **B-25 (region vertex groups)**, **B-29 (published authoring
  reference)** — not delivered by v3, because nothing needed them yet. They are what a
  garment artist models against, so they gate the *first authored garment*, not the body.

These are not failures of the body session; they are the gap between a body built to render
and a body built to be dressed. They are logged here so nobody discovers them at the moment
a garment needs them.

## Consequences

- The wearables area can start: the mechanism is settled and the first work is the import
  pipeline, not garment authoring.
- The body session gains a definition of done it did not have, plus three named gaps to
  close before the first authored garment (Face shell, hem loops, vertex groups, authoring
  reference).
- Any change to body topology, vertex order, or UV chart is a **contract-version event**:
  announced, versioned, hash-recorded, and paired with a re-bake of every wearable. The
  pipeline refuses a binding whose body hash does not match, so this fails loudly rather
  than silently.
- Hair ships segmented into sub-regions from the first hairstyle. This is not optional and
  it is not deferrable.

## Amendment — P12: the hundredth wearable is the one that matters

*Owner, Dictation 7: "we would want to make a lot of wearables very easily so we would
rather work hard on a mechanism that works as a template but in the future each wearable
would be easy to make."*

This does not change the mechanism chosen above — it is the same conclusion the research
reached, now stated as law and with the priorities sharpened. What it changes is **what
counts as done**:

1. **The import tool is the deliverable, not the first garment.** Effort goes into weight
   transfer, binding, and layer chaining being *reliable without supervision*. A pipeline
   that needs an engineer to babysit each import has not shipped, however good the first
   garment looks.
2. **The acceptance gates are self-serve.** An artist imports and gets pass/fail with a
   reason — enclosure failed at max bulk, coverage leaves a gap at the boot line, over
   budget at LOD1. Gates that only an engineer can interpret put an engineer in the content
   loop, which P12 forbids.
3. **Garment archetypes, not one-off assets.** Tunic, trousers, boots, hair are *kinds*
   with shared coverage, hem and layer conventions. A new tunic inherits the kind and
   changes the shape.
4. **Zero code per garment, and the build proves it.** The end state: adding a wearable
   touches no `.cpp` and no `CMakeLists`. Anything that does is a defect in the mechanism.

**The same law now governs held items** — a new weapon must not mean new animation work.
Items declare a *use archetype* (`swing`, `thrust`, `chop`, `work`, `draw`, `aim`, `raise`,
`consume`, `gesture`) parameterized by grip, reach and weight; the engine animates the
archetype. Specified in CHARACTERS.md §6.2, and it needs one enabling capability the
animation system does not have yet: **layered poses with masks**, so an upper-body action
plays over locomotion and a character can swing while walking. `ItemUse::animKey` from the
Phase 12 action model is the placeholder this replaces.

## Addendum (architect ruling, post-implementation): outfit chaining is deferred

The pipeline landed (tasks 13.1–13.4; 13.5's offline half). Implementation surfaced a real
design fact the ADR had not stated: **which garments share a slot is a property of an
outfit, not of a garment** — a tunic worn with and without armour cannot carry one binding
baked against a fixed under-layer. Closing 13.5 end-to-end therefore means re-fitting the
stack at *equip time* (inner → offset → outer) on the job lanes and cache.

**Ruling: build it after the body deliverables (13.6–13.9) and the UV repack land — not
now.** Three reasons. The shipped catalogue does not need it (no two morph-following layers
share a region, verified). Deferral accrues no content debt — bindings already carry
`rootBodyHash` and the offline chaining half exists and is tested, so nothing is being
authored against a wrong assumption; this is the safe kind of deferral, unlike the WoW/hair
kind. And a new runtime contract should land against a settled body, not one in the middle
of a contract-version event.

Also ratified from implementation findings: weight diffusion runs over the position-welded
mesh (coincident vertices must share weights or seams tear at the first bend); the ~50%
inpainted fraction on closed garments is *correct by construction* (inner walls face the
body and must fail the orientation gate — removing the gate reintroduces the
ribcage-under-a-sleeve bug); and the content hash deliberately excludes morph targets, so
adding a shape parameter never invalidates the catalogue while moving a vertex always does.
