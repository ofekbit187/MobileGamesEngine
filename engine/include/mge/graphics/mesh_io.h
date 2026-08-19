#pragma once

// Runtime mesh container I/O (task 2.4, v1). The .mgemesh file is the baked,
// streaming-ready form every mesh ships in — written by the import tool,
// read by the runtime. See docs/adr/0002-runtime-mesh-format.md for layout
// and the v2 roadmap (quantization, in-place mapping).

#include <cstdint>
#include <vector>

#include "mge/graphics/mesh_data.h"

namespace mge {

// In-memory serialization — the one layout shared by .mgemesh files and the
// world container's embedded asset payloads (ADR 0002/0003).
void serializeMesh(const LodMesh& mesh, std::vector<uint8_t>& out);
bool deserializeMesh(const uint8_t* data, size_t size, LodMesh& out);

bool writeMeshFile(const char* path, const LodMesh& mesh);
bool readMeshFile(const char* path, LodMesh& out);

}  // namespace mge
