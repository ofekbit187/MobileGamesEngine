# ADR 0006 — The humanoid variation scope

**Status:** accepted (2026-08-20) · implements CHARACTERS.md §4.1 (body
variants and the face sub-schema) · builds on [ADR 0005](0005-humanoid-template-body.md)

## Context

CHARACTERS.md §4.1 dictates that humanoid variety comes from **variant data
files**, never from new models, and names the mechanism: *"skeleton-proportion
scaling … and morph deltas on the template mesh (chest, face, fine features).
Both representations are compact data (P1)."*

Half of that shipped with ADR 0005: proportions. Height, breadths and limb
ratios move joints, the mesh follows through the skinning palette, and every
garment follows with it. The other half did not exist, and everything scaling
cannot say went with it — a stack of ribs is not a longer bone, and neither is
a heavy brow. The face in particular had **no parameters at all**, so every
character in the engine had the same face.

This ADR fixes the scope: what a humanoid may differ by, by how much, and
which of the two mechanisms carries each parameter.

## Decision

### 1. Two mechanisms, and every parameter belongs to exactly one

| | **Proportions** | **Shape** |
|---|---|---|
| Realized by | skeleton scaling → skinning palette | morph deltas on the shared mesh |
| Units | metres, or a ratio of height | a number in [-1, +1], 0 = template |
| Per character | 17 matrices (1088 B) | 15 floats (60 B) |
| Garments follow | yes, for free | not yet — see *Consequences* |

Nothing produces a per-character mesh. That is the P1 case for the whole
design and it is asserted by `the_variation_scope_costs_almost_nothing_per_character`.

### 2. The scope

**Proportions** — clamped to these, never rejected:

| Parameter | Min | Standard | Max | Meaning |
|---|---|---|---|---|
| `height` | 1.35 | 1.75 | 2.15 | metres, **sole to crown** |
| `shoulderWidth` | 0.28 | 0.37 | 0.52 | metres, shoulder joint to shoulder joint |
| `hipWidth` | 0.15 | 0.20 | 0.30 | metres, femoral head to femoral head |
| `legRatio` | 0.43 | 0.50 | 0.56 | legs as a fraction of height |
| `armRatio` | 0.38 | 0.44 | 0.50 | arm length as a fraction of height |
| `bulk` | 0.70 | 1.00 | 1.60 | limb and torso thickness |
| `headScale` | 0.85 | 1.00 | 1.20 | head size against the body |
| `footScale` | 0.80 | 1.00 | 1.25 | foot size against the body |

**Shape** — 15 parameters, each in [-1, +1]:

*Body:* `chest` (shallow ↔ deep ribcage), `belly` (lean ↔ heavy waist),
`seat` (flat ↔ full hips), `muscle` (soft ↔ defined limbs), `neck`
(slender ↔ thick).

*Face — the sub-schema of §4.1, designed to grow:* `skull` (narrow and long ↔
round and broad), `brow`, `cheeks`, `jawWidth`, `chin`, `noseLength`,
`noseWidth`, `mouth`, `eyes` (spacing), `ears`.

At ±1 a parameter moves the surface between **7 mm and 71 mm**. That range is
deliberate: below a few millimetres a parameter is decoration nobody will
notice; a facial feature that moved a centimetre and a half would leave the
range human faces actually cover, and the engine's style is grounded realism
(MODELING.md §1).

### 3. `height` means sole to crown — for every combination

Legs are placed from `legRatio`, the head from `headScale`, and **the spine
takes up whatever is left**. Without that, a long-legged variant came out
taller than its own `height` field said, which makes the field a lie and no
game can lay out a doorway against it. Asserted at both ends of the scope by
`shape_and_proportions_compose`.

### 4. Content is clamped, never rejected

A variant file is content. `clampToScope` runs inside `buildSkeleton` and
`morphWeights`, so no path can be handed an out-of-scope body: an absurd
number produces the nearest sane body rather than a failed load or a
character the animation cannot cope with.

### 5. Morph targets are authored parametrically, and shipped as glTF

`tools/model/humanoid_morphs.py` defines each target as a **region of the body
measured against landmarks found on the model itself** plus a displacement
over it — never a hand sculpt. Consequences:

- **Deterministic and diffable**, which the repo requires of all content.
- **Smooth by construction**: the region weight is a continuous function of
  position, so a morph cannot tear the mesh.
- **Re-derivable**: if the base mesh is upgraded the targets regenerate; a
  sculpt would have to be redone.
- **Per LOD**: targets are generated after each LOD is decimated, so every
  level gets deltas that match its own vertices. (Blender also refuses to
  decimate a mesh that already carries shape keys.)

They are authored as Blender **shape keys**, which glTF carries natively as
morph targets — no bespoke authoring format, no side-car file. The importer
matches them to the canonical parameters **by name** and drops any it does not
recognize.

### 6. Storage

`.mgeskin` v2 appends a morph block. Each target stores only the vertices it
actually moves: `uint16` index, `int16[3]` position against the target's own
scale, `int8[3]` normal — **12 bytes per moved vertex**. The quantization
scale is computed per target at bake time, so int16 spends its whole range on
that target instead of on a fixed guess.

Shipped LOD0: 3518 deltas across 15 targets = **42 KB, once, process-wide**.

A target stores **one direction**. A weight of −1 applies the delta negated,
so "narrow jaw" costs nothing beyond "square jaw" — and the test asserts the
two are exact mirrors, which is also what stops the file quietly storing both.

## Consequences

- **Garments do not yet follow the shape half.** They ride the palette, so
  proportions carry, but a heavy belly does not push out a tunic: that needs
  the morph deltas of the regions a garment covers baked into the garment
  (CHARACTERS.md §5). It is the wearable system's to do.
- **The morph pass is CPU, at load/spawn time**, exactly like skinning today.
  `skinMesh` applies shape in bind space and then the palette — the order the
  shader must use. GPU morph+skin lands with GPU skinning (task 8.10); until
  then the P1 claim is about storage, not about the frame path.
- **Expressions get this for free.** CHARACTERS.md §8 wants expressions as
  morph presets on the template face, blendable over any face variant — the
  same delta machinery, a different set of targets. Task 8.20 no longer waits
  on infrastructure, only on the expression targets themselves.
- **`Morph` order is a file-format contract.** Appending a parameter is safe;
  reordering silently reassigns every baked delta.
- The face sub-schema stays **strictly above the jaw line** (y ≥ 1.46 m on the
  template) — asserted — so a facial parameter can never quietly change a body
  measurement a garment was fitted to.

## Alternatives considered

- **Facial structure as extra joints** (jaw, brow, cheek bones scaled per
  character). Rejected: it reuses the palette and costs no delta storage, but
  it cannot say "rounder skull", it spends rig slots and vertex influences the
  crowd budget needs, and expressions would still have to be morphs — two
  mechanisms where one does.
- **Per-variant baked head meshes.** Rejected on P1, for the same reason ADR
  0005 rejected per-variant bodies.
- **Hand-sculpted morph targets.** Rejected for now: not diffable, not
  re-derivable when the base mesh changes, and a sculpt cannot be reviewed by
  a test. Parametric targets do not preclude sculpted ones later — the format
  and the runtime do not care where a delta came from.
- **Dense (non-sparse) targets.** Rejected: a facial parameter touches a few
  dozen of 1640 vertices, so 15 dense targets would cost more than the mesh.
