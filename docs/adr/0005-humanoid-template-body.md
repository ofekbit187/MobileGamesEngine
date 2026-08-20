# ADR 0005 — The humanoid template body

**Status:** accepted (2026-08-19) · **revised 2026-08-20**: the body is now an
imported CC0 base mesh, not generated geometry (see *Revision* below) ·
supersedes the v1 parametric part-body for task 8.12 · implements
CHARACTERS.md §4–§6

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

### 1. The body is an imported, anatomically modelled base mesh

The template body is **Blender Studio's Human Base Meshes** bundle (CC0),
object `GEO-body_male_realistic` — 10,582 quads of anatomy, UV-unwrapped,
watertight, in a relaxed A-pose. `tools/model/humanoid_template.py` runs
Blender headlessly (`bpy`) and **does not edit its geometry**. It only:

1. places it — 180° yaw so it faces the engine's −Z, uniform scale to 1.75 m,
   soles on the ground, centred on the mid-line;
2. builds the canonical 17-joint rig **at the mesh's own joints**;
3. skins it (bone heat), clamps to four influences and prunes the leakage;
4. decimates LOD1/LOD2 and cuts the garments out of the body's own surface;
5. exports glTF, which `mge_asset_import --skinned` bakes to `.mgeskin`.

**The rule this replaced a week of failures with: fit the rig to the mesh,
never bend the mesh onto the rig.** Every earlier attempt warped the base mesh
towards an idealised T-pose skeleton and every one of them wrecked it. So the
rig's bind pose *is* the model's authored stance — which is why the arms hang
about 21° out from vertical and the legs splay slightly in towards the ankles,
and why `buildSkeleton()` in `engine/src/character/humanoid.cpp` reproduces
exactly the joint table in the authoring script.

*Why imported rather than generated:* generated ring profiles gave a shape that
read as a mannequin. A base mesh built by character artists carries anatomy no
profile table encodes — clavicle, deltoid insertion, calf asymmetry, the shape
of a hand — and it costs nothing at runtime, because everything downstream
consumes `SkinnedMeshData`, not the generator.

*Licensing:* CC0, public domain, no attribution required (we credit it anyway
in `docs/MODELING.md`). The 48 MB bundle is a **build-time input** and is not
committed; the baked `.mgeskin` assets are, so the engine builds and runs
without it.

### 2. One mesh for every character; variants live in the skinning palette

A character's proportions are **not** a mesh. They are a per-joint scale folded
into the skinning matrices:

```
palette[j] = poseWorld_variant[j] · R[j] · scale[j] · R[j]ᵀ · translate(−bindPos_template[j])
```

`R[j]` is the **bone's own frame** (+Y along the bone). This factor is what
the A-pose bind made necessary: with the scale expressed in character axes, a
bulky character's diagonally hanging arms got *longer* instead of thicker.
In the bone frame, `bulk` always means across the bone and a length ratio
always means along it, whatever stance the model was authored in. The test
`body_variants_come_from_the_palette_not_new_meshes` asserts both halves —
arm thickness scales with `bulk`, arm length does not.

Consequences:

- **Memory:** 60 characters cost **one** 85 KB mesh plus 60 × 1088 B of joint
  matrices, instead of 60 meshes (~5 MB). This is the P1 case for the design.
- **Animation:** every clip retargets to every variant for free — same rig,
  same pose, different palette (CHARACTERS.md §4.2).
- **Wearables:** a garment authored against the template rides the *same*
  palette, so "authored once, fits every variant" holds arithmetically rather
  than by per-body authoring (§5.1).

### 3. Regions partition one shell; masking drops triangle ranges

The base mesh is a single watertight surface, so regions (torso, arm, hand,
leg, foot, scalp, neck) are a **partition of its triangles**, derived from each
vertex's dominant bone and grouped into one `MeshPart` per region at import.
Masking a covered region is an index-range decision on the shared mesh: a
dressed character costs *fewer* triangles than an undressed one and no extra
vertices. Because a garment is cut from the body's own surface along the same
region boundaries, the garment always covers exactly what the mask removes —
no holes, no exposed backfaces, no clipping (§5.1).

### 4. Garments are cut out of the body

Each shipped garment is the body's own surface, restricted to the regions it
covers, pushed out along the normals by its layer's thickness (base 4 mm, mid
9 mm, outer 17 mm) and solidified. It therefore fits the body exactly, encloses
whatever layer sits beneath it, and inherits the body's skin weights — so it
animates and stretches with every variant for free.

Garments may also take geometry *outside* what they mask, purely for looks: the
tunic takes the top of the arms so its boundary falls on the smooth ring of the
upper arm instead of the jagged shoulder seam, and the boots take the ankle.
They must still cover every region the engine masks for them, or the masked
body leaves a hole.

### 5. Budgets

| Level | Triangles | Vertices | Use |
|---|---|---|---|
| LOD0 | 2200 | 1640 | player, close NPCs |
| LOD1 | 1200 | 1021 | crowd |
| LOD2 |  560 |  573 | far crowd, still fully animated |

Garments: tunic 848, armour 868, trousers 776, boots 532, hair 184–444.

Vertex: 36 B — position, normal, UV (uint16), 4 joint indices, 4 uint8 weights
summing to 255. Four influences is the hardware-skinning maximum mobile GPUs
agree on; the shipped body averages **2.2** per vertex.

Published mobile guidance puts hero characters at 5k–20k triangles and warns
that a mid-range Android device wants a hero under ~5k for a steady 60 fps.
This engine is crowd-first and untextured so far, so the template sits well
under that: silhouette spent on shape, nothing spent on detail a normal map
will carry later.

### 6. Skin weights are pruned, not just normalised

Bone heat is a diffusion solve and it leaks: on this body it left the ankle
~10% *thigh* influence, which reads as the foot swimming when the knee bends.
An influence survives only if its bone is within 10 cm of the closest bone
influencing that vertex — near enough to keep every real blend band (elbow,
knee, shoulder), far enough to cut leakage across an intervening joint.

Decimation undoes both cleanups, because collapsing an edge merges the two
vertices' influence sets, so **the clamp and the prune are re-applied after
every reduction** — on each LOD and on each garment. `test_body_mesh.cpp`
asserts the invariant on the shipped asset, measuring distance to the *bone*
rather than to its joint so the rule stays pose-independent.

## Consequences

- The frame path still needs **GPU skinning** (task 8.10): the palette and the
  vertex format are ready, `skinMesh()` is the CPU reference the shader must
  match. Until the skinned pipeline lands, tools skin on the CPU at load time.
- Textures are not modelled yet, but the **UV layout is** — it is the base
  mesh's own unwrap, carried through import.
- `BodyRegion::Face` is **empty**: the imported head is one shell and the
  dominant-bone rule puts all of it in `Scalp`. Nothing masks `Face` today
  (hair masks nothing), so this costs nothing yet; a visor or mask wearable
  will need the head split, which is an authoring change (a second material
  the importer maps to a region), not an engine change.
- Fingers are modelled but **not rigged** — the hand is one joint. Finger
  joints would cost rig slots the crowd budget cannot justify yet, and the
  hand's own geometry carries the silhouette.

## Alternatives considered

- **Generate the body from ring profiles** (the previous revision of this ADR).
  Rejected: it produced a mannequin, not a person. The generator's advantage —
  garments derived from the same profiles — is preserved by cutting garments
  out of the imported body instead.
- **Warp the base mesh onto an idealised T-pose rig.** Rejected after repeated
  attempts corrupted the model. Fitting the rig to the mesh costs nothing and
  cannot damage the art.
- **Keep rigid parts, improve the primitives.** Rejected: joints read as gaps
  no matter how good the primitives are, and rigid parts cannot deform, so
  garments could never be a single skinned surface.
- **Per-variant baked meshes.** Rejected on P1: it multiplies the mesh cost by
  the number of distinct bodies on screen, exactly the thing the engine's first
  principle forbids.
