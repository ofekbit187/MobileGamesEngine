#pragma once

// Runtime mesh container I/O (task 2.4, v1). The .mgemesh file is the baked,
// streaming-ready form every mesh ships in — written by the import tool,
// read by the runtime. See docs/adr/0002-runtime-mesh-format.md for layout
// and the v2 roadmap (quantization, in-place mapping).

#include <cstdint>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/graphics/mesh_data.h"

namespace mge {

// In-memory serialization — the one layout shared by .mgemesh files and the
// world container's embedded asset payloads (ADR 0002/0003).
void serializeMesh(const LodMesh& mesh, std::vector<uint8_t>& out);
bool deserializeMesh(const uint8_t* data, size_t size, LodMesh& out);

bool writeMeshFile(const char* path, const LodMesh& mesh);
bool readMeshFile(const char* path, LodMesh& out);

// Skinned runtime asset (`.mgeskin`, task 8.12): the baked form of an
// authored, rigged model — vertices with their four bone influences, the
// index buffer grouped by body region, and the region table. Written by the
// import tool, read by the runtime; glTF never reaches a device (ADR 0005).
void serializeSkinnedMesh(const SkinnedMeshData& mesh, std::vector<uint8_t>& out);
bool deserializeSkinnedMesh(const uint8_t* data, size_t size, SkinnedMeshData& out);

bool writeSkinnedMeshFile(const char* path, const SkinnedMeshData& mesh);
bool readSkinnedMeshFile(const char* path, SkinnedMeshData& out);

}  // namespace mge
