#include "mge/graphics/camera.h"
#include "mge/graphics/primitives.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(lookat_maps_target_forward) {
    const Mat4 view = Mat4::lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    // Right-handed view space looks down -Z: the target must land on -Z.
    const Vec3 t = view.transformPoint({0, 0, 0});
    MGE_CHECK_NEAR(t.x, 0.0f, 1e-5);
    MGE_CHECK_NEAR(t.y, 0.0f, 1e-5);
    MGE_CHECK_NEAR(t.z, -10.0f, 1e-5);
}

MGE_TEST(frustum_culls_correctly) {
    Camera camera;
    camera.eye = {0, 0, 10};
    camera.target = {0, 0, 0};
    const Frustum frustum = Frustum::fromViewProj(camera.viewProj());

    // In front of the camera: visible.
    MGE_CHECK(frustum.intersects(Aabb::fromCenterExtents({0, 0, 0}, {1, 1, 1})));
    // Behind the camera: culled.
    MGE_CHECK(!frustum.intersects(Aabb::fromCenterExtents({0, 0, 30}, {1, 1, 1})));
    // Far off to the side: culled.
    MGE_CHECK(!frustum.intersects(Aabb::fromCenterExtents({200, 0, 0}, {1, 1, 1})));
    // Beyond the far plane: culled.
    MGE_CHECK(!frustum.intersects(Aabb::fromCenterExtents({0, 0, -600}, {1, 1, 1})));
    // Straddling a side plane: still visible (conservative test).
    MGE_CHECK(frustum.intersects(Aabb::fromCenterExtents({6, 0, 0}, {4, 1, 1})));
}

MGE_TEST(lod_selection_by_distance) {
    const float switches[] = {10.0f, 30.0f};
    MGE_CHECK(selectLod(5.0f, switches, 3) == 0);
    MGE_CHECK(selectLod(15.0f, switches, 3) == 1);
    MGE_CHECK(selectLod(100.0f, switches, 3) == 2);
    MGE_CHECK(selectLod(5.0f, nullptr, 1) == 0);
    MGE_CHECK(selectLod(5.0f, nullptr, 0) == 0);
}

namespace {
bool meshIsSane(const MeshData& m) {
    if (m.vertices.empty() || m.indices.empty() || m.indices.size() % 3 != 0) return false;
    for (uint32_t index : m.indices) {
        if (index >= m.vertices.size()) return false;
    }
    for (const Vertex& v : m.vertices) {
        const float len = v.normal.length();
        if (len < 0.99f || len > 1.01f) return false;
    }
    return true;
}
}  // namespace

MGE_TEST(primitives_are_sane) {
    const MeshData box = makeBox({2, 4, 6});
    MGE_CHECK(meshIsSane(box));
    MGE_CHECK(box.vertices.size() == 24 && box.indices.size() == 36);
    MGE_CHECK_NEAR(box.bounds.min.x, -1.0f, 1e-6);
    MGE_CHECK_NEAR(box.bounds.max.y, 2.0f, 1e-6);
    MGE_CHECK_NEAR(box.bounds.max.z, 3.0f, 1e-6);

    const MeshData cylinder = makeCylinder(1.0f, 3.0f);
    MGE_CHECK(meshIsSane(cylinder));
    MGE_CHECK_NEAR(cylinder.bounds.max.y, 1.5f, 1e-5);
    MGE_CHECK_NEAR(cylinder.bounds.max.x, 1.0f, 1e-2);

    const MeshData capsule = makeCapsule(0.5f, 2.0f);
    MGE_CHECK(meshIsSane(capsule));
    MGE_CHECK_NEAR(capsule.bounds.max.y, 1.0f, 1e-4);
    MGE_CHECK_NEAR(capsule.bounds.min.y, -1.0f, 1e-4);

    const MeshData plane = makePlane(10, 10);
    MGE_CHECK(meshIsSane(plane));
    MGE_CHECK(plane.indices.size() == 6);
}
