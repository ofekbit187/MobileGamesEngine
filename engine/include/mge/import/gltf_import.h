#pragma once

// Native glTF 2.0 import (task 2.5, v1): parses a .gltf/.glb, applies node
// world transforms, and merges all mesh primitives into one LodMesh (single
// LOD — generated LOD chains are a later import step). Host-side tooling;
// the runtime consumes the baked .mgemesh output, never glTF directly.

#include <string>

#include "mge/graphics/mesh_data.h"

namespace mge {

bool importGltf(const char* path, LodMesh& out, std::string* error = nullptr);

}  // namespace mge
