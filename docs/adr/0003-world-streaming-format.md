# ADR 0003 — World streaming container v1 (.mgeworld)

**Status:** accepted (task 4.1 design; realizes P2 and P10)

## Decision

A shipped world is one seek-friendly container. Nothing in it is ever parsed
wholesale: the runtime reads the small index tables at open, then fetches
exactly the byte ranges it needs with priority-ordered async reads.

```
FileHeader   { magic "MGEW", version, chunkSizeMeters,
               assetCount, chunkCount,
               assetTableOffset, nameTableOffset/Size, chunkTableOffset }
AssetTable   [assetCount]  — fixed-size records:
               { assetId, kind (mesh|virtual), nameOffset/len,
                 payloadOffset, payloadSize,           // mesh blob (may be 0)
                 proportions[3], shape,                 // virtual models
                 descOffset/len }                       // description text
NameTable    — string bytes referenced by the asset table
ChunkTable   [chunkCount] — fixed-size records, sorted by (cx, cz):
               { cx, cz, cellKind (exterior|interior),
                 anchor[3], enterRadius, exitRadius,    // interior cells (P10)
                 placementsOffset, placementCount }
Payloads     — placement arrays + serialized mesh blobs
Placement    { assetId, pos[3], yaw, color[4] }         // 40 B, plain array
```

Key properties:

- **Partial reads only** (P2): index tables total a few hundred KB even for
  tens of thousands of chunks; each chunk's placements are one contiguous
  `pread` at a known offset, each asset payload another. Both go through
  the priority AsyncIO system — what the player approaches loads first.
- **Shared assets by id** (P1): chunks reference assets by stable AssetId;
  payloads are stored once. Asset residency is refcounted across chunks.
- **Interior cells are chunks** (P10): an interior is a chunk-table entry
  whose residency is driven by *approach prediction* against its door anchor
  (enter/exit radii), not by grid distance — entering a building is the same
  machinery as walking into a new region, so it can never be a loading
  screen.
- **Virtual models ship in the world** (P5): a virtual asset entry carries
  proportions/shape/description and no payload; fulfilling one later means
  writing a payload for the same id (world rebake or patch pack), with zero
  changes to any placement.
- Mesh blobs use the same serialized layout as `.mgemesh` (ADR 0002), so the
  import pipeline and the world pipeline share one code path, and the format
  inherits the v2 quantization roadmap unchanged.

## Deferred to v2 (with format version gates)

- Per-LOD payload ranges so distant chunks fetch only coarse LODs
  (task 4.4's full form); v1 fetches whole assets
- Compression (per-section LZ4) and section alignment for direct DMA
- Entity gameplay-state records beyond placements (inventory seeds, AI
  profiles) — arrive with Phases 6/8
- Patch packs: overlay containers fulfilling virtual assets post-ship
