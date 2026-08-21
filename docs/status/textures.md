# Textures & materials

**Session:** session_01BckkiWsd8dqsjacHidZYPe
**Branch:** `claude/textures-artist-research-bkz70u`
**State:** working
**Updated:** 2026-08-21 — re-aimed onto the reversal; 15.1 was built against the superseded
ruling and is re-labelled below rather than claimed

## Now

**Re-aimed.** I built and pushed a *generated* face (commit `67bcf5d`) against ADR 0014 as it
stood when I was dispatched. The owner reversed that ruling the same day — **base maps are
imported, not generated** — and I merged the reversal before this update. Correcting my own
record first, because a status file claiming a delivered 15.1 would be false under the current
charter:

- **15.1 is NOT done.** It needs *an imported* face. What is committed is a generated one.
- **15.0 is now first**, and it is a measurement, not an assumption.

I am starting 15.0. The generated face stays in the tree as evidence about the *chart and the
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

**This now bears on 15.0 directly:** it sets the minimum resolution a sourced skin must have in
the face region to be worth acquiring, so it is an input to the survey rather than a later polish
item.

## Needs from the architect

```
SEAM: Body contract (skinMesh) — blocks any textured character
NEED: `skinMesh()` in engine/src/character/body_mesh.cpp writes position and normal
      but not UV, so a CPU-skinned body samples one texel for its whole surface.
BREAKS: One line in skinMesh's vertex write. Nothing downstream: the static
      Vertex already has the uv field and it is currently left at {0,0}.
PROPOSAL: out.vertices[i].uv[0] = sv.uv[0] / 65535.0f; (and [1]). My tool copies
      the UVs across itself as a local workaround; it should not outlive this.
```

```
SEAM: Skinned draw — blocks textured characters on the path they are really drawn through
NEED: `SkinnedDrawItem` has no material, and `skinned.vert` reads inUv at
      location 2 but never outputs it, so the GPU skinned path cannot sample a
      texture at all. An imported skin is as useless here as a generated one.
BREAKS: engine/shaders/skinned.vert (pass UV through), the skinned pipeline's
      descriptor layout (bind set 2 as the lit pipeline does), and a
      `const GpuMaterial* surface` on SkinnedDrawItem. Renderer-owned.
PROPOSAL: Mirror what DrawItem already does — same set 2, same lit.frag, same
      1x1-white default so one pipeline serves textured and untextured.
      Verified on that seam the usual way: mge_skin_test.
```

```
SEAM: UV chart density (Face island) — now an input to 15.0, not a polish item
NEED: The face needs ~965 px/m to carry a legible pupil; it has 483. Measured,
      with both renders committed.
BREAKS: The chart, hence every skin texture — a B-27 contract-version event.
      Body session's file.
PROPOSAL: Repack the Face island 2x linear inside the existing 1024^2 sheet
      (+4.5% sheet, from 56% currently unused). Rides the next contract-version
      event rather than causing one.
```

**No ruling needed to proceed with 15.0** — the reversal answers the route question. The question
I *will* bring back after 15.0 is whether what is actually sourceable clears the bar, and at what
licence.

## Last landed

- **`67bcf5d` — a generated face, rendered** (`tools/skin_preview/`). Built against ADR 0014
  pre-reversal; **re-labelled above, not claimed as 15.1.** Deterministic (two bakes
  byte-identical), maps validate with full 11-level linear-space mip chains, albedo sRGB-typed.
  Captures in `docs/status/evidence/textures/`.
- **`3f63609` — the UV audit** that found the chart unusable, and
  `assets/standards/skin_texture.mgestd`, the standard the import gates will run against.

## Verification

`scripts/verify.sh`: host tier green (12/12 `ctest`), host runner still printing
`steady-state heap allocations: 0`. arm64-under-QEMU and APK tiers **skipped here** — no NDK/SDK
in this container — flagged rather than reported green. Everything added is a host-only tool
under `tools/`; no engine, shader or `app/` file was touched.

## Not started

15.0 (sourceability — starting now), 15.1 (one *imported* face), 15.2 (the import path:
re-projection through the inverse repack transform + conformance gates), 15.3 (phenotype
binding), 15.4 (the `.mgetex` baker), 15.5 (the crowd proof). 15.3 and 15.5 are what prove
Ruling 1, which the reversal left standing.
