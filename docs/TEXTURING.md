# Texturing guide — the engine's texture standard

The textures artist's document: what a texture in this engine *is*, what it may
cost, how it is authored, and what must be true before one is called done.
Governed by the [principles](PRINCIPLES.md) — **P1 (memory efficiency) decides
every tie** — and paired with [MODELING.md](MODELING.md), which owns geometry
and the UV chart that every skin texture obeys.

**Nothing in this engine is textured today.** The 3D shaders take a base-colour
factor and no sampler (`engine/shaders/lit.frag`); the static vertex is 24 B of
position and normal with no UV (`mesh_data.h`); the GPU resource manager uploads
meshes only (task 2.3); `.mgemesh` v1 carries no material reference
([ADR 0002](adr/0002-runtime-mesh-format.md)); per-mip streaming is world-format
v2 ([ADR 0003](adr/0003-world-streaming-format.md)). The only sampler bound
anywhere is the UI's R8 font atlas.

So this document is written **before the pipeline exists, on purpose**. A
texture standard set after the first hundred textures are authored is a rewrite
of the first hundred textures. Part I is the standard; Part II is the study
behind it — tools, technique, and what shipping engines did.

Every proposal here is **○ awaiting the owner's verdict**. Two things are
already **● real output**: the standard exists as a machine-readable file the
pipeline enforces (§4.1), and §6 measures the delivered body's UV chart with it
— finding that **the body currently has no usable chart at all**, which blocks
every texture until it is repaired
([`research/uv-audit.md`](research/uv-audit.md)).

---

# Part I — The standard

## 1. The role

The textures artist owns every authored pixel the engine samples:

- **Skin sheets** for the humanoid template body, against the fixed UV chart.
- **Wearable and held-item textures**, authored against the template like the
  geometry is (CHARACTERS.md §5.1).
- **The world material library** — tiling surfaces and trim sheets for
  buildings, ground, and props. Not per-asset unique textures (§5).
- **The Codex UI sheet** — parchment grounds, ink rules, wax, ornament (P6).
- **The rules above**: chart, channel layout, colour space, compression,
  budgets, and the tests that enforce them.

Not owned: geometry, UV layout (MODELING.md), lighting and the material *model*
(graphics), and the shader code that consumes these maps — though the standard
below is a joint contract with all three, and none of it can be changed on one
side alone.

Working method, same as the rest of the project: docs first, owner's verdict on
the review board, evidence labelled **● real output** vs **○ design proposal**,
never a mockup presented as engine output.

## 2. Style — what a texture must carry here

MODELING.md §1 sets the style: **grounded realism at low resolution**, medieval
book-and-paper identity, "no feature the budget cannot finish." Textures inherit
it, with three consequences that decide almost every authoring choice:

1. **Texture carries material, geometry carries silhouette.** The body has no
   eyes, no mouth, no fingers, and no belt — the mesh is 2 004 triangles at
   LOD0. Everything that reads as *detail* on a character is the texture's job:
   the face, the hands' fingers, the seam of a garment, the grain of leather.
   This is the opposite of a high-poly pipeline, and it is the correct trade at
   phone scale (a 512² map costs less than the triangles it replaces).
2. **Material identity beats micro-detail.** At 400 px of screen height a
   villager occupies, oak-vs-pine pore structure is invisible; *wood vs cloth vs
   plate* must be unmistakable. Author the large value shapes and the mid
   frequencies; spend nothing on grain that dies at mip 2. This is the same
   lesson Genshin Impact draws from painterly rather than photoreal texturing —
   less visual noise per texel, and the budget survives.
3. **Handmade, not manufactured.** Cloth is woven and worn, leather is creased,
   plate is hammered and scratched, parchment is fibrous and stained. No
   machine-perfect edges, no glossy chrome, no sci-fi panelling.

Colour discipline: skin, cloth, and stone live in a narrow, slightly desaturated
range with value doing the work. The medieval palette is earth pigments —
ochre, umber, madder, woad, bone — not saturated primaries.

## 3. The map set *[proposal]*

One material, at most **two texture fetches**. Mobile tile-based GPUs are
filtering-bound long before they are bandwidth-bound, and every extra sampler is
a permanent per-pixel tax.

| Map | Channels | Space | Carries |
|---|---|---|---|
| `albedo` | RGB (+A) | **sRGB** | base colour; A = alpha-test cutout when needed |
| `packed` | R,G,B | **linear** | R = ambient occlusion · G = roughness · B = "metal-ish"/specular mask |
| `normal` | XY | linear | optional, opt-in per material (§4) |

Rules:

- **A material with no `packed` map is normal, not lazy.** Roughness and AO fall
  back to per-material constants. Most props never need the second fetch.
- **Channel order is fixed engine-wide (R=AO, G=roughness, B=mask)** — the
  Unreal ORM convention, chosen because it is the one most external artists and
  tools already export. Never per-asset.
- **B is the throwaway channel.** Block compressors give blue the least
  precision, so the least precision-sensitive signal (a near-binary mask) goes
  there — the standard reason ORM is ordered this way.
- **Roughness, not smoothness.** One convention, no per-import inversion.
- **Normal maps are opt-in.** The engine is flat-shaded with a single
  directional light and hemispheric ambient; a normal map on a 300-triangle
  crate buys almost nothing at phone scale. Reserve them for surfaces that
  *tile large and are seen close*: ground, plaster, plank walls, plate armour.
- **No baked lighting in albedo.** AO belongs in `packed.R` where the shader can
  weight it against the light; painted-in shadow is permanent and wrong the
  moment the sun moves.

## 4. Formats and compression *[proposal — hardens into an ADR when the pipeline lands]*

**Every shipped texture is block-compressed, mip-mapped, and never decoded to
RGBA at runtime.** An uncompressed 1024² RGBA texture is 4 MiB resident and 5.3
with mips; the same texture in ASTC 6×6 is 456 KiB, 606 KiB with mips. At P1's
ranking that is not an optimisation, it is the entry ticket.

**Runtime container: `.mgetex`**, the house-style sibling of `.mgemesh` — index
table plus byte-range payloads, `mmap`-able, each mip level its own range so the
streaming system can pull mip 4 without touching mip 0 (exactly the per-LOD byte
ranges ADR 0003 defers to format v2). **KTX2 is the interchange format** on the
way in, the way glTF is for meshes: import reads KTX2/PNG, bake writes `.mgetex`.

**GPU formats: two packs.**

| Pack | Format family | Play device coverage | Role |
|---|---|---|---|
| ASTC LDR | `textureCompressionASTC_LDR` | **> 80 %** of Play devices | primary |
| ETC2/EAC | `textureCompressionETC2` (core in GLES 3.0) | **> 95 %** | fallback default |

Both are Vulkan feature bits we can query at device creation; Play's App Bundle
texture targeting (`#tcf_astc` / `#tcf_etc2`) ships the right pack and nothing
else, so the device downloads one. Google's own guidance is ETC2 as the safe
default and ASTC as the size/quality win — which is exactly a two-pack bake.

**Block sizes** (ASTC's whole point is that the rate is a per-texture choice):

| Content | ASTC block | bpp | ETC2 equivalent |
|---|---|---|---|
| Albedo, world surfaces & skin | 6×6 | 3.56 | ETC2 RGB (4.0) |
| Albedo, near-field (face, held items, UI) | 4×4 | 8.00 | ETC2 RGBA (8.0) |
| Packed AO/rough/mask | 6×6 | 3.56 | ETC2 RGB |
| Normal (XY) | 5×5 with the **XXXY swizzle** | 5.12 | **EAC_RG11** (8.0) |
| Large distant surfaces (terrain far tiles) | 8×8 | 2.00 | ETC2 RGB |

Normal maps are the one place the format choice is subtle. ASTC has no
two-channel mode; the encoder's own recommendation is to store X in the
luminance channels and Y in alpha (`XXXY`, `astcenc -normal`) so the L+A
endpoint mode is used, and reconstruct `Z = sqrt(1 − X² − Y²)` in the shader.
Current published comparisons put `EAC_RG` ahead of a naively-encoded ASTC
normal map and note that most engines *cannot express* the swizzled ASTC
convention to their samplers — a caveat that does not apply to us: we own the
baker, the container, and the shader, so the swizzle is ours to declare. Bake
ASTC-XXXY in the ASTC pack, EAC_RG in the ETC2 pack, and let one shader define
handle both.

**Colour space is a property of the texture, not of the shader.** Albedo bakes
to an sRGB-typed format so the hardware linearises for free; every data map
bakes linear. A roughness map sampled through sRGB is a bug that looks like an
art problem, and it is the single most common texture defect in shipping games.

### 4.1 The standard is a file, not this document

Everything above is prose, and **prose cannot refuse a bad texture**. The
operative form of this standard is data:

**`assets/standards/skin_texture.mgestd`** — sheet tiers, density classes and
tolerances, per-map colour space and channel order, chart rules, mip and padding
rules, compression per map class, and an explicit `refuse` list. Line-oriented,
commented, diffable, parsed in forty lines of C++ with no dependency. Where this
document and that file disagree, **the file wins** — it is what the pipeline
actually reads.

This is P12 applied to textures. The alternative — rules living in a document
and enforced by whoever reviews the work — makes the hundredth texture exactly
as expensive as the first, and it makes every tolerance change a negotiation
instead of a one-line edit. Concretely:

- `mge_uv_report` reads it today and gates the **chart** (§6).
- The baker reads it when it lands and gates the **images**: colour space, mip
  chain, island padding at the smallest streamed mip, compression PSNR,
  deterministic rebuild. Until then the report says those are *not checkable
  yet* rather than passing them by default.
- Adding a body region, retiering a sheet, or tightening the stretch limit is a
  line in that file — **no engineer in the content loop**.

Two rules in it are worth stating in prose too, because they are the ones a
pipeline usually gets wrong by omission:

- **The chart rules run before the density rules**, and the density verdict is
  withheld unless ≥ 90 % of the body is measurable. A green light computed from
  the 4 % that survived a defect is worse than a red one.
- **Every refusal carries its reason.** `FAIL chart_islands_disjoint` teaches an
  artist nothing; *"Scalp/Torso share texels — neither can be painted
  differently from the other"* is actionable without an engineer translating.

## 5. Budgets

Per-texture budgets, at the resolution that ships (LOD0 mip 0):

| Content | Albedo | Packed | Resident cost, ASTC + mips |
|---|---|---|---|
| Skin sheet (whole body, all 12 regions) | 1024² | — | 0.59 MiB |
| Skin sheet, crowd tier | 512² | — | 0.15 MiB |
| Wearable / garment | 512² | 512² (optional) | 0.15–0.30 MiB |
| Held item (weapon, tool) | 256² | — | 0.04 MiB |
| World material (tiling / trim sheet) | 512² | 512² | 0.30 MiB |
| Terrain layer | 512² | 512² | 0.30 MiB |
| Codex UI sheet | 1024² | — | 0.59 MiB |

And the rules that make those numbers hold:

- **Props and buildings get no unique textures.** They are textured from the
  shared library of tiling materials and trim sheets. This is not a compromise:
  it is how modular environment art is done, and published breakdowns of modular
  medieval kits put ~80 % of a scene's surface on trim sheets and tileables.
  For a P10 world where *every* building opens, per-building texture sets are
  arithmetically impossible.
- **One shared skin sheet serves a crowd.** Sixty villagers cost one 1024²
  sheet, not sixty — the same law the template body follows for geometry
  (ADR 0007). Variety comes from the variant's skin *tone* and a handful of
  sheets, not from per-person maps.
- **Texture memory is its own registered budget** in `BudgetRegistry`, refused
  at cap like every other one. *[proposal]* **64 MiB resident on the mid tier**,
  split into a small always-resident core (UI sheet + the material library's
  base mips + the skin sheets in use, ~12 MiB) and a streamed remainder. The
  number is the owner's to set per device tier; the mechanism is not optional.
- **The scale test is the gate.** `tools/stream_test` traverses 1.77 GiB of
  world with geometry flat at 6.9 MiB of a 64 MiB cap. When textures exist, the
  same run must stay flat under the texture cap — a rising line over a 1.9 km
  traversal is a leak, not a warm-up.

### 5.1 Texel density

The engine standard *[proposal]*:

| Class | Density | Example |
|---|---|---|
| Near-field | **1024 px/m** | face, held items, UI at 1× |
| **Standard** | **512 px/m** | skin, garments, walls, props, ground |
| Large / distant | **256 px/m** | building shells, terrain far layers, cliffs |

512 px/m is the third-person industry standard (it is what The Last of Us Part
II uses; hero-asset workflows go to 1024 px/m and first-person to 10.24 px/cm).
It is the right anchor here because our camera is third-person on a phone: a
character 400 px tall on screen cannot show more than that, and P1 spends the
difference elsewhere.

The rule that matters more than the number: **consistency**. A wall must not be
four times sharper than the floor beside it, and a hand must not be sharper than
a torso. Which brings us to the first real finding of this study.

## 6. The template UV chart — measured, and refused

The chart is the base mesh's own unwrap, carried through import and fixed from
then on (MODELING.md §5). Before painting a texel on it, measure it.
`tools/uv_report` (`mge_uv_report`, in `ctest`) walks the delivered body, sums
every triangle's area on the body and on the sheet, and returns **pass/fail per
rule with a reason**, checked against `assets/standards/skin_texture.mgestd`
(§4.1). `--gate` makes it exit non-zero, which is how the body session runs it
as an acceptance gate.

**● Real output, this environment, against the v3 imported body at LOD0:**

```
whole body: 2200 triangles, 1973 with no UV area (89.7%), 1617 edge-locked
            1494 of 1640 vertices sit exactly on a tile edge (91.1%)

VERDICTS (against assets/standards/skin_texture.mgestd)
  FAIL  chart_in_unit_tile         1617 triangles (73.5%) have every vertex pinned
                                   to a tile edge — islands that lay outside 0..1
                                   and were clamped flat on import
  FAIL  chart_max_degenerate_frac  1973 of 2200 triangles have zero UV area
  FAIL  chart_regions_required     regions with no geometry: Face
  FAIL  chart_islands_disjoint     regions sharing texels: Scalp/Neck, Scalp/Torso,
                                   Neck/Torso
  FAIL  density_outside_tolerance  not conclusive: only 4.5% of the body's surface
                                   has usable UVs (need 90%)

chart status: REFUSED
```

**The engine's body currently has no usable UV chart.** Both arms, both hands,
both legs and both feet have *zero* texture space: every triangle in them maps
to a line. The cause is not the modelling — a real unwrap exists in the source —
but the seam between it and the engine: the source unwrap extends past `u = 1`,
the `SkinVertex` UV encoding is normalized `uint16` and cannot represent that
(BODY_CONTRACT.md B-3), and the importer **clamps instead of refusing**. That
silent clamp turned an export setting into committed damaged content across
three LODs.

Full measurements, root cause, the fix, whether it costs UV coordinates or
topology, and the seam request that stops it recurring:
**[`research/uv-audit.md`](research/uv-audit.md)** — filed as a handoff to the
body-modeling session, which owns the chart.

Two consequences that belong to this document rather than that one:

- **No skin texture may be authored until the chart conforms.** Not "should
  not" — the pipeline refuses it. Painting against a chart that is about to be
  repacked wastes the work twice: once when it is painted, once when it is
  invalidated (B-27 makes a UV change a contract-version event).
- **The face needs more than an even share.** MODELING.md §1 gives the face no
  eye, brow or mouth geometry *on purpose* — the texture carries all of it. A
  region with no island cannot carry anything, so when the head is split, `Face`
  is the one place the `near_field` density class (1024 px/m) is earned.

*An earlier revision of this section reported an uneven-but-usable chart (589
px/m mean, 3.6× spread). Those numbers were measured against the previous
code-generated body and are superseded; the audit records what changed.*

## 7. Mips, filtering, and streaming

- **Every texture ships a full mip chain, down to 1×1.** No exceptions, not even
  UI. Mips cost 33 % more memory and save far more bandwidth than that, and a
  minified un-mipped texture aliases into shimmer that no art fixes.
- **Mips are generated in linear space**, then re-encoded — box-filtering sRGB
  values darkens every level and is the second most common texture defect.
- **Normal-map mips are renormalised** per level; roughness may be adjusted
  toward the variance the lost normals carried (a proper spec-AA pass is later
  work, but the hook belongs in the baker from day one).
- **Alpha-tested mips preserve coverage.** Naïve downsampling makes cutout
  foliage fade away with distance; the fix is the standard one — rescale each
  level's alpha so the fraction of texels passing the alpha test matches mip 0.
  Anything with leaves, rope, or chain-mail needs it.
- **Filtering: bilinear + mips is the default.** Trilinear costs 2× on Mali-class
  hardware; 2× anisotropic bilinear is both cheaper and better than isotropic
  trilinear where it matters (ground at grazing angles). Use aniso surgically,
  never globally. Target ≤ 1 GB/s average texture read bandwidth.
- **Streaming is by mip, not by texture.** A distant chunk holds mip 4 and up;
  approach pulls the finer levels through the same priority rings the residency
  manager already runs. This is the texture half of task 4.4, and the reason
  `.mgetex` stores mips as separate byte ranges.
- **No runtime mip generation.** Mips are content: baked, deterministic,
  diffable.

## 8. Textures as generators, not as pixels *[proposal]*

The template body is not a file — it is code that generates a mesh
deterministically, gated by tests (ADR 0007). **Textures should follow the same
law**, and for this engine specifically the argument is strong:

- A 1024² source PNG is ~2–4 MB in git, per texture, forever, undiffable. The
  generator that produces it is ~100 lines, reviewable, and diffs like code.
- P1 is a *runtime* principle, but repository weight is the thing that makes a
  content pipeline unworkable. "Giant games, minimal runtime burden" wants a
  content pipeline that expands, not one that stores.
- Determinism is already the house rule (MODELING.md §3.7: same inputs, same
  vertices, byte-identical builds). Procedural textures satisfy it natively;
  hand-painted PNGs satisfy it trivially. Both work — but only one of them can
  be *parameterised*, and parameterisation is how one plaster generator becomes
  the eleven plasters a town needs.
- It matches how the highest-end texture work is actually done: Substance
  Designer graphs are generators, and studios ship the graph, not just its
  output.

So: **the default authoring form for a world material is a generator with named
parameters; the default form for a unique hero surface is a painted source
image; the bake step is the same for both.** Both feed one baker that does
mips → colour space → block compression → `.mgetex`.

And the P5 corollary: **a texture can be virtual too.** A material may declare
"oak plank, weathered, 512 px/m" with no pixels, render as a legible
placeholder, and be fulfilled later by an external agent under the same ID —
the exact mechanism `tools/fulfill_demo` already proves for meshes. The manifest
export grows a texture section; nothing else changes.

## 9. Definition of done

A texture is done when all of these hold, and each is a **test**, not an opinion
— the same bar `tests/test_body_mesh.cpp` sets for geometry:

- [ ] **Chart-conformant** — every texel inside a declared island; islands
      padded/dilated ≥ 2 texels at the shipping resolution *and still ≥ 2 texels
      of usable padding at the smallest streamed mip*, so filtering never bleeds
      one region into another.
- [ ] **Power-of-two, full mip chain**, generated in linear space, coverage
      preserved for alpha-tested maps, normals renormalised per level.
- [ ] **Correct colour space** — albedo sRGB-typed, every data map linear.
      Asserted at bake, not trusted.
- [ ] **Inside budget** at its class resolution, with the block size the class
      declares.
- [ ] **Compression-verified** — the baker decompresses its own output and
      reports PSNR against the source; below the class threshold fails the bake
      rather than shipping a smeared normal map.
- [ ] **Seamless where it tiles** — edge-continuity test on wrap, plus a 3×3
      preview: no visible repeat cadence, no bright/dark corner.
- [ ] **Density-consistent** — measured texels/metre within ±15 % of the class
      standard (§5.1).
- [ ] **Deterministic** — two builds byte-identical.
- [ ] **Looked at** — rendered on the actual model, at gameplay distance *and*
      close up, in the engine, not in a viewer. Every defect in the template
      body was found by looking at a render; textures will be no different.
      `tools/body_preview` is the pattern to copy.

---

# Part II — The study

## 10. Tools

### 10.1 What this repository can run today

The environment has no DCC application, no image toolchain, and no texture
compressor installed: no Blender, no ImageMagick, no `astcenc`, no `toktx`, no
Python imaging stack. `third_party/` holds `stb_truetype.h`, `cgltf`, and a
font. Everything visual this project has produced — the body sheets, the LOD
comparisons, the walk cycle — is C++ writing PPM files and a Vulkan renderer on
llvmpipe.

That is not a limitation to work around; it is the pipeline. It is headless, it
runs in CI, it is deterministic, and it is the reason every geometry claim in
this repo is backed by a real capture. The texture pipeline should be built the
same way:

| Piece | How | Status |
|---|---|---|
| Chart measurement | `tools/uv_report` (`mge_uv_report`) | **● built, in `ctest`** |
| Texture generation | C++ in `engine/` or a `tools/` baker | to build |
| PNG/preview output | vendor `stb_image_write.h` next to `stb_truetype.h` | to vendor |
| Source image reading | vendor `stb_image.h` (KTX2/PNG in) | to vendor |
| ASTC/ETC2 encoding | vendor **`astcenc`** (Arm, Apache-2.0) as a build target | to vendor |
| Compression QA | decode with the same library, compute PSNR | to build |
| Looking at it | `tools/texture_preview` mirroring `tools/body_preview` | to build |

`astcenc` is the reference ASTC compressor, has the quality presets that matter
(`-fast` for iteration, `-thorough` for ship), the `-normal` mode that applies
the XXXY swizzle correctly, and a permissive licence. `libktx`'s `toktx` wraps
the same encoder for KTX2 output if interchange files are ever needed.

### 10.2 What a human texture artist uses — and how to use it well

When the owner brings a human artist (or an external agent) into the pipeline,
these are the tools they will be holding. The engine must accept their output,
so the standard above is written in their vocabulary.

**Substance 3D Designer** — the industry standard for *procedural* texturing and
the closest commercial analogue of §8. Node graphs produce tileable materials
parameterised by exposed sliders; one graph yields a family of plasters. Pro
practice worth stealing wholesale:

- Build from **noise → shape → height**, and derive normal, AO, and curvature
  *from the height field* rather than authoring them independently — the maps
  then agree with each other by construction.
- Expose a small number of meaningful parameters (age, wetness, mortar depth),
  not fifty internal knobs. The exposed set *is* the material's API.
- Work at 2× the ship resolution and downsample; artefacts that survive
  downsampling are real, artefacts that vanish were never worth authoring.
- It is automatable: the Substance Automation Toolkit's `sbscooker` (`.sbs` →
  `.sbsar`) and `sbsrender` (`.sbsar` → maps) turn a graph into a build step,
  which is how a graph-based workflow stays deterministic and CI-checkable.

**Substance 3D Painter** — the standard for *per-asset* texturing: bake mesh
maps (AO, curvature, thickness, position, normal-from-high-poly), then paint
with smart materials and generators that read those bakes, so wear appears on
edges and dirt collects in cavities automatically. Export presets map channels
to a target layout — ours is the ORM order in §3. The discipline that separates
professionals: **paint with masks driven by baked data, not by hand-placed
brush strokes**, so the same material re-applies to the next asset.

**Blender** — free, scriptable, headless (`blender -b -P bake.py`), and entirely
capable of the baking half: AO, curvature, cavity, normal-from-high-poly, and
UV work. For this project it is the natural fallback if an external agent needs
a GUI, and the only one of these that could run in CI.

**Krita / GIMP** — hand-painting, when a surface is genuinely unique (a
manuscript page, a painted shield). Free, and for a book-and-paper identity,
hand-painted work is often *more* appropriate than procedural.

**Materialize** (free, photo → PBR maps) and **ArmorPaint** (open-source
Painter-alike) are the credible no-licence alternatives worth naming for an
external agent without an Adobe subscription.

**Compression and QA**: `astcenc`, `toktx`/`libktx`, AMD **Compressonator**
(GUI + CLI, side-by-side compression comparison with error metrics), and
NVIDIA Texture Tools / **DirectXTex** as the reference implementations to read
for alpha-coverage mips and gamma-correct downsampling.

### 10.3 Habits that separate a professional from a hobbyist

Distilled from the sources in §12, and worth writing down because most texture
defects are process defects:

1. **Never author in the shipping format.** Author high, bake down; every bake
   is reproducible from the source.
2. **Check texel density with a checker map before painting anything.** Stretch
   and inconsistency are chart problems; they are unfixable later and invisible
   until they are embarrassing (see §6).
3. **Judge at distance, not at 100 % zoom.** The asset is seen at 3 m on a
   6-inch screen. Evaluate it there, in the engine, under the engine's light.
4. **Judge tiling as 3×3, always.** Every tileable texture has a repeat cadence;
   the only question is whether you found it before the player did.
5. **Judge compressed, not source.** Normal maps and smooth gradients are where
   block compression hurts; a source-only review ships the artefact.
6. **Keep the value range honest.** Real albedo lives between roughly 0.03 and
   0.9 reflectance; pure black and pure white in albedo are lighting bugs
   waiting to happen.
7. **Pad and dilate every island** — and check it at the smallest mip that ships,
   not at mip 0, which is where padding silently disappears.
8. **One naming convention, mechanically enforced**: `<asset>_albedo`,
   `<asset>_packed`, `<asset>_normal`. The baker should refuse anything else.

## 11. Technique notes for a code-first texture pipeline

Since §8 makes generators the default form, the craft here is procedural. The
toolbox that covers nearly everything this engine needs:

- **Value/gradient noise + fBm** — the base of plaster, dirt, cloud, rust.
  Octave count is a cost knob; 4–5 is plenty at 512².
- **Domain warping** (feeding noise through noise) — the single cheapest way to
  turn "computer noise" into "natural material". Wood grain, marble veining,
  parchment fibre.
- **Worley / cellular noise** — stone, cobble, cracked mud, leather pores.
  Distance-to-second-nearest gives the mortar lines for free.
- **Directional fibre noise** — parchment and cloth: anisotropic noise along one
  axis, plus a second pass at 90°, gives a woven ground that reads as paper or
  linen depending on the frequency ratio. This is the Codex UI's base material.
- **Height first, everything else derived** — normal from the height gradient,
  AO from a blurred height inversion, curvature from the second derivative.
  Three maps, one source, guaranteed consistent.
- **Bake from our own mesh.** We have the template body as generated C++ data.
  Per-vertex AO and curvature can be computed directly from it and rasterised
  into the chart — no high-poly sculpt, no external baker, and the result is
  exactly aligned to the geometry that ships.
- **Stains and history are what sell "handmade"**: a clean procedural material
  reads as plastic. Edge wear driven by curvature, grime driven by inverted AO,
  and one large-scale low-frequency value break to kill uniformity.
- **Tileability is a constraint on the generator, not a fix-up.** Use periodic
  noise (hash on wrapped coordinates); never mirror-blend a seam.

## 12. Prior art — what shipping engines and games do

| Source | What they do | What we take |
|---|---|---|
| **id Tech 5 / RAGE** — MegaTexture | Virtual texturing: a unique 128 K² texture streamed as 128² tiles, decoupling logical texture space from VRAM | The idea of mip/tile-granular residency. **Not** full VT: a feedback-buffer pass fights tile-based mobile GPUs, and our chunk streaming already gives us the residency spine |
| **Unreal Engine** — SVT / RVT | Streaming Virtual Texturing for very large texture sets; per-platform texture format targeting; mobile guidance to cut samplers | Mip-granular streaming, and the ORM channel convention external artists already export |
| **Unity** — Android defaults | ASTC recommended default on Android, ETC2 fallback, per-platform overrides | The two-pack strategy, validated by a second engine |
| **Google Play** — texture targeting | App Bundle serves per-device texture packs; ASTC > 80 %, ETC2 > 95 % coverage | Exactly our ship plan: bake both, ship one |
| **Arm / Mali best practices** | Compress everything, mip everything, bilinear default, 2× aniso over trilinear, ≤ 1 GB/s average texture read | §7 verbatim — this is the hardware we ship on |
| **Genshin Impact** | Open world on phones; painterly non-photoreal texturing to cut visual noise; art budgets set by engineering, not negotiated after | The strongest peer. Art direction chosen so the budget *can* win — the same reason MODELING.md picked coarse realism |
| **The Last of Us Part II** | 512 texels/m as a project-wide standard | Our standard density (§5.1) |
| **Modular environment / trim-sheet practice** | ~80 % of a modular scene textured from trim sheets and tileables; snap unit chosen so UVs align | §5's rule that props and buildings get no unique textures — mandatory for P10 |
| **The Witness** (Castaño) | Alpha-coverage-preserving mipmaps; gamma-correct downsampling | §7's mip rules |
| **Bethesda open worlds** | Largely unique, high-resolution per-asset textures | The counter-example. It is why their worlds need desktop VRAM, and it is precisely what P1 forbids |
| **Khronos KTX2 / Basis Universal** | One supercompressed file transcoded at load to whatever the device supports (UASTC for quality, ETC1S for size) | KTX2 as *interchange*. We transcode at **bake** time instead of load time — a phone should not spend CPU and battery transcoding what a build machine could have decided |

The through-line: **every engine that survives on mobile decides texture format
and residency at build time and keeps the runtime dumb.** The ones that push the
decision to runtime pay for it in battery.

## 13. What the engine must land before a texture can ship

Honest sequencing against [TASKS.md](TASKS.md). Nothing in Part I is buildable
until these exist, and they are graphics-side work, not texture-artist work:

1. **UVs on the static vertex.** `Vertex` is 24 B of position + normal today;
   textured statics need a UV (the skinned body vertex already has one). This is
   the `.mgemesh` v2 quantisation pass ADR 0002 already schedules "with texture
   streaming".
2. **Sampled images in the renderer** — descriptor sets, samplers, and a texture
   binding in the lit pipeline. The UI pipeline is the only one with a sampler
   bound (an R8 512² font atlas); the 3D path has none.
3. **Budgeted texture upload/evict** in the GPU resource manager — the half of
   task 2.3 that is explicitly still pending.
4. **Material references in the runtime format** (ADR 0002) and **per-mip byte
   ranges** in the world format (ADR 0003 v2) — the two format v2 items that
   texture streaming has been waiting on all along.
5. **glTF material/texture import** (task 2.5) and the `.mgetex` baker.

**The shortest path to the engine's first authored texture is the UI.** The
overlay pipeline already binds a sampler and already has an atlas; giving the
Codex a real parchment ground and ink rules needs the atlas to carry colour, not
a new pipeline, and it lands the whole bake → mips → compress → `.mgetex` chain
on the smallest possible surface. That is the recommended first milestone; the
skin sheet follows once the chart defect in §6 is fixed.

---

## Sources

Formats and hardware:
[Google Play texture compression targeting](https://developer.android.com/guide/playcore/asset-delivery/texture-compression) ·
[Arm ASTC encoder & format overview](https://github.com/ARM-software/astc-encoder) ·
[Arm ASTC developer guide](https://developer.arm.com/documentation/102162/0430/About-Arm-ASTC-Encoder) ·
[Arm GPU Best Practices — texture sampling](https://developer.arm.com/documentation/101897/v2-2/Buffers-and-textures/Texture-sampling-performance) ·
[Khronos KTX](https://www.khronos.org/ktx/) ·
[Basis Universal](https://github.com/BinomialLLC/basis_universal) ·
[Normal map compression revisited — Ignacio Castaño](http://www.ludicon.com/castano/blog/2026/02/normal-map-compression-revisited/) ·
[ASTC gggr/XXXY normal swizzle discussion](https://github.com/ARM-software/astc-encoder/issues/144)

Craft and practice:
[Computing alpha mipmaps — Castaño / The Witness](http://www.ludicon.com/castano/blog/articles/computing-alpha-mipmaps/) ·
[Texel density deep dive — Beyond Extent](https://www.beyondextent.com/deep-dives/deepdive-texeldensity) ·
[Texel density guide — RebusFarm](https://rebusfarm.net/blog/texel-density-basics-every-artist-should-know) ·
[ORM packing](https://www.strayspark.studio/blog/orm-texture-packing-game-performance-guide) ·
[Trim sheets for modular environments — polycount](http://wiki.polycount.com/wiki/Modular_environments) ·
[Substance Automation Toolkit CLI](https://substance3d.adobe.com/documentation/sat/command-line-tools/sbsrender)

Engines and games:
[Unreal streaming virtual texturing](https://dev.epicgames.com/documentation/en-us/unreal-engine/streaming-virtual-texturing-in-unreal-engine) ·
[Unity recommended texture formats by platform](https://docs.unity3d.com/2023.2/Documentation/Manual/class-TextureImporterOverride.html) ·
[Genshin Impact: crafting an anime-style open world (GDC)](https://www.gdcvault.com/play/1027538/-Genshin-Impact-Crafting-an)
