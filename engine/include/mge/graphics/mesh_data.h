#pragma once

// CPU-side mesh data (load/import-time representation) and the v1 runtime
// mesh container. Heap use here is load-path only — the frame path touches
// GPU buffers, never this. See docs/adr/0002-runtime-mesh-format.md.

#include <cstdint>
#include <vector>

#include "mge/core/math.h"

namespace mge {

struct Vertex {
    Vec3 position;
    Vec3 normal;
};
static_assert(sizeof(Vertex) == 24, "vertex layout is part of the pipeline contract");

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
