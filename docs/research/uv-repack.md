# The repack, measured — what ADR 0010 cost and what it exposed

**From:** the character asset pipeline session · **To:** the architect, cc the owner, the textures and wearables sessions
**Subject:** Job 3 — the body is repacked, re-imported and re-baked. Four of the standard's six chart rules now pass; the two that do not are named, and one of them is task 13.7.
**Ruling implemented:** `docs/adr/0010-body-uv-chart-policy.md` — one tile, halves **disjoint**
**Instrument:** `tools/model/repack_uv.py` (new, committed) · **Gate:** `mge_uv_report --gate`, the engine's own

---

## 0. Where this starts

The predecessor session measured the pristine source and disproved the corruption
hypothesis (`source-uv-measurement.md`): the CC0 body is a deliberate 24-tile
mirrored UDIM layout, never single-tile. **I reproduced that measurement byte for
byte before changing anything** — same bundle sha256
`811f43ac…b3515`, same `.blend` sha256 `3c121505…c137`, same 24 tiles, same
u 0.038676..8.948707, same 0 straddling faces, same 0 degenerate triangles.
Nothing below rests on taking that report on trust.

## 1. The result

● real output, `mge_uv_report` against the committed `humanoid_template_lod0.mgeskin`:

| | before (shipped v3) | after (this repack) |
|---|---|---|
| triangles with no UV area | 1 973 of 2 200 (**89.7 %**) | **0** (0.0 %) |
| vertices pinned to a tile edge | 1 494 of 1 640 (**91.1 %**) | **0** |
| regions with usable texture space | **3** of 12 | **11** of 12 |
| texel density | 2 281 px/m over 4 % of the body | **505 px/m over all of it** |
| density spread across regions | 3.6× | **0 %** — every region at 505 px/m |
| `chart_in_unit_tile` | FAIL | **pass** |
| `chart_max_degenerate_frac` | FAIL | **pass** |
| `chart_islands_disjoint` | FAIL (50 pairs) | **pass** |
| `chart_min_utilisation` | not checkable | **pass** — 71.1 % (floor 55 %) |
| `density_outside_tolerance` | pass | **pass** (worst region 0 %) |
| `chart_regions_required` | FAIL (Face) | FAIL (Face) — **task 13.7** |
| `stretch_above_max` | FAIL | FAIL — §4 below |

Arms, hands, legs and feet had **no texture space at all** before this. They have
their own, evenly scaled, disjoint islands now.

**New body content hash: `e6eae4e58a7ec271`** (1 916 vertices, 2 200 triangles,
11 parts). All six garments re-cut and re-baked against it; `ctest` 12/12 green;
the host runner still prints `steady-state heap allocations: 0`.

## 2. Disjoint is measured, not asserted

ADR 0010 forbids the mirrored layout. Claiming `merge_overlap=False` in a script
is not evidence that the halves keep their own texels, so the tool measures it:
each triangle is rasterised into a left-half or right-half coverage grid by the
sign of its mean x, and the shared fraction is `|L ∩ R| / |L ∪ R|`.

Run both ways on the same source, ● real output:

```
merge_overlap=True   -> MIRRORED    100.0% of the two halves' texels shared
merge_overlap=False  -> DISJOINT      0.0% of the two halves' texels shared
```

The instrument discriminates, and the shipped chart reads 0.0 %. A scar on one
cheek remains possible; per-side garment masking remains possible.

**The first honest attempt at this got it wrong, and the measurement is what
caught it.** Collapsing all 24 tiles onto tile 0 — the obvious reading of "repack
into the single tile" — lands the mirrored halves *exactly* on top of each other,
and the packer leaves coincident islands where they are. That chart measured
11.7 % shared texels, with Scalp at 118.7 % fill and Torso at 110.2 % (a region
cannot exceed 100 % unless its own islands overlap). It would have delivered the
mirrored layout the ADR rejects, by accident, while every line of the script said
disjoint. The collapse now brings the tiles down to **two**, one per half.

## 3. Three things the gate found that arithmetic would not have

Each of these passed a plausible-looking implementation and failed the engine's
own measurement. They are recorded because each is a trap the next session would
otherwise re-enter.

1. **The chart must be packed on the mesh the gate measures.** Packed on the
   21 160-triangle base and measured on the 2 200-triangle LOD0, **45 of 55
   region pairs overlapped** — because which region a triangle belongs to is read
   from the bone that moves it, decimation moves weights, and
   `chart_islands_disjoint` compares region *bounding boxes*, where one stray
   triangle stretches a region across the sheet. The chart is now packed on LOD0,
   and LOD1/LOD2 are decimated **out of LOD0** so they inherit it.
2. **The importer breaks ties by enum index; this pipeline broke them
   alphabetically.** A 2-2 vote between Torso and a limb goes to Torso in the
   engine (`BodyRegion::Torso` = 3) and went to ArmL/LegL here. That single
   disagreement was the last four failing pairs — Torso/ArmL, Torso/ArmR,
   Torso/LegL, Torso/LegR — and nothing else. Fixed in `repack_uv.face_regions`.
3. **The packer only ever shrinks.** Handed one region's islands at source scale
   it spreads them out rather than filling the space, so region boxes came back
   near tile-sized around almost no content (3–19 % fill, and the *same* foot at
   4.4 % on one side and 48.2 % on the other). Each region is now scaled up
   before packing, and packed with the rest of the body **hidden** — selection
   alone is not enough, because `pack_islands` rescales every island it can see,
   which silently squashed each region already placed.

## 4. The two rules that still refuse, and what they need

**`chart_regions_required` — Face.** Unchanged and expected: this is `B-8` and
**task 13.7**, my next job. ADR 0010 already records that it is achievable —
3 587 of the source's 10 582 vertices are in the head.

**`stretch_above_max` — 8 of 11 regions between 1.40× and 1.97×, threshold
1.50×.** This one needs a ruling rather than a fix from me, and I am flagging it
rather than acting:

- It is **the source unwrap's own distortion**, not the repack's. A per-region
  uniform scale cannot change the ratio of UV area to surface area *within* a
  region. The repack made it *visible* — before, 89.7 % of triangles had no UV
  area, so there was nothing to measure the stretch of.
- Removing it means **re-unwrapping the CC0 body**, i.e. discarding the seams a
  human artist authored and generating new ones. That is originating a UV layout,
  which §6.2 puts outside this session's charter — and the worst readings are
  Hands (1.97×) and Arms (1.74×), where the source's seams are doing real work.
- The overshoot is modest: the worst region is 1.97× against a 1.50× rule, and
  three regions already pass.

```
SEAM: Body contract (UV chart) ⇄ textures standard
NEED: A ruling on `stretch_above_max` for the imported body. The chart now
      passes four of the standard's six chart rules and meets its density and
      evenness targets; stretch is inherited from the CC0 source's authored
      unwrap and cannot be improved by packing.
BREAKS: Nothing shipped. It gates only whether `mge_uv_report --gate` can go
      green before task 13.7, and whether a skin texture may be authored.
PROPOSAL: One of — (a) relax `stretch_max` to 2.0 for the imported body and
      record why, (b) accept the rule failing and let the Face work in 13.7 be
      the trigger to revisit, or (c) commission a re-unwrap from an artist as
      source content (which is in charter — sourced, not originated). I
      recommend (a) or (b); I will not re-unwrap by script on my own judgment.
```

## 5. Density: 505 px/m, against a 512 px/m standard

ADR 0010 quotes the predecessor's 607 px/m for the disjoint layout and calls it
19 % of headroom. The shipped chart measures **505 px/m**, and the difference is
real and worth naming rather than rounding away: their pack put every island in
one undifferentiated pile, which is denser and **fails `chart_islands_disjoint`
outright** (50 region pairs sharing texels, measured). Giving each region its own
box is what `B-27`, `B-28` and the standard require, and it costs that headroom.

505 px/m is 1.4 % under the 512 px/m standard *at a 1024² sheet*, and the density
is now perfectly even (every region 505 px/m, 0 % spread) where before it varied
3.6× across the three regions that had any texels at all. If the owner wants the
target met outright rather than approached, the lever is sheet size, not layout —
a 2048² hero sheet reads 1 010 px/m against the same chart.

## 6. Incidental, reported not fixed

- **`cut_shell` in `humanoid_template.py` has the same tie-break bug** I fixed in
  the packer: its comment says "ties go to the lowest region name, as they do in
  the importer", and the importer does no such thing. Garment cut boundaries can
  therefore disagree with the regions the engine masks, which is a hole waiting
  to happen. I did **not** change it in this push: it would alter every garment's
  cut geometry, and that is a change the wearables session should see coming
  rather than find inside a chart repack.
- **The Blender preview renders labelled "front" were showing the back.** The
  camera sat on −Y with the body facing +Y. Harmless to the assets and corrosive
  to review, since those renders are what an aesthetic gate would be judged on.
  Fixed, and verified against the engine's own `body_preview` sheet.
- **The pipeline did not run on current Blender at all** before this: decimation
  refuses a mesh carrying shape keys, so garment cutting died at the first
  garment. The garment source now drops the body's morphs before decimating —
  it does not want them (a garment follows a morphed body through 13.4's re-fit).

## 7. What a later session needs to reproduce this

```
bundle  human-base-meshes-bundle-v1.4.1.zip  sha256 811f43accbb31a88266d932f8f5563b2d13586fca0ba2693aad1f5fe582b3515
blend   human_base_meshes_bundle.blend       sha256 3c121505651140ceb4d69fd1d8923f7788ffadd81672f5be14845a5f2c75c137
```

`tools/model/vendor/` is gitignored, so the hashes are the check. `bpy` 5.0.1 via
PyPI reproduces every measurement in this note.

```
python3 tools/model/repack_uv.py                    # measure the repack alone
python3 tools/model/humanoid_template.py <outdir>   # the whole body pipeline
mge_asset_import --skinned <lod>.glb <lod>.mgeskin  # through the hardened path
mge_uv_report                                       # the gate, on the result
mge_garment_fit                                     # re-bake all six garments
```
