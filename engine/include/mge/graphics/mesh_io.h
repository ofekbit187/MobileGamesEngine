#pragma once

// Runtime mesh container I/O (task 2.4, v1). The .mgemesh file is the baked,
// streaming-ready form every mesh ships in — written by the import tool,
// read by the runtime. See docs/adr/0002-runtime-mesh-format.md for layout
// and the v2 roadmap (quantization, in-place mapping).

#include "mge/graphics/mesh_data.h"

namespace mge {

bool writeMeshFile(const char* path, const LodMesh& mesh);
bool readMeshFile(const char* path, LodMesh& out);

}  // namespace mge
