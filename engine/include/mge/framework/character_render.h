#pragma once

// The per-frame character render composition, made portable so the P1 gate
// can see it (task 19.7).
//
// WHY THIS FILE EXISTS. All of this used to live inside
// `app/src/main/cpp/device_game.cpp`. No host tool compiles that file, and
// the allocation gate is `tools/host_runner` — so the frame path of the
// SHIPPED APP was the one frame path in this engine the gate never looked at.
// That is not a hypothetical: a `std::vector` built inside the frame function
// heap-allocated on every frame of every APK we shipped, and survived because
// the thing that would have caught it does not compile the file it was in
// (19.6). Reporting `steady-state heap allocations: 0` while that was true
// made the number mean less than it appeared to.
//
// So the work moves here, where `host_runner` runs it inside the counter.
//
// THE VULKAN CONSTRAINT, which shapes the whole design. `mge_core` builds for
// arm64 under QEMU with no graphics library at all — the emulated tier is
// deliberately Vulkan-free — and the gate runs on that tier too. Nothing here
// may name a GPU type. Hence the split:
//
//   * `composeCharacterFrame` is an ordinary function: pose, palette and
//     joint transforms involve no GPU types at all, so the majority of the
//     per-frame work is shared outright.
//   * the draw-list emission is a TEMPLATE on the item type. The device
//     instantiates it on the renderer's real `SkinnedDrawItem`/`DrawItem`;
//     the host runner instantiates it on stand-ins of the same shape. Same
//     source, both sides — a template is what lets the gate compile the
//     device's loops without compiling Vulkan.
//
// What the gate therefore covers, and what it does not: it covers this code,
// which is the per-frame work and every container it grows. It does not cover
// the Vulkan submission that follows, which allocates nothing on our side.

#include <cstddef>
#include <cstring>

#include "mge/character/animation.h"
#include "mge/character/body_mesh.h"
#include "mge/character/held_items.h"
#include "mge/character/humanoid.h"
#include "mge/character/use_archetypes.h"
#include "mge/core/math.h"
#include "mge/framework/world.h"

namespace mge {

// Everything one character contributes to a frame that does not depend on a
// GPU type: where it stands, how it is posed, and — only when it is carrying
// something — the joint transforms a held item rides.
struct CharacterFrame {
    Pose pose;                     // locomotion with any use motion layered in
    Mat4 model;                    // character-to-world
    Aabb bounds;                   // what the renderer culls against
    Vec3 lodReference{};           // where it is, for LOD selection
    Mat4 jointWorld[kJointCount];  // valid only when jointWorldValid
    bool jointWorldValid = false;
};

// The animation half of a character's frame, in the order the device build
// runs it:
//
//   1. interpolate the transform between simulation steps (fixed-step sim,
//      smooth render — the alpha is the engine's render alpha)
//   2. sample locomotion
//   3. layer the use archetype OVER it when one is playing, under the
//      archetype's own mask, so the legs keep walking through a swing and a
//      one-handed grip leaves the off arm to the walk (14.1)
//   4. build the skinning palette from the result
//   5. when the character carries something, evaluate the joint transforms —
//      from the SAME skeleton the palette came from, or a held item lands
//      where a template-proportioned hand would be rather than this
//      character's
//
// Allocation-free: every output is caller-owned storage and `LayeredPose` is
// a stack value. That property is the entire point of this file, and
// `host_runner` is what proves it rather than this comment.
void composeCharacterFrame(const TransformComponent& transform, float alpha,
                           const HumanoidVariant& variant, LocomotionAnimator& anim,
                           const UsePlayer& use, const UseMotion& motion,
                           const JointMask& useMask, bool needJointWorld,
                           Mat4 outPalette[kJointCount], CharacterFrame& out);

// ------------------------------------------------------- draw emission -----
//
// Templates, for the Vulkan reason at the top of this file. Each takes a
// prototype item already carrying the shared per-character state (mesh,
// palette, model, bounds, colour) and varies only what a row must vary.

// The body: one draw range per VISIBLE region. Garment masking is a draw
// range here rather than a second mesh — a covered region is simply not
// submitted (CHARACTERS.md §6).
template <class SkinnedItem, class List>
void emitBodyParts(const SkinnedItem& proto, const MeshPart* parts, size_t partCount,
                   uint32_t visibleRegions, List& out) {
    for (size_t i = 0; i < partCount; ++i) {
        if ((visibleRegions & regionBit(parts[i].region)) == 0) continue;
        SkinnedItem item = proto;
        item.firstIndex = parts[i].firstIndex;
        item.indexCount = parts[i].indexCount;
        out.push_back(item);
    }
}

// Garments: whole-mesh draws sharing the body's palette and transform, which
// is what makes a garment deform with the skin for free. `Garment` needs a
// `mesh` assignable to the item's and a `color[4]`.
template <class SkinnedItem, class Garment, class List>
void emitGarments(const SkinnedItem& proto, const Garment* garments, size_t count, List& out) {
    for (size_t i = 0; i < count; ++i) {
        SkinnedItem worn = proto;
        worn.mesh = garments[i].mesh;
        worn.firstIndex = 0;
        worn.indexCount = 0;  // the whole garment
        std::memcpy(worn.baseColor, garments[i].color, sizeof(worn.baseColor));
        out.push_back(worn);
    }
}

// Held items: rigid props on rig-derived sockets, so one matrix each and no
// geometry work at all. Deliberately NOT `placeHeldItem`, which bakes the
// item's geometry per call — right for an offline preview, an allocation on
// the frame path here.
//
// `Held` needs `mesh` (assignable to the item's, and non-null once uploaded),
// `anchor`, `def` and `color[4]`. Requires `frame.jointWorldValid`; a caller
// that passes characters carrying things without asking for joint transforms
// gets nothing emitted rather than wrong placement.
template <class DrawItemT, class Held, class List>
void emitHeldItems(const CharacterFrame& frame, const HumanoidVariant& variant,
                   const Held* held, size_t count, List& out) {
    if (!frame.jointWorldValid) return;
    for (size_t i = 0; i < count; ++i) {
        if (held[i].mesh == nullptr) continue;
        DrawItemT prop;
        prop.mesh = held[i].mesh;
        prop.model = frame.model *
                     attachPointTransform(held[i].anchor, variant, frame.jointWorld) *
                     gripTransform(held[i].def);
        prop.worldBounds = frame.bounds;  // the character's, good enough to cull with
        prop.lodReference = frame.lodReference;
        std::memcpy(prop.baseColor, held[i].color, sizeof(prop.baseColor));
        out.push_back(prop);
    }
}

// --------------------------------------------------- world renderables -----

// What the caller's asset lookup hands back about one resident mesh. The
// bounds are the mesh's OWN, in model space; the template turns them into the
// world-space sphere the renderer culls against.
struct ResolvedRenderable {
    Vec3 boundsCenter{};
    float boundsRadius = 0.0f;
};

// The static half of the draw list: every renderable entity in the world,
// interpolated between simulation steps and pushed as one item.
//
// `resolve(assetId, item, ResolvedRenderable&) -> bool` is the caller's, and
// is where every GPU-typed decision lives — which mesh handle, which material,
// what placeholder parameters. It returns false for an asset that is not
// resident, which is the ordinary "still streaming" answer rather than an
// error. Everything the template itself does — the lerp, the model matrix,
// the culling bounds, the colour copy, the push — is type-free, which is what
// puts it under the gate.
template <class DrawItemT, class List, class Resolve>
void emitWorldRenderables(const World& world, float alpha, List& out, Resolve resolve) {
    world.forEachRenderable(
        [&](EntityId, const TransformComponent& t, const ModelComponent& m) {
            DrawItemT item;
            ResolvedRenderable mesh;
            if (!resolve(m.asset, item, mesh)) return;
            const Vec3 pos = lerp(t.prevPosition, t.position, alpha);
            Transform xf;
            xf.position = pos;
            xf.rotation = Quat::fromAxisAngle({0, 1, 0}, t.yaw);
            item.model = xf.toMatrix();
            item.worldBounds = Aabb::fromCenterExtents(
                pos + mesh.boundsCenter,
                {mesh.boundsRadius, mesh.boundsRadius, mesh.boundsRadius});
            item.lodReference = pos;
            std::memcpy(item.baseColor, m.color, sizeof(item.baseColor));
            out.push_back(item);
        });
}

}  // namespace mge
