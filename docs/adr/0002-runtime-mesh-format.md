# ADR 0002 — Runtime mesh format v1 (.mgemesh)

**Status:** accepted (task 2.4, v1)

## Decision

Meshes ship in a baked engine format; the runtime never parses interchange
formats (glTF stays in tooling — `mge_asset_import` bakes it).

**v1 layout** (implemented in `engine/src/graphics/mesh_io.cpp`):

```
FileHeader { magic "MGEM", version, lodCount, bounds[6] }
per LOD:
  LodHeader { vertexCount, indexCount, switchDistance, bounds[6] }
  Vertex[vertexCount]   // float3 position + float3 normal (24 B)
  uint32[indexCount]
```

- LODs are ordered finest→coarsest with ascending switch distances, matching
  `selectLod` and the streaming system's LOD-granular residency (P2).
- Bounds are stored per LOD and per file so culling and streaming never need
  the vertex data to make decisions.

## v2 roadmap (P1-driven, deliberately deferred)

v1 optimizes for correctness and a working end-to-end path. The compaction
pass that makes this format final is scheduled with texture streaming:

- Quantized vertex attributes (16-bit positions in mesh-local space,
  oct-encoded normals) — ~2.5× smaller vertices
- 16-bit indices where vertex count allows
- Section alignment for direct mmap/DMA-friendly loads without parsing
- Generated LOD chains at import time (mesh simplification)
- Material/texture references once the material system carries them

Version field gates the migration; readers reject unknown versions.
