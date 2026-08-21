# Textures & materials

**Session:** session_01BckkiWsd8dqsjacHidZYPe
**Branch:** `claude/textures-artist-research-bkz70u`
**State:** ready
**Updated:** 2026-08-21 — task 15.1 built; a generated face is rendered and waiting for the owner

## Now

**Task 15.1 is done and needs the owner's eye.** A generated skin is on the shipped body,
rendered through the engine's own textured lit path, with the captures committed as evidence:

| Capture | What it is |
|---|---|
| `evidence/textures/face_483ppm.png` | the face at the chart's current density — **the capture the ADR 0012 ruling needs** |
| `evidence/textures/face_965ppm.png` | the same face at 2× density, for the density question below |
| `evidence/textures/face_threequarter.png` | three-quarter, where a flat texture betrays itself |
| `evidence/textures/body.png` | the whole figure at play distance |
| `evidence/textures/albedo_sheet.png` | the generated sheet itself, flat |

Reproduce: `mge_skin_preview --out <dir> [--sheet 2048]`, then `tools/skin_preview/ppm_to_png.py`.

### My judgment, as input and not as the verdict

Asked by ADR 0014 Ruling 2 whether it reads as skin or as plastic: **the skin reads; the
features are crude but legible.** Specifically —

- **Not plastic.** The tone varies continuously across the body, the perfusion zones put warmth
  where blood actually shows, and there is no visible seam anywhere — the generator evaluates in
  metres on the body, so noise crosses UV island boundaries without knowing they exist.
- **The features are the weak half.** Eyes, brows, lips and nostrils are anthropometrically sized
  and placed but they are ellipses: no eyelid crease, no eye corner, no lash thickness variation.
  The face reads as a face and does not read as a portrait.
- **On the 2.08× Face stretch (ADR 0012 Ruling 1b):** I can see no distortion attributable to it
  in the render. Features are symmetric and correctly proportioned. That is one session's eye on
  one skin, which is exactly why the waiver's retirement condition names the owner and not me.

### The one measured finding the owner should have with the render

**The face does not have enough texels for its own features, and fixing it is nearly free.**

At the chart's current even density the Face island gets **483 px/m — 90 texels across a 186 mm
face.** At that rate:

| Feature | Real size | Texels |
|---|---|---|
| eye opening | 30 mm | 14.5 |
| iris | 11.7 mm | 5.6 |
| **pupil** | **4 mm** | **1.9** |

A pupil two texels wide cannot be drawn, only implied. The two captures above are the same
generator at 483 and 965 px/m and the difference is visible.

The fix does not need a bigger sheet. The Face island is **1.5 % of the sheet today**, the sheet is
only **43.7 % covered**, and reaching 965 px/m on the face costs **6.0 %** — about **4.5 % more
sheet, inside the existing 1024².** Even texel density across regions is the right default and I
wrote that rule myself; the face is the one principled exception, and `skin_texture.mgestd`
already carries a `near_field 1024` density class for exactly this.

This is a chart change, so it is the body session's, and it rides a contract-version event
(B-27). It is listed under `Needs:` below rather than acted on.

## Needs from the architect

```
SEAM: Body contract (skin weights / skinMesh) — blocks 15.2, not 15.1
NEED: `skinMesh()` in engine/src/character/body_mesh.cpp writes position and normal
      but not UV, so a CPU-skinned body samples one texel for its whole surface.
      Every textured character render needs the chart carried through.
BREAKS: One line in skinMesh's vertex write. Nothing downstream: the static
      Vertex already has the uv field and it is currently left at {0,0}.
PROPOSAL: out.vertices[i].uv[0] = sv.uv[0] / 65535.0f; (and [1]). My tool copies
      the UVs across itself so 15.1 could ship; that workaround should not
      outlive this task.
```

```
SEAM: Skinned draw — blocks textured characters on the real path
NEED: `SkinnedDrawItem` has no material, and `skinned.vert` reads inUv at
      location 2 but never outputs it, so the GPU skinned path cannot sample a
      texture at all. Skin is useless on the pipeline characters actually draw
      through; 15.1 only works because it CPU-skins into the static path.
BREAKS: engine/shaders/skinned.vert (pass UV through), the skinned pipeline's
      descriptor layout (bind set 2 as the lit pipeline does), and a
      `const GpuMaterial* surface` on SkinnedDrawItem. Renderer-owned.
PROPOSAL: Mirror what DrawItem already does — same set 2, same lit.frag, same
      1x1-white default so one pipeline serves textured and untextured.
      Verified the same way as everything else on that seam: mge_skin_test.
```

```
SEAM: UV chart density (Face island) — quality, not blocking
NEED: The face needs ~965 px/m to carry a legible pupil; it has 483. Measured
      above, with both renders committed.
BREAKS: The chart, hence every skin texture — a B-27 contract-version event.
      Body session's file.
PROPOSAL: Repack the Face island 2x linear inside the existing 1024^2 sheet
      (+4.5% sheet, from 56% currently unused). Rides the same contract-version
      event as any other chart change; no need to cause one for this alone.
```

**Ruling I need before 15.2:** whether the shared map set is **one sheet for the whole body** or
**a body sheet plus a face sheet**. The measurement above argues for the second, and it changes
what 15.2 builds, so I would rather have it ruled than assume it.

## Last landed

- **15.1 — one generated face, rendered** (this commit). `tools/skin_preview/`: the generator
  (`skin_generator.{h,cpp}`) and the preview tool. Deterministic: two bakes byte-identical
  (`skin_sheet.ppm` md5 `6b8289833016b8727ddbc208cf87d57c`). Colour is specified in CIELAB via a
  melanin/haemoglobin model, so the output has a *checkable* property rather than an opinion:
  this skin measures **ITA 25.6° → Fitzpatrick IV (intermediate)**. Both maps validate against
  `validateTexture` with full 11-level mip chains generated in linear space, and the albedo is
  sRGB-typed as the standard requires.
- The UV audit and `assets/standards/skin_texture.mgestd`, earlier.

## Verification

`scripts/verify.sh`: host tier green (9 OK, 12/12 `ctest`), host runner still printing
`steady-state heap allocations: 0`. The arm64-under-QEMU and APK tiers **skipped in this
environment** — no NDK/SDK present — which I am flagging rather than reporting as green. The
change cannot reach them: everything added is a host-only tool under `tools/`, inside the
`TARGET mge_graphics` block, and no engine, shader, or `app/` file was touched. (I installed
`libvulkan-dev`/`mesa-vulkan-drivers` in this container, so the Vulkan tools now run here — that
is an environment change, not a repository one.)

## Not started

15.2 (generator v1 / the full map set), 15.3 (phenotype binding), 15.4 (the `.mgetex` baker and
its five gates), 15.5 (the crowd proof). 15.3 and 15.5 are the ones that prove ADR 0014 Ruling 1
— nothing built so far allocates per character, and the crowd measurement is what will show it.
