# ADR 0010 — The body UV chart: repack to one tile, halves disjoint

**Status:** accepted (architect ruling) · **Date:** 2026-08-20
**Depends on:** ADR 0007 (template body), ADR 0008 (wearable fitting), `docs/TEXTURING.md`
**Evidence:** `docs/research/source-uv-measurement.md`, `docs/research/uv-audit.md`

## Context — and a correction to the record

The committed v3 body shipped with a UV chart outside the `[0,1]` tile, wasting ~90% of its
texture space. The working hypothesis — mine and the owner's — was that the pristine CC0
source had a clean single-tile chart which the retired modeler session **corrupted in
processing**. The owner ruled the processed assets discarded and ordered a fresh re-import
that would "preserve the source's UV chart verbatim".

**The pipeline session measured the source before touching it, and the hypothesis was
wrong.** The source is a clean, deliberate **24-tile mirrored UDIM layout** (u 0.039–8.949,
v 0.030–3.967), zero degenerate triangles, zero faces straddling a tile border, no clamp
fingerprint anywhere. There was never a single-tile chart to corrupt.

This changes the attribution but not the diagnosis. **The silent clamp is still the entire
mechanism of the damage** — the importer met a legitimate multi-tile layout it could not
represent, said nothing, and flattened it into committed content across three LODs and six
garments. The retired session's failure was shipping without measuring, not vandalism.

Two consequences follow immediately:

- "Re-import verbatim" is **impossible**. `SkinVertex` stores UVs as normalized uint16
  (B-3); a UDIM layout has no representation in it, and the newly hardened importer would
  now — correctly — refuse the source outright. **A repack into the tile is mandatory**, and
  it is a legitimate, measurable transformation rather than a taste-based one.
- The owner's discard ruling **stands, and is strengthened**: the repack has to happen
  regardless, so doing it once as a fresh import through the hardened importer is exactly
  right. Only its stated reason changes.

## Decision

**Repack the source's 24 UDIM tiles into the single `[0,1]` tile, with the body's left and
right halves DISJOINT — each side owning its own texels. Mirroring is rejected.**

The pipeline session was asked to recommend, and instead of arguing from arithmetic it
packed both layouts and measured them:

| Layout | Utilisation | Outside tile | Texel density @1024² |
|---|---|---|---|
| **Disjoint** (ruled) | 66.7% | 0 | **607 px/m** |
| Mirrored | 78.8% | 0 | 932 px/m |

## Why disjoint

**The only argument for mirroring is density, and the measurement removes it.** Disjoint
delivers 607 px/m against `TEXTURING.md`'s 512 px/m skin target — 19% headroom — and clears
the 55% utilisation floor. Mirroring buys texels we do not need.

**Against mirroring, three things we would never get back:**

1. **Asymmetry becomes impossible, permanently.** Shared texels mean a scar on one cheek, a
   birthmark on one arm, a burn, a brand, one blind eye, mud on the side you fell on —
   none of it can exist. Dictation 5 dictates that every person is *unique*, with
   hereditary features carried in DNA; asymmetry is one of the few visual axes that reads
   as individual rather than as a parameter slider. Choosing mirroring would quietly rule
   it out for the life of the project.
2. **It breaks per-side garment masking.** `docs/research/wearables.md` §3.3 keeps
   texture-space masking open as a future option for thin clothing. On a mirrored chart a
   mask is applied to both sides at once: a torn left sleeve tears the right sleeve too.
3. **It is the WoW-hair shape of mistake** (ADR 0008): cheap to adopt now, and effectively
   impossible to undo once textures exist, because every skin ever painted conforms to the
   chart. `B-27` already makes chart changes contract-version events requiring a re-bake of
   every wearable binding.

P12 decides it: pay once in the mechanism so content stays cheap **and unconstrained**
later. 19% density headroom is a fair price for never telling an artist "you cannot put it
on only one side".

## Consequences

- The repack proceeds under this ruling; the pipeline session is unblocked.
- The chart it produces becomes the frozen contract (`B-27`, `B-28`) — every skin texture,
  and any future texture-space masking, conforms to it.
- `docs/TEXTURING.md`'s density target is met with headroom rather than by pretending.
- Should a future dictation demand higher skin density, the answer is a larger texture or a
  smarter pack — **not** mirroring, which trades away expressiveness rather than memory.

## Also ruled here

**Task 13.6 had two owners.** ADR 0008 D-3 assigns the `BodyRegion` extension (elbow/knee
cut lines, ear sub-shell) to the wearables session; the Phase 13 sequencing note and the
pipeline session's brief both assign it to the pipeline. My error, caught by the pipeline
session rather than discovered as a collision. **Ruling: it belongs to the character asset
pipeline session** — the cut lines are geometry on the body it is producing, and splitting
"cut the shells" from "declare the regions" across two sessions would put a seam through
one indivisible change. ADR 0008 D-3's assignment is superseded on this point.

**The empty `Face` region is achievable.** 3,587 of the source's 10,582 vertices are in the
head, so `B-8`'s empty Face region is a decimation artifact, not a gap in the source —
task 13.7 proceeds as planned.
