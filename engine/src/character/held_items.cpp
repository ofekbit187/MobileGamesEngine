#include "mge/character/held_items.h"

#include <cmath>
#include <cstring>
#include <string>

#include "mge/character/body_mesh.h"
#include "mge/character/wearable_catalogue.h"
#include "mge/core/log.h"
#include "mge/graphics/mesh_io.h"

namespace mge {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Rotates a direction (no translation) — normals need this where positions
// need `transformPoint`.
Vec3 rotateOnly(const Mat4& m, const Vec3& v) {
    return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
}

}  // namespace

// ---------------------------------------------------------- attachment -----

const char* attachPointName(AttachPoint point) {
    switch (point) {
        case AttachPoint::HandR: return "hand_r";
        case AttachPoint::HandL: return "hand_l";
        case AttachPoint::Back: return "back";
        case AttachPoint::HipL: return "hip_l";
        case AttachPoint::HipR: return "hip_r";
        case AttachPoint::Count: break;
    }
    return "?";
}

bool attachPointFromName(const char* name, AttachPoint& out) {
    if (name == nullptr) return false;
    for (size_t i = 0; i < kAttachPointCount; ++i) {
        const AttachPoint point = static_cast<AttachPoint>(i);
        if (std::strcmp(name, attachPointName(point)) == 0) {
            out = point;
            return true;
        }
    }
    return false;
}

Joint attachPointJoint(AttachPoint point) {
    switch (point) {
        case AttachPoint::HandR: return Joint::HandR;
        case AttachPoint::HandL: return Joint::HandL;
        case AttachPoint::Back: return Joint::Chest;
        case AttachPoint::HipL:
        case AttachPoint::HipR: return Joint::Hips;
        case AttachPoint::Count: break;
    }
    return Joint::HandR;
}

Mat4 attachPointTransform(AttachPoint point, const HumanoidVariant& variant,
                          const Mat4 jointWorld[kJointCount]) {
    const Joint joint = attachPointJoint(point);
    const Mat4& world = jointWorld[static_cast<size_t>(joint)];

    // The offset from the joint to the socket, in joint-local space. Derived
    // from the rig and scaled with the character, so a broad or tall variant
    // gets its sockets moved for free — the sheath sits on THIS back, not on
    // the template's.
    const Skeleton skeleton = buildSkeleton(variant);
    const float heightScale = variant.height / templateVariant().height;
    const float width = variant.shoulderWidth / templateVariant().shoulderWidth;
    const float depth = variant.bulk * heightScale;

    Vec3 offset{0, 0, 0};
    Quat rotation{0, 0, 0, 1};
    switch (point) {
        case AttachPoint::HandR:
        case AttachPoint::HandL:
            // Inside the fist: a little along the bone, so a grip sits in the
            // palm rather than at the wrist pivot.
            offset = {0, -0.03f * heightScale, 0};
            break;
        // The carry sockets all INVERT the item. Grip space runs the blade
        // +Y away from the hand, so a sheath that only translated would carry
        // a sword hilt-down with its point above the head — measurably: the
        // tip reached y = 2.17 m on a 1.75 m character. 180 degrees plus the
        // cant is what a scabbard actually does.
        case AttachPoint::Back:
            // Over the shoulder blades: hilt at the shoulder, blade down the
            // back and tilted across it.
            offset = {0.04f * width, skeleton.torsoLength * 0.34f, 0.11f * depth};
            rotation = Quat::fromAxisAngle({0, 0, 1}, (180.0f + 18.0f) * kDegToRad);
            break;
        case AttachPoint::HipL:
            offset = {-0.13f * width, 0.02f * heightScale, 0.02f * depth};
            rotation = Quat::fromAxisAngle({0, 0, 1}, (180.0f - 22.0f) * kDegToRad);
            break;
        case AttachPoint::HipR:
            offset = {0.13f * width, 0.02f * heightScale, 0.02f * depth};
            rotation = Quat::fromAxisAngle({0, 0, 1}, (180.0f + 22.0f) * kDegToRad);
            break;
        case AttachPoint::Count: break;
    }
    return world * Mat4::translation(offset) * Mat4::rotation(rotation);
}

// ---------------------------------------------------------------- grip -----

const char* gripTypeName(GripType grip) {
    switch (grip) {
        case GripType::OneHanded: return "one_handed";
        case GripType::TwoHanded: return "two_handed";
        case GripType::Versatile: return "versatile";
        case GripType::Count: break;
    }
    return "?";
}

bool gripTypeFromName(const char* name, GripType& out) {
    if (name == nullptr) return false;
    for (size_t i = 0; i < static_cast<size_t>(GripType::Count); ++i) {
        const GripType grip = static_cast<GripType>(i);
        if (std::strcmp(name, gripTypeName(grip)) == 0) {
            out = grip;
            return true;
        }
    }
    return false;
}

Mat4 gripTransform(const HeldItemDef& item) {
    const Quat x = Quat::fromAxisAngle({1, 0, 0}, item.gripRotation.x * kDegToRad);
    const Quat y = Quat::fromAxisAngle({0, 1, 0}, item.gripRotation.y * kDegToRad);
    const Quat z = Quat::fromAxisAngle({0, 0, 1}, item.gripRotation.z * kDegToRad);
    return Mat4::translation(item.gripOffset) * Mat4::rotation(x) * Mat4::rotation(y) *
           Mat4::rotation(z);
}

// ------------------------------------------------------------ placing -----

bool isHeldItem(size_t catalogueIndex) {
    const WearableCatalogue& catalogue = wearableCatalogue();
    return catalogueIndex < catalogue.size() && catalogue.at(catalogueIndex).held;
}

const HeldItemDef* heldItemDef(size_t catalogueIndex) {
    if (!isHeldItem(catalogueIndex)) return nullptr;
    return &wearableCatalogue().at(catalogueIndex).heldItem;
}

const MeshData& sharedHeldItemMesh(size_t catalogueIndex) {
    // One copy per catalogue row, process-wide: a hundred guards with the same
    // sword cost one sword (P1).
    // Keyed by catalogue index, so it follows the catalogue's generation for
    // the same reason the garment caches do.
    static std::vector<MeshData> meshes;
    static uint32_t built = 0;
    const WearableCatalogue& catalogue = wearableCatalogue();
    if (built != catalogue.generation()) {
        built = catalogue.generation();
        meshes.assign(catalogue.size(), MeshData{});
        for (size_t i = 0; i < catalogue.size(); ++i) {
            const WearableDef& def = catalogue.at(i);
            if (!def.held || def.mesh.empty()) continue;
            const std::string path =
                std::string(characterAssetDir()) + "/" + def.mesh + ".mgemesh";
            LodMesh lod;
            if (!readMeshFile(path.c_str(), lod) || lod.lods.empty()) {
                MGE_LOGE("wearables", "held item %s: missing mesh %s", def.id.c_str(),
                         path.c_str());
                continue;
            }
            meshes[i] = lod.lods[0];
        }
    }
    static const MeshData empty;
    return catalogueIndex < meshes.size() ? meshes[catalogueIndex] : empty;
}

bool placeHeldItem(size_t catalogueIndex, bool sheathed, const HumanoidVariant& variant,
                   const Mat4 jointWorld[kJointCount], HeldItemPlacement& out) {
    const HeldItemDef* item = heldItemDef(catalogueIndex);
    if (item == nullptr) return false;

    const MeshData& mesh = sharedHeldItemMesh(catalogueIndex);
    if (mesh.vertices.empty()) return false;  // asset not installed

    // An item that cannot be sheathed stays in hand whatever the flag says —
    // a torch does not go on your back because combat ended.
    const bool onBody = sheathed && item->sheathable;
    const AttachPoint point = onBody ? item->sheathed : item->drawn;

    // The whole mechanism, in one line: joint → socket → grip → vertex.
    //
    // The grip transform applies ONLY in hand. It describes how the item sits
    // in a fist — a sword is nosed forward out of the grip rather than held
    // straight up the forearm — and that is meaningless on a back or a hip,
    // where the socket's own orientation is what carries the item. Applying it
    // to a sheathed sword laid it flat across the shoulder blades: 0.18 m of
    // vertical extent for an 0.83 m weapon.
    const Mat4 socket = attachPointTransform(point, variant, jointWorld);
    const Mat4 place = onBody ? socket : socket * gripTransform(*item);

    out.mesh.vertices.resize(mesh.vertices.size());
    out.mesh.indices = mesh.indices;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vertex& src = mesh.vertices[i];
        Vertex& dst = out.mesh.vertices[i];
        dst.position = place.transformPoint(src.position);
        const Vec3 n = rotateOnly(place, src.normal);
        dst.normal = n.lengthSq() > 1e-12f ? n.normalized() : Vec3{0, 1, 0};
        dst.uv[0] = src.uv[0];
        dst.uv[1] = src.uv[1];
    }
    out.mesh.computeBounds();

    const WearableDef& def = wearableCatalogue().at(catalogueIndex);
    for (int c = 0; c < 4; ++c) out.color[c] = def.color[c];
    return true;
}

}  // namespace mge
