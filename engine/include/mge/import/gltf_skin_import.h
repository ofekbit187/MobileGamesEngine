#pragma once

// Skinned glTF import (task 8.12): brings an authored, rigged model into the
// engine's SkinnedMeshData. Host-side tooling — the runtime consumes the
// baked `.mgeskin` output, never glTF (ADR 0002/0005).
//
// The importer is strict about one thing: the model's joints must be the
// engine's canonical humanoid rig, matched BY NAME ("Hips", "Spine",
// "UpperArmL"…). A model rigged to anything else is rejected rather than
// silently mis-bound, because a wrong joint mapping looks like a modelling
// bug and is nearly impossible to spot in a render.

#include <string>

#include "mge/character/body_mesh.h"

namespace mge {

// Reads the first skinned mesh in the file. Weights are re-normalised and
// clamped to four influences; per-vertex body regions are derived from each
// vertex's dominant joint, and triangles are grouped into one MeshPart per
// region so masking stays a draw-range decision.
bool importSkinnedGltf(const char* path, SkinnedMeshData& out, std::string* error = nullptr);

}  // namespace mge
