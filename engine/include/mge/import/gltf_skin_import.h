#pragma once

// Skinned glTF import (task 8.12): brings an authored, rigged model into the
// engine's SkinnedMeshData. Host-side tooling — the runtime consumes the
// baked `.mgeskin` output, never glTF (ADR 0002/0005).
//
// The importer refuses what it cannot represent. Two things are hard limits
// rather than preferences, and both fail the import with a measured reason:
//
//   * The rig. The model's joints must be the engine's canonical humanoid rig,
//     matched BY NAME ("Hips", "Spine", "UpperArmL"...). A model rigged to
//     anything else is rejected rather than silently mis-bound, because a
//     wrong joint mapping looks like a modelling bug and is nearly impossible
//     to spot in a render.
//
//   * The UV tile. `SkinVertex` stores UVs as normalized uint16 (B-3), so a
//     coordinate outside [0,1] has no representation. The import fails with
//     the chart's measured u/v extent and the count of vertices outside it.
//     It does NOT clamp: clamping is what silently destroyed 89.7 % of the
//     template body's UV area across three LODs and six committed garments
//     before anyone measured it (docs/research/uv-audit.md, AGENTS.md 4).

#include <string>

#include "mge/character/body_mesh.h"

namespace mge {

// Reads the first skinned mesh in the file. Weights are re-normalised and
// clamped to four influences; per-vertex body regions are derived from each
// vertex's dominant joint, and triangles are grouped into one MeshPart per
// region so masking stays a draw-range decision.
bool importSkinnedGltf(const char* path, SkinnedMeshData& out, std::string* error = nullptr);

}  // namespace mge
