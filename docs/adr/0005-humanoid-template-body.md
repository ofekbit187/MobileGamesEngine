# ADR 0005 — The humanoid template body

**Status:** accepted (2026-08-19) · supersedes the v1 parametric part-body for
task 8.12 · implements CHARACTERS.md §4–§6

## Context

The engine ships **one** humanoid template body; every humanoid in every game
is an instance of it (CHARACTERS.md §4). The v1 body was an assembly of boxes
and capsules parented rigidly to joints. It satisfied the mechanism (variants,
wearables, masking) but not the look — the owner's verdict on the review board
was *"the human body template model doesn't look good enough"*.

Three constraints shape the replacement, and they pull against each other:

1. **It has to look like a body.** Silhouette is all a mobile character has:
   at phone scale, with no textures yet, the shape carries everything.
2. **Many at once.** Characters are crowd content, so the per-character cost —
   memory first (P1), then draw cost — decides how many can share a scene.
3. **It has to stay parametric.** Variants, wearable fitting and masking are
   dictated behaviour (CHARACTERS.md §4.1, §5). Whatever the body is, one
   authored garment must fit every body built from it.

## Decision

### 1. A ring-profile skinned mesh, generated from the canonical rig

The body is a skinned triangle mesh built from **cross-section rings** placed
along each bone: pelvis, waist, ribcage, chest, trapezius, neck, skull; deltoid,
bicep, elbow, forearm, wrist, hand; glute, thigh, knee, calf, ankle, foot. Each
ring carries a radius pair, a superellipse exponent (round limbs, flatter
chest, flat sole), and its skin binding. Neighbouring rings are stitched into
quads; every shell is closed.

Ring radii follow human measurements at the reference height (1.75 m, 7.4 heads,
0.42 m biacromial, ~0.36 m hips), and the profile obeys the deformation rules
character artists use: three loops across every bending joint, a 50/50-weighted
loop exactly at the joint, a domed shoulder that stays inside the torso.

*Why generated rather than imported:* the profiles are the same source for the
body, for all three LODs, and for every garment, so a garment cannot drift out
of alignment with the body, and a body cannot drift out of alignment with the
rig. The glTF import path (P5, task 2.5) stays open: an artist-made body
replaces the generator behind the same interfaces, because everything
downstream consumes `SkinnedMeshData`, not the generator.

### 2. One mesh for every character; variants live in the skinning palette

A character's proportions are **not** a mesh. They are a per-joint scale folded
into the skinning matrices:

```
palette[j] = poseWorld_variant[j] · scale[j] · translate(−bindPos_template[j])
```

`scale[j]` stretches the bone's length (height, leg ratio, arm ratio, head size)
and its cross-section (bulk, shoulder width, hip width) in joint-local space, so
it never leaks into child bones. Consequences:

- **Memory:** 60 characters cost **one** 57 KB mesh plus 60 × 1088 B of joint
  matrices, instead of 60 meshes (~3.4 MB). This is the P1 case for the design.
- **Animation:** every clip retargets to every variant for free — same rig, same
  pose, different palette (CHARACTERS.md §4.2).
- **Wearables:** a garment authored against the template rides the *same*
  palette, so "authored once, fits every variant" holds arithmetically rather
  than by per-body authoring (§5.1).

### 3. Body parts are closed shells, and masking drops whole shells

Each region (torso, arm, hand, leg, foot, head, neck) is its own watertight
shell; shells interpenetrate where the seam is hidden inside the body. A
covered region is simply not drawn — no holes, no exposed backfaces, no
clipping (§5.1). Masking is an index-range decision on the shared mesh, so a
dressed character costs *fewer* triangles than an undressed one and no extra
vertices.

### 4. Budgets

| Level | Triangles | Use |
|---|---|---|
| LOD0 | 2004 | player, close NPCs |
| LOD1 | 1164 | crowd |
| LOD2 | 572 | far crowd, still fully animated |

Vertex: 36 B — position, normal, UV (uint16), 4 joint indices, 4 uint8 weights.
Four influences is the hardware-skinning maximum mobile GPUs agree on; the
template uses at most two per vertex, which also keeps linear-blend-skinning's
twist artifacts ("candy wrapper") out of the shipped body.

Published mobile guidance puts hero characters at 5k–20k triangles and warns
that a mid-range Android device wants a hero under ~5k for a steady 60 fps.
This engine is crowd-first and untextured so far, so the template sits well
under that: silhouette spent on shape, nothing spent on detail a normal map
will carry later.

## Consequences

- The frame path still needs **GPU skinning** (task 8.10): the palette and the
  vertex format are ready, `skinMesh()` is the CPU reference the shader must
  match. Until the skinned pipeline lands, tools skin on the CPU at load time.
- Textures are not modelled yet, but the **UV layout is** — every region has a
  fixed island in the chart, so skin textures authored against the template are
  valid on every variant and every LOD.
- Faces are shaped (jaw, brow, nose) but have no eye/mouth geometry; the face
  sub-schema and expressions (CHARACTERS.md §4.1, §8) will arrive as morph
  deltas on this mesh.
- Hands are a palm with a thumb — enough for grip points and silhouette, not
  articulated fingers. Finger joints would cost rig slots the crowd budget
  cannot justify yet.

## Alternatives considered

- **Import a ready-made base mesh.** Rejected for now: no licensed asset in the
  pipeline, and an imported body would need per-variant fitting data that the
  generated profiles give us for free. The import path remains the upgrade
  route, not a dead end.
- **Keep rigid parts, improve the primitives.** Rejected: joints read as gaps
  no matter how good the primitives are, and rigid parts cannot deform, so
  garments could never be a single skinned surface.
- **Per-variant baked meshes.** Rejected on P1: it multiplies the mesh cost by
  the number of distinct bodies on screen, exactly the thing the engine's first
  principle forbids.
