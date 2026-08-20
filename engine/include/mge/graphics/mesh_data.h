#pragma once

// CPU-side mesh data (load/import-time representation) and the v1 runtime
// mesh container. Heap use here is load-path only — the frame path touches
// GPU buffers, never this. See docs/adr/0002-runtime-mesh-format.md.

#include <cstdint>
#include <vector>

#include "mge/core/math.h"

namespace mge {

// 32 bytes: position, normal, and a texture coordinate.
//
// UV is float, not the normalized uint16 the skinned vertex uses (B-3), for
// one reason: static world geometry TILES. A four-metre plank wall wants
// u = 0..4 so one authored 512² material repeats across it, and a chart
// clamped to the unit tile cannot say that — which is exactly the defect
// docs/research/uv-audit.md found on the body. Quantizing this stream is
// `.mgemesh` v2 work (task 2.4) and must keep tiling representable.
//
// The convention for engine-generated geometry is METRES: one unit of UV is
// one metre of surface, so texel density is uniform across every primitive
// without the caller thinking about it, and a material scales the rate it
// wants (docs/TEXTURING.md §5.1).
struct Vertex {
    Vec3 position;
    Vec3 normal;
    float uv[2] = {0, 0};
};
static_assert(sizeof(Vertex) == 32, "vertex layout is part of the pipeline contract");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Aabb bounds{};

    void computeBounds() {
        if (vertices.empty()) {
            bounds = Aabb{};
            return;
        }
        bounds.min = bounds.max = vertices[0].position;
        for (const Vertex& v : vertices) bounds.extend(v.position);
    }
};

// A mesh with its LOD chain: lods[0] is finest; switchDistances is ascending
// (see selectLod in camera.h). v1 chains may hold a single LOD.
struct LodMesh {
    std::vector<MeshData> lods;
    std::vector<float> switchDistances;
    Aabb bounds{};  // union over LODs

    void computeBounds() {
        bounds = Aabb{};
        bool first = true;
        for (auto& lod : lods) {
            lod.computeBounds();
            if (first) {
                bounds = lod.bounds;
                first = false;
            } else {
                bounds.extend(lod.bounds.min);
                bounds.extend(lod.bounds.max);
            }
        }
    }
};

}  // namespace mge
