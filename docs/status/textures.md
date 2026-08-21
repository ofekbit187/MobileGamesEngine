# Textures & materials

**Session:** session_01BckkiWsd8dqsjacHidZYPe
**Branch:** `claude/textures-artist-research-bkz70u`
**State:** ready
**Updated:** 2026-08-21 — **an imported face is on the body and rendering.** 15.1 needs the owner's eye

## Now

**A real photographic CC0 skin is on our body, through a mesh-to-mesh transfer, and it renders.**
That is the capture ADR 0012's provisional Face waiver has been waiting for — over to the owner.

| Evidence | What it is |
|---|---|
| `evidence/textures/imported_face.png` | **the portrait the ruling needs** |
| `evidence/textures/imported_threequarter.png` | three-quarter |
| `evidence/textures/imported_body.png` | the whole figure at play distance |
| `evidence/textures/imported_sheet.png` | the transferred sheet, flat, on our frozen chart |

Reproduce: `mge_skin_import --mesh <source.obj> --texture <source.png> --out <dir>`. The source is
a build-time input and is not committed, the same convention `humanoid_template.py` follows for
the Blender bundle; provenance and licence in `research/skin-sourceability.md`.

### What 15.2 turned out to need, and what it measured

One rigid transform cannot fit two humans in **different poses** — the source holds its arms
wider than ours. The first run measured 71 mm mean separation and **64 mm across the face**, which
is two eye-widths: every feature landed somewhere else. So the transfer fits **each body region
separately** by iterated closest point, using the segmentation our body already publishes, which
means the source needs no segmentation of its own:

| Region | before | after | how far it moved |
|---|---|---|---|
| Face | 62.5 mm | **5.2 mm** | 92 mm |
| Torso | 67.1 mm | 10.1 mm | 93 mm |
| Hands | 200.5 mm | 10.1 mm | **224 mm** |
| Arms | 90.8 mm | 31.0 mm | 91 mm |

The hands were **22 cm** out of correspondence under a single transform. After fitting: whole body
**15.6 mm** mean, **7.1 mm across the face** — comfortably inside an iris.

Two defects found by looking at the render, both fixed and both worth recording because they are
properties of *importing onto a different body*, not of this particular source:

- **Red eyes and a red mouth.** MakeHuman models eyeballs, teeth and tongue as separate meshes, so
  its sheet carries saturated interiors for surfaces our closed face shell does not have.
  Rejected by testing each sample against the **CIELAB skin locus** — the same published colour
  science the ITA classifier uses, applied as a validator rather than as taste — and filled from
  surrounding skin by the dilation that was already there. 5 698 samples rejected, 3 271 on the face.
- **Black bands around both wrists.** Per-region offsets are discontinuous where regions meet, and
  arm-to-hand is a 13 cm step. Fixed by carrying the offsets **per vertex**, relaxing them across
  the mesh and blending per texel, so the field is continuous by construction. One small dark patch
  survives on one wrist — visible in `imported_body.png`, and honest to leave visible.

Deterministic: two imports byte-identical (`imported_sheet.ppm` md5 `0e4b9b3b96c14a4879e45494332487cc`).

### My judgment, as input and not the verdict

**The skin reads as skin — it is photographic, so it should.** Tone, mottling and the shading
around the nose, mouth and collarbones all land; there is no visible UV seam anywhere; at play
distance it reads as a person. It is a categorical improvement on what a generator of ours
produced, which is what the owner said it would be.

**But the face has no eyes, and that is a geometry gap, not a texture one.** The source assumes a
separate eyeball mesh, so what it supplies for the eye region is *eyelid skin over a closed
socket*. Our face is one closed shell with no eye opening, so the imported face reads as a man
with his eyes shut. No skin — imported, generated or painted — can fix this: it needs either eye
geometry, or eyes painted in, and painting them in is originating. **This wants a ruling**, and it
is listed under `Needs:`.

Also worth the owner knowing when he looks: **this is the face at 483 px/m**, before the 2× Face
repack riding the ADR 0019 clavicle event. It gets better on its own.

## Needs from the architect

```
SEAM: Body geometry — the face has no eyes, so an imported skin cannot give it any
NEED: Sourced skins assume separate eyeball geometry (MakeHuman, and every other
      photographic human skin, is authored that way). Our Face is one closed shell
      with no eye opening, so the transfer supplies eyelid skin over a closed
      socket and the face reads as eyes-shut. Visible in imported_face.png.
BREAKS: The body's head geometry — eye geometry is a mesh change, so it rides a
      contract-version event. Body session's call, not mine.
PROPOSAL: Two options, and I do not think it is mine to choose.
      (a) Eye geometry on the template — a sourced eyeball, maskable, on the same
          contract-version event as the clavicle if it can still be added.
      (b) Accept eyes-shut for now and revisit. Cheap, and honest, but every
          character in the game has its eyes closed.
      What I will NOT do is paint eyes into the skin: that is originating what a
      session cannot see, which is the rule ADR 0014's reversal restored.
```

Everything else I had open is now ruled and merged: `skinMesh()` carries UVs (my workaround is
deleted), the skinned draw path is chartered to the renderer, and the Face repack rides ADR 0019.

## Last landed

- **15.1 + the core of 15.2** (this commit). `tools/skin_import/`: OBJ loader, `stb_image`
  vendored for the import path, similarity alignment with the **facing chosen by measurement**
  rather than assumed, per-region iterated-closest-point fitting with a smoothed per-vertex
  offset field, skin-locus rejection, and the transfer itself. The result goes through the bake
  path that already existed — dilate, mip in linear space, sRGB-tagged, `validateTexture` — and
  renders through the engine's textured lit pipeline.
- **15.0** — `research/skin-sourceability.md`, the survey that found this route.
- The UV audit and `assets/standards/skin_texture.mgestd`, earlier.

## Earlier in this session

### Previously — re-aimed onto the reversal

I built and pushed a *generated* face (commit `67bcf5d`) against ADR 0014 as it
stood when I was dispatched. The owner reversed that ruling the same day — **base maps are
imported, not generated** — and I merged the reversal before this update. Correcting my own
record first, because a status file claiming a delivered 15.1 would be false under the current
charter:

- **15.1 is NOT done.** It needs *an imported* face. What is committed is a generated one.
- **15.0 is now first**, and it is a measurement, not an assumption.

The generated face stays in the tree as evidence about the *chart and the
render path* — see "What survives the reversal" — but it is not a content route and I am not
treating it as a fallback. If nothing turns out to be sourceable, that comes back here as a
finding, per the reversal's closing instruction.

### What survives the reversal, and what does not

Route-**independent**, and needed by 15.1/15.2/15.4 whatever the maps' origin:

| Piece | Why it still applies |
|---|---|
| Surface-map rasterizer (mesh → chart space, per-texel position/normal/region) | this is how a re-projected sample is *checked* to land correctly on the frozen chart — 15.0's second half |
| Mip chain in linear space, island dilation, `validateTexture` round-trip | four of 15.4's five gates, already exercised on a real map |
| AO bake (ray-cast body against itself) | an imported albedo still needs the packed map's AO channel |
| The render harness (put a map on the body, capture portrait / three-quarter / body) | 15.1 needs exactly this, pointed at an imported map instead |
| The Face density measurement | **more important now, not less** — a photographic skin needs texels to carry a pupil just as much |
| Both seam requests below | unchanged by where maps come from |

**Superseded:** the melanin/haemoglobin colour model and the procedural facial features, as a
*source of content*. They are how you originate skin, which is what the reversal forbids.

### The measured finding, which is route-independent

**The face does not have enough texels for its own features.** At the chart's even density the
Face island gets **483 px/m — 90 texels across a 186 mm face**:

| Feature | Real size | Texels |
|---|---|---|
| eye opening | 30 mm | 14.5 |
| iris | 11.7 mm | 5.6 |
| **pupil** | **4 mm** | **1.9** |

A pupil two texels wide cannot be resolved by any map, imported or otherwise. Committed under
`evidence/textures/` are the same content at 483 and 965 px/m; the difference is visible.

The fix needs no bigger sheet: the Face island is **1.5 % of a sheet that is 43.7 % covered**, and
doubling its linear density costs about **4.5 % more sheet inside the existing 1024²**. Even
density across regions is the right default — I wrote that rule — and the face is the principled
exception the standard's `near_field 1024` class already exists for.

**15.0 turned this around:** the sourced content has ~30× the face texels our chart can hold, so
the constraint is not what we can acquire — it is what our chart can accept.


## Verification

`scripts/verify.sh`: host tier green (12/12 `ctest`), host runner still printing
`steady-state heap allocations: 0`. arm64-under-QEMU and APK tiers **skipped here** — no NDK/SDK
in this container — flagged rather than reported green. Everything added is a host-only tool
under `tools/`; no engine, shader or `app/` file was touched.

## Not started

15.1 (one *imported* face), 15.2 (the import path:
re-projection through the inverse repack transform + conformance gates), 15.3 (phenotype
binding), 15.4 (the `.mgetex` baker), 15.5 (the crowd proof). 15.3 and 15.5 are what prove
Ruling 1, which the reversal left standing.
