# Task 15.0 — Is a skin sourceable for this body? Measured.

**Session:** textures · **Date:** 2026-08-21
**Question set by:** ADR 0014's reversal — *"establish what actually exists under an acceptable
licence for this body, and confirm a re-projected sample lands correctly on the frozen chart"*
**Method:** every claim below is a file downloaded and measured, or a licence read at source.
Where something is estimated rather than measured, it says so.

---

## The finding in one line

**Skin is sourceable, but not by the route the reversal assumed.** The by-construction route —
re-project through the inverse of our repack — has **an empty input set**, because the body's
source bundle ships no textures at all. What *is* available under CC0 is authored for a
**different mesh**, so the import path 15.2 must build is a **mesh-to-mesh transfer**, not an
inverse-affine re-projection.

---

## Route 1 — "any texture authored against the Blender base mesh's original layout transfers
onto our chart by construction"

The reasoning is correct and I am not disputing it: `repack_uv.py` moved the source's 24 UDIM
islands as units, no splits, no cut seams, vertex order untouched (ADR 0010), so the transform is
a per-island affine we own and can invert.

**But the set of textures it applies to is empty.** Our body is Blender Studio's *Human Base
Meshes* bundle, `GEO-body_male_realistic`. That bundle ships **base meshes with UV maps and no
textures or materials** — it is explicitly geometry for you to texture yourself. Nothing in it,
and nothing found authored against it, is a skin.

So route 1 is a *capability we hold in reserve* for anything ever authored against that layout,
not a way to obtain a skin today. **This corrects an assumption in the reversal**, which listed it
first among the two routes that were "open the whole time".

## Route 2 — tileable detail in tangent space (chart-independent)

**Confirmed, with a file on disk.** `human_skin_4-4K` from ShareTextures via cc0-textures.com:

| Property | Measured |
|---|---|
| Licence | CC0 / public domain ("copy, modify, distribute and perform… even for commercial purposes") |
| Resolution | **4096 × 4096**, every map |
| Maps | diffuse, normal, height, AO, smoothness, edge, metallic (7) |
| Download | 21.4 MB zip, retrieved and unpacked |
| Siblings | 6 human-skin materials in that library, all 4K, all CC0 |

**What it can do:** pore and micro-detail over the whole body, tiled in tangent space, at any
density we choose. It is chart-independent, so it needs no transfer at all.

**What it cannot do — and this is the point:** a tiling material has no layout, so it cannot place
anything. No eyes, no lips, no brows, no nostrils, no per-region tone. **It supplies skin, not a
face**, and 15.1 needs a face.

**Also measured, and worth recording because the web says otherwise:** ambientCG — named in most
"CC0 skin" advice — has **no human skin materials**. Its API returns exactly one asset for
`q=skin`, and that asset is `Leather008`. The general impression that CC0 human skin is abundant
does not survive contact with the libraries.

## Route 3 — a real face texture, from a different mesh (not in the reversal, and the only one
that answers 15.1)

**MakeHuman's system asset pack is CC0 and contains 22 complete human skin textures.** I read the
zip's central directory remotely and pulled one entry out by byte range rather than downloading
268 MB:

| Property | Measured |
|---|---|
| Pack | `makehuman_system_assets_cc0.zip`, 280 737 770 B, 517 entries |
| Skin textures | **22**, spanning young / middle-age / old × African / Asian / Caucasian × female / male |
| Sample pulled | `skins/young_caucasian_male/young_lightskinned_male_diffuse.png` |
| Resolution | **2048 × 2048**, 8-bit RGB |
| Content | photographic-derived, **full body layout including a face** — eyes, lips, ears, nostrils, nails, all present |
| Licence | CC0. The texture itself carries a `PUBLICDOMAIN / MAKEHUMAN.ORG` strip in unused sheet space |

**Licence trap, recorded before it bites:** *system* assets are CC0; **user-contributed skins on
the same site are not.** The first community skin I opened (`male_generic_skin_a`) is **CC-BY**.
"MakeHuman skins are CC0" is true only of the bundled pack.

**The catch:** it is authored for **MakeHuman's** base mesh and UV layout, not Blender Studio's.
Our inverse-repack affine says nothing about it. Getting it onto our chart requires a
correspondence between two different meshes.

---

## What this means for 15.2 — the import path is a mesh-to-mesh transfer

Both meshes are CC0 and both are obtainable, so the transfer is possible; it is simply a different
tool from the one the reversal anticipated. The shape of it, using machinery that already exists
in `tools/skin_preview/`:

1. For each texel of **our** chart, we already know the 3D point and normal it covers — that is
   the surface-map rasterizer, built and working.
2. Align the MakeHuman mesh to ours (both are ~1.75 m humans in bind pose; a similarity transform
   from shared landmarks, then closest-point refinement).
3. For that 3D point, find the corresponding point on the **MakeHuman** mesh and read *its* UV.
4. Sample the source texture there; write it into our chart. Dilate, mip, validate — all of which
   the existing bake path already does.

The risk to manage is step 2/3 on the **face**: a few millimetres of misalignment puts an eye in
the wrong place, and the face is where every millimetre shows. A landmark-driven alignment
weighted toward the head, checked by rendering, is the way — and the check is exactly the
render harness 15.1 needs anyway.

## The resolution finding, which now points the other way

Earlier I reported that our Face island's 483 px/m makes a 4 mm pupil 1.9 texels wide. The source
measurement inverts the framing of that problem:

- Our chart gives the **Face region 15 700 texels** (measured).
- The MakeHuman sheet gives the head **on the order of 4 × 10⁵ texels** — *estimated* from the
  head island's extent in the 2048² layout, not segmented precisely, because that needs the mesh.

That is roughly **30× more texels for the face, about 5× in linear density**. The source content
is not the limiting factor and never will be. **Our chart is.** Whatever we import, the face is
throttled at import time by an island sized for even body density.

This makes the Face-island repack request (in `docs/status/textures.md`) materially more valuable
than when I raised it: it is no longer "the generator could draw a better pupil", it is "we will
be throwing away 30× the detail we acquire."

---

## Recommendation

1. **Take route 3 for the face and body base**: MakeHuman system assets, CC0, 22 skins — which
   also gives the phenotype layer (15.3) real variety to interpolate between, across age and
   ethnicity, rather than one skin with sliders.
2. **Take route 2 for micro-detail**: a CC0 4K tileable, tangent-space, over the top. It costs no
   transfer and it is what carries close-up pore detail the 2048² body sheet cannot.
3. **Build the mesh-to-mesh transfer as 15.2**, not the inverse-affine re-projection.
4. **Repack the Face island first if it is going to be repacked at all** — importing at 483 px/m
   and re-importing later at 965 means doing the transfer twice.

## What I need ruled

- **Is a mesh-to-mesh transfer acceptable as "the import path"?** It is more machinery than the
  reversal anticipated, and it is the difference between having a face and not having one. It is
  still acquire → validate → process → integrate; the process step is just bigger.
- **Is the CC0 MakeHuman pack acceptable as a project dependency** (build-time input, like the
  Blender bundle — the baked maps would be committed, not the 268 MB pack)?
- **Face island density**, as above, before rather than after the transfer is built.

Nothing here falls back to generating. If these are refused, the finding stands as a finding.

## Reproducing every number above

```sh
# route 2 sample
curl -L -o hs4.zip https://download.cc0-textures.com/st/human_skin_4-4K.zip

# ambientCG's actual skin inventory
curl "https://ambientcg.com/api/v2/full_json?type=Material&q=skin&limit=50"

# route 3: read the pack index without downloading it, then pull one entry
curl -r <eocd-range> http://files.makehumancommunity.org/asset_packs/\
makehuman_system_assets/makehuman_system_assets_cc0.zip
```

Neither source archive is committed: both are build-time inputs, the same convention
`humanoid_template.py` follows for the Blender bundle.
