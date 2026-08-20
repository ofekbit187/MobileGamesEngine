# UV chart audit — handoff to the body-modeling session

**From:** the textures session · **To:** the body-modeling session, cc the architect
**Subject:** the delivered v3 template body ships without a usable UV chart
**Instrument:** `tools/uv_report` (`mge_uv_report`), runs in `ctest`; `--gate` exits
non-zero for use as an acceptance gate
**Standard it measures against:** `assets/standards/skin_texture.mgestd`
**Measured on:** the v3 imported body at LOD0, on integration
(`humanoid_template_lod0.mgeskin`, 1 640 vertices / 2 200 triangles)

---

## 0. Read this first — the earlier audit is superseded

The four defects reported on the review board (density spread 3.6×, intra-island
stretch, palm/thumb overlap, empty `Face` island) were measured against the
**previous, code-generated body**. That body no longer exists: the v3 body is
Blender Studio's CC0 base mesh, imported (MODELING.md §0). I re-ran the audit
against the delivered mesh before filing, and the picture changed:

| Earlier finding | Status against v3 |
|---|---|
| D1 density spread 3.6× | **Superseded** — not measurable; see D-A |
| D2 stretch up to 3.3× | **Superseded** — not measurable; see D-A |
| D3 palm and thumb share texels | **Fixed** — v3 has one shell per region, 11 parts, no duplicates |
| D4 `Face` island empty | **Confirmed** — carried forward as D-C |

In their place is a single defect that subsumes the first two and blocks all
texture work outright. **`docs/MODELING.md` §5 currently carries the superseded
numbers because I wrote them there before the merge — that file is the
architect's; §5 below proposes the replacement text.**

---

## D-A — The chart is destroyed on import: 90 % of the body has no texture space

**Severity: blocking.** Nothing can be textured. This is not an evenness
problem; it is the absence of a chart over nine of twelve regions.

### What the measurement shows

```
whole body: 2200 triangles, 1973 with no UV area (89.7%), 1617 edge-locked
            1494 of 1640 vertices sit exactly on a tile edge (91.1%)

region  shells   tris no-UV-area edge-locked   usable%
Scalp        1    428        241          52     43.7%
Neck         1     54         30          10     44.4%
Torso        1    256        240         140      6.2%
ArmL         1    190        190         168      0.0%
ArmR         1    198        198         178      0.0%
HandL        1    130        130         130      0.0%
HandR        1    132        132         132      0.0%
LegL         1    206        206         204      0.0%
LegR         1    200        200         197      0.0%
FootL        1    204        204         204      0.0%
FootR        1    202        202         202      0.0%
```

Only **4.5 % of the body's surface area** has usable UVs. Both arms, both
hands, both legs and both feet have **zero** — every triangle in them maps to a
line, not an area.

### Root cause: UVs outside the 0–1 tile, silently clamped at import

The `u` histogram over all 1 640 vertices:

```
u=0.0:    2   u=0.1:   20   u=0.2:   38   u=0.3:   38   u=0.4:   48
u=0.5:   39   u=0.6:   57   u=0.7:   59   u=0.8:   61   u=0.9:   37
u=1.0: 1241   <-- the clamp edge
```

A smooth distribution across the tile, then **1 241 vertices piled on exactly
u = 1.0**. Two further facts make the diagnosis certain:

- **1 617 triangles have all three vertices pinned to a tile edge; exactly
  zero triangles are partially pinned.** Whole islands were flattened, none
  straddle the boundary — which is what happens when an island lies wholly
  outside the tile, and never what a real unwrap produces.
- The 399 unclamped vertices carry 396 distinct `u` values. A genuine unwrap
  *does* exist in the source; only the part inside the tile survived.

The mechanism is `engine/src/import/gltf_skin_import.cpp:185`:

```cpp
const float t = uv[k] < 0.0f ? 0.0f : (uv[k] > 1.0f ? 1.0f : uv[k]);
vertex.uv[k] = static_cast<uint16_t>(t * 65535.0f + 0.5f);
```

The source mesh's unwrap extends beyond `u = 1` — a second UDIM tile, or a
layout that was never normalised into the tile. The engine's `SkinVertex`
stores UVs as normalized `uint16` (**B-3**), so values above 1 are not
representable; the importer clamps them and the chart is destroyed. **Silently
clamping is what turned a fixable export setting into committed, damaged
content that shipped through three LODs without anyone noticing.**

All three LODs are equally affected, so decimation is not the cause:

| Asset | u == 1.0 | UV-degenerate triangles |
|---|---|---|
| `humanoid_template_lod0` | 75.7 % | 89.7 % |
| `humanoid_template_lod1` | 76.5 % | 90.5 % |
| `humanoid_template_lod2` | 74.9 % | 90.0 % |

### The fix

**Source-side, in `tools/model/humanoid_template.py`: normalise the unwrap into
the 0–1 tile before export, and repack the islands per region.** Not a uniform
scale — that would halve horizontal density and keep the layout's original
imbalance. Repack island by island to equal texels-per-square-metre (D-B).

Please also **report the source unwrap's true `u`/`v` extent** when you look:
the clamped data cannot tell us whether the layout was two tiles, a mirrored
overlap, or something else, and that decides how much repacking is real work
versus an affine transform.

### Is this UV-only, or topology?

**UV coordinates only, if the existing seams suffice** — a repack that moves and
rescales existing islands changes `SkinVertex::uv` and nothing else: same
vertex count, same order, same triangles. Under **B-13** that is *not* a
topology event, but it **is** a **B-27 contract-version event**: announced,
versioned, hash-recorded.

**It becomes a topology event if the repack needs new UV seams.** A new seam
splits vertices, which changes vertex count and order and invalidates every
garment binding and morph delta by index (B-13). Six garment assets are already
committed. **Please determine which case applies before cutting anything**, and
if new seams are needed, raise it to the architect as a contract-version event
rather than absorbing it.

Either way this is the cheapest it will ever be: **zero skin textures exist.**

---

## D-B — Evenness is unverified, and cannot be verified until D-A is fixed

The standard requires every region within **±15 %** of the chart mean
(`density_tolerance`) and at most **1.50×** stretch inside an island
(`stretch_max`) — the numeric form of MODELING.md §5's "a texture must not be
sharper on the hands than on the torso."

Against v3 the tool refuses to rule: only 4.5 % of the surface is measurable,
and a verdict on that sliver would describe the sample, not the body. What
little is measurable hints the repack has real work to do — Scalp 2 287 px/m,
Neck 2 334, Torso 2 173, with Scalp at 1.63× stretch (over the 1.50 limit) —
but treat those as symptoms of the damage, not as the chart's true densities.

**After the D-A repack, `mge_uv_report --gate` returns pass/fail on this
automatically.** Target: mean ≈ **512 px/m at a 1024² sheet** for skin
(`skin_density_class standard`).

---

## D-C — `Face` region has no geometry, and the face is the texture's whole job

Confirmed on v3 and already known to the architect (AGENTS.md §10.1 contract
debt, tasks 13.7–13.9). Recorded here because it lands differently on textures
than on wearables:

- **For wearables** it blocks the first mask, visor or face-covering helm.
- **For textures it is worse**, because MODELING.md §1 requires the face to
  carry eyes, brows and mouth **entirely in texture** — the geometry
  deliberately omits them. A body whose most-looked-at surface has no region of
  its own cannot be given a face at all, and the scalp island it currently
  shares is dominated by the back of the head.

**Fix:** when the head is split, give `Face` its own island **and more than an
even share of the sheet** — it is the one region where the standard's
`near_field` density class (1024 px/m) is justified rather than indulgent.
Under B-8 the region must exist regardless; this asks only that its island be
sized for what it must carry.

---

## D-D — Region islands overlap each other (Scalp / Neck / Torso)

The three regions that still have any UV area have **overlapping island
bounding boxes**. Some of this is an artefact of D-A — clamped islands collapse
onto the same edge — so it may resolve itself with the repack. It is listed so
the repack is checked for it rather than assumed clean: two regions sharing
texels cannot be painted differently from each other, and texture-space masking
(ADR 0008, research §3.3) addresses regions independently or not at all.

The standard requires disjoint islands with a **4-texel gap**
(`chart_islands_disjoint`, `chart_min_island_gap_texels`), so filtering at the
smallest streamed mip cannot bleed one region into its neighbour.

---

## Seam request

```
SEAM: Body contract (UV chart) / model import
NEED: The importer must REFUSE a UV set it cannot represent, not clamp it.
      Clamping converted a fixable export setting into committed damaged
      content across three LODs and six garments, and nothing reported it.
      Textures cannot trust a chart that can be silently destroyed in transit.
BREAKS: `engine/src/import/gltf_skin_import.cpp` (currently clamps to [0,1]);
      whoever owns the import path — the ownership map does not name
      `engine/*/import/**`, which is itself worth ruling on.
PROPOSAL: On import, if any UV falls outside [0,1], fail the import with the
      measured extent in the message ("u spans 0.000..1.983 — normalise the
      unwrap into the 0-1 tile"). No silent clamp, no partial success. The
      uint16 UV encoding (B-3) makes the tile a hard limit, so refusing is the
      only honest behaviour. Optionally add the same check to the body's
      acceptance gates so a bad chart fails at bake, before it is committed.
```

---

## Proposed replacement text for `docs/MODELING.md` §5

*(The architect owns that file; this is the correction, not an edit.)*

Replace the bullet beginning "**The current chart does not meet the rule
above.**" with:

> - **The delivered v3 chart is not usable.** `tools/uv_report` measures it:
>   89.7 % of the body's triangles have zero UV area, and 91.1 % of its
>   vertices sit exactly on a tile edge — the source unwrap extends past
>   `u = 1` and the importer clamps it flat, destroying the chart for both
>   arms, both legs, both hands and both feet. Repacking the unwrap into the
>   0–1 tile is a **B-27 contract-version event** and must happen before any
>   skin texture exists (that count is zero today). Measurements, root cause
>   and the handoff: [`research/uv-audit.md`](research/uv-audit.md).

---

## What the body session gets, concretely

1. `mge_uv_report` — run it after any chart change; `--gate` for pass/fail.
   It reads the standard from data, so tightening a tolerance is a one-line
   edit to `assets/standards/skin_texture.mgestd`, not a code change (P12).
2. The rules it enforces, with reasons attached to every refusal — an artist
   can act on them without an engineer reading the output.
3. This document, for the *why* behind each rule.

When the chart conforms, the report prints `chart status: CONFORMS` and the
textures session can author the first skin against it.
