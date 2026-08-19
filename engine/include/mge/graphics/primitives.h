#pragma once

// Procedural primitives. Two jobs: simple real geometry for early scenes,
// and the placeholder volumes for virtual models (P5, task 2.8) — a virtual
// model's shape hint (box / cylinder / capsule) maps to exactly these, at the
// declared proportions.

#include "mge/graphics/mesh_data.h"

namespace mge {

// Axis-aligned box centered at origin, size = full extents (w, h, d).
MeshData makeBox(const Vec3& extents);

// Y-axis cylinder centered at origin: total height, radius.
MeshData makeCylinder(float radius, float height, int segments = 24);

// Y-axis capsule centered at origin: total height including caps.
MeshData makeCapsule(float radius, float height, int segments = 24, int rings = 8);

// Flat XZ plane centered at origin, normal +Y.
MeshData makePlane(float width, float depth);

}  // namespace mge
