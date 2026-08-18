#include "mge/core/math.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(vec3_basic_ops) {
    const Vec3 a{1, 2, 3};
    const Vec3 b{4, 5, 6};
    MGE_CHECK_NEAR((a + b).x, 5.0f, 1e-6);
    MGE_CHECK_NEAR(a.dot(b), 32.0f, 1e-6);
    const Vec3 c = a.cross(b);
    MGE_CHECK_NEAR(c.x, -3.0f, 1e-6);
    MGE_CHECK_NEAR(c.y, 6.0f, 1e-6);
    MGE_CHECK_NEAR(c.z, -3.0f, 1e-6);
    MGE_CHECK_NEAR((Vec3{3, 4, 0}.length()), 5.0f, 1e-6);
    MGE_CHECK_NEAR((Vec3{10, 0, 0}.normalized().x), 1.0f, 1e-6);
}

MGE_TEST(quat_rotation) {
    // 90 degrees around Y maps +X to -Z.
    const Quat q = Quat::fromAxisAngle({0, 1, 0}, kPi * 0.5f);
    const Vec3 r = q.rotate({1, 0, 0});
    MGE_CHECK_NEAR(r.x, 0.0f, 1e-5);
    MGE_CHECK_NEAR(r.z, -1.0f, 1e-5);

    // Composing two 90-degree rotations equals one 180-degree rotation.
    const Vec3 r2 = (q * q).rotate({1, 0, 0});
    MGE_CHECK_NEAR(r2.x, -1.0f, 1e-5);
    MGE_CHECK_NEAR(r2.z, 0.0f, 1e-5);
}

MGE_TEST(mat4_transform) {
    const Mat4 t = Mat4::translation({10, 20, 30});
    const Vec3 p = t.transformPoint({1, 2, 3});
    MGE_CHECK_NEAR(p.x, 11.0f, 1e-6);
    MGE_CHECK_NEAR(p.y, 22.0f, 1e-6);
    MGE_CHECK_NEAR(p.z, 33.0f, 1e-6);

    // Transform composition: scale then rotate then translate.
    Transform xf;
    xf.position = {5, 0, 0};
    xf.rotation = Quat::fromAxisAngle({0, 1, 0}, kPi * 0.5f);
    xf.scale = {2, 2, 2};
    const Vec3 q = xf.toMatrix().transformPoint({1, 0, 0});
    MGE_CHECK_NEAR(q.x, 5.0f, 1e-5);
    MGE_CHECK_NEAR(q.z, -2.0f, 1e-5);
}

MGE_TEST(aabb_queries) {
    Aabb box = Aabb::fromCenterExtents({0, 0, 0}, {1, 1, 1});
    MGE_CHECK(box.contains({0.5f, -0.5f, 0.9f}));
    MGE_CHECK(!box.contains({1.5f, 0, 0}));

    const Aabb other = Aabb::fromCenterExtents({1.5f, 0, 0}, {1, 1, 1});
    MGE_CHECK(box.intersects(other));
    const Aabb far_ = Aabb::fromCenterExtents({5, 0, 0}, {1, 1, 1});
    MGE_CHECK(!box.intersects(far_));

    box.extend({3, 0, 0});
    MGE_CHECK_NEAR(box.max.x, 3.0f, 1e-6);
}
