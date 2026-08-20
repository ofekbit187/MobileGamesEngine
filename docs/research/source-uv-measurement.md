# The pristine source's UV chart — measured before anything touched it

**From:** the character asset pipeline session · **To:** the architect, cc the owner and the textures session
**Subject:** the corruption hypothesis behind the 2026-08-20 ruling is **wrong**, and the correction changes what Job 3 can be
**Instrument:** `tools/model/measure_source_uv.py` (reads the `.blend` and nothing else — no placement, no scale, no rig, no decimation)
**Source:** Blender Studio *Human Base Meshes* bundle **v1.4.1** (CC0), object `GEO-body_male_realistic`

```
human-base-meshes-bundle-v1.4.1.zip  sha256 811f43accbb31a88266d932f8f5563b2d13586fca0ba2693aad1f5fe582b3515
human_base_meshes_bundle.blend       sha256 3c121505651140ceb4d69fd1d8923f7788ffadd81672f5be14845a5f2c75c137
```

Fetched from `https://download.blender.org/demo/asset-bundles/human-base-meshes/`.
The bundle is gitignored (`tools/model/vendor/`), so the hashes are how a later
session confirms it measured the same bytes.

---

## 0. The answer, first — the owner asked for this either way

**The source unwrap was never inside the 0–1 tile.** It is a clean, deliberate
**24-tile UDIM layout**:

```
u extent : 0.038676 .. 8.948707
v extent : 0.030347 .. 3.966712
outside the 0-1 tile : 35 607 of 42 340 uv loops (84.1 %)
uv area covered      : 9.4867 tiles over 21 160 triangles
zero-area triangles  : 0 (0.0 %)
loops at exactly u = 1.0 : 0        <-- the clamp fingerprint is ABSENT
tiles occupied           : 24
faces straddling a tile border : 0
```

So the retired session **did not corrupt a clean chart**. There was no clean
single-tile chart to corrupt. The source's chart is healthy *as a UDIM layout* —
zero degenerate triangles, zero faces straddling a tile border, no pile-up
anywhere — and it is simply not representable in one `uint16` tile.

The damage still happened exactly where the audit said it did (the importer's
silent clamp, `docs/research/uv-audit.md` D-A). What was wrong was only the
**attribution**: the clamp destroyed a multi-tile chart that arrived that way,
rather than a single-tile chart our processing had moved.

### Why this is not a quibble

The ruling of 2026-08-20 says the body *"re-imports fresh from the pristine
Blender CC0 source through the hardened importer"*, and my brief says to
*"preserve the source's own UV chart verbatim wherever it is already clean."*

**Both are now impossible as written, and they fail in a way that proves the
Job 1 gate works.** A verbatim re-import of the pristine source through the
hardened importer will be **refused** — correctly — because 84.1 % of its UV
loops are outside the tile the vertex format can represent. The refusal will
read like this (○ **projected**, not captured: producing the real message needs
a rigged glTF export of the untouched mesh, and the importer checks the rig
before it looks at UVs, so the raw source cannot reach the UV gate yet — the
loop counts below will also shift to glTF vertex counts, which split at UV
seams):

```
UV coordinates fall outside the 0-1 tile, which the uint16 vertex format
cannot represent (B-3): u spans 0.0387..8.9487, v spans 0.0303..3.9667 —
<n> of <m> vertices outside. Normalise the unwrap into the 0-1 tile
before export.
```

The u/v extents in it are ● real, measured above. The gate's behaviour itself is
● real and tested on a fixture built to the same shape (a second island pushed
past u = 1): `skin_import_refuses_uvs_outside_the_tile_instead_of_clamping`.

A repack into the tile is therefore **not optional and not scope creep — it is
the only path from this source to a textured body.** It is also precisely what
uv-audit D-A asked the body session to do. I am not treating that as licence to
proceed unasked, because the repack forces one decision that is a *policy*, not
a mechanism (§2).

---

## 1. What the layout actually is

The 24 occupied tiles are a **mirrored pair**. Faces split exactly in half, and
the second half is the first translated by +2 in `v`:

```
faces with v < 2  : 5295      mean x = -0.1678   (the body's right half)
faces with v >= 2 : 5295      mean x = +0.1678   (the body's left half)
1235 of 1236 distinct min-v values in the upper block coincide with the
lower block once the +2 shift is removed
```

Unique chart content is therefore **12 tiles / 4.7434 tiles of UV area**; the
other half is a translated copy of it.

| | |
|---|---|
| Source triangles | 21 160 (10 590 quads, 10 582 vertices) |
| Source height | 1.6900 bu → ×1.0355 to reach the 1.75 m template |
| Body surface area at 1.75 m | **1.9020 m²** |
| Chart area, both halves | 9.4867 tiles |
| Chart area, unique | 4.7434 tiles |

---

## 2. The one decision I will not make — mirrored or disjoint

Packing into a single tile forces a choice about the mirrored halves, and it is
a content-policy call with consequences on the textures and wearables sides.
**I am raising it rather than picking it**, because "how the sheet is laid out"
is the kind of judgment that got the previous session retired.

**The good news is that it is not a budget crisis.** I expected it to be, and it
is not. Rather than argue it from arithmetic, I ran both packs into a throwaway
scene and measured the result — ● real output, nothing written, nothing
committed:

| Option | Unique surface | Achieved utilisation | Outside tile | Density at 1024² |
|---|---|---|---|---|
| **Disjoint** (halves keep separate texels) | 1.902 m² | **66.7 %** | 0 | **607 px/m** ✅ |
| Mirrored (halves share texels) | 0.951 m² | 78.8 % | 0 | 932 px/m |

*(Blender island packer, `rotate=True, margin=0.003`; islands translated,
rotated and scaled as units — never split.)*

Both pack cleanly into the tile with nothing left outside. **Disjoint clears the
standard's 512 px/m skin target with 19 % headroom, and its 66.7 % utilisation
clears the `chart_min_utilisation` floor of 55 %.** So the density argument for
mirroring does not bite: mirroring buys ~1.54× more density that the standard
does not ask for, and it costs two things the project has already said it
wants:

- **Asymmetry becomes impossible.** Left and right share texels, so a scar, a
  tattoo, a sunburn or any per-side detail paints onto both sides at once.
- **Per-region texture-space masking breaks.** ADR 0008 §3.3 addresses regions
  independently; `ArmL` and `ArmR` sharing an island cannot be addressed
  separately, and the standard's `chart_islands_disjoint` rule forbids it
  outright.

```
SEAM: Body contract (UV chart) ⇄ textures standard
NEED: The source is a 24-tile mirrored UDIM layout, so a repack into the 0-1
      tile is mandatory before the body can be imported at all. The repack must
      choose whether the mirrored halves stay DISJOINT (each side its own
      texels) or are OVERLAPPED (halves share texels). Measured on a real pack,
      not estimated: disjoint reaches 66.7 % utilisation and 607 px/m at 1024²,
      already over the standard's 512 px/m skin target and over its 55 %
      utilisation floor — so the density argument for mirroring does not bite.
BREAKS: Mirroring would break asymmetric skin detail and per-region texture-space
      masking (ADR 0008 §3.3), and contradicts `chart_islands_disjoint` in
      `assets/standards/skin_texture.mgestd`. Disjoint costs nothing measurable.
PROPOSAL: Rule DISJOINT. I then repack algorithmically (Blender's island packer,
      seeded and deterministic), verify with `mge_uv_report --gate` before and
      after, and record the result. No island is placed by hand or by eye, and
      no new UV seam is cut — the source's existing seams and islands are moved
      and scaled, nothing is split, so vertex count and order are untouched by
      the repack itself (uv-audit D-A's "UV-only" case, not its topology case).
```

Until this is ruled I am not repacking, because the repack is the step that
bakes the answer in.

---

## 3. Corrections this forces elsewhere

These are other sessions' files, so they are listed, not edited:

1. **`docs/TASKS.md` Phase 13 preamble** states the UV corruption "was introduced
   by the retired modeler session's processing." Measured: it was not. The
   source was multi-tile; the importer's clamp is the whole mechanism. The
   ruling's *action* (discard v3, re-import fresh, hardened importer) is still
   right — only its *reason* needs correcting, and the discard is still correct
   because a body imported through a clamp cannot be repaired in place.
2. **`docs/research/uv-audit.md` D-A** asked the body session to "report the
   source unwrap's true `u`/`v` extent … it decides how much repacking is real
   work versus an affine transform." Answered above: **24 tiles, mirrored, no
   straddling faces** — so it is a per-island affine repack, the cheapest of the
   cases that document contemplated, and no new seams are needed.
3. **`docs/MODELING.md` §7** should record the bundle version actually measured
   (v1.4.1, already named there) and that the vendored source is UDIM, so the
   next session does not rediscover this.
4. **Task 13.6 has two owners.** ADR 0008 D-3 ends "`BodyRegion` extends to
   match — **wearables-session work**", but the Phase 13 sequencing note and my
   brief both put 13.6–13.9 on this session. Minor, but `BodyRegion` is a seam
   and AGENTS.md §10.3 requires any segmentation change to extend `CoverBits`
   in the same ruling — so it is worth saying once who cuts it, rather than
   both of us waiting for the other.

---

## 4. Incidental finding — the face geometry is there (B-8)

Checked while the source was open, because Job 3 asks whether a real
`BodyRegion::Face` shell is possible:

```
vertices in the top 25 cm of the model : 3587 of 10582 (33.9 %)
```

**A third of the source mesh's vertices are in the head.** The source carries
real facial geometry; the delivered v3 body's empty `Face` region is a product
of decimating 21 160 triangles down to a 2 200-triangle LOD0, not of the source
lacking a face. That makes B-8 / task 13.7 achievable — with the caveat that
the LOD0 triangle budget will have to be spent deliberately on the head rather
than uniformly, which is a budget question for the architect when I get there.

The source carries **no vertex groups and no materials**, so region tagging and
hem loops are ours to derive mechanically, as expected.

---

## 5. Status

- **Job 1 (harden the importer): done.** The import now refuses an
  unrepresentable chart with the measured extent, covered by
  `skin_import_refuses_uvs_outside_the_tile_instead_of_clamping` in
  `tests/test_skin_import.cpp`. 166 tests, 0 failed. **No task number covers
  this work** — the ruling lives in AGENTS.md §4 and the Phase 13 preamble says
  "through the hardened importer" without a line of its own. I have not
  self-assigned one; if the architect wants it tracked, please mint the number
  and I will fill the line in.
- **Job 2 (measure the source): done — reported above.**
- **Job 3 (import fresh): blocked on the §2 ruling.** Everything downstream
  (Jobs 4 and 5) sits behind it, because the repack decides the vertex data that
  the contract hash, the garment re-cut and all six `.mgefit` bakes are computed
  from. Re-doing that atomic event twice is exactly what the contract-version
  discipline exists to avoid.
