#include "mge/character/body_mesh.h"

#include <cmath>
#include <cstring>
#include <string>

#include "mge/core/log.h"
#include "mge/graphics/mesh_io.h"

// The humanoid template body is CONTENT, not code (P5, CHARACTERS.md §4).
// It is Blender Studio's CC0 human base mesh, placed, rigged to the canonical
// 17-joint skeleton and reduced to three LODs by
// tools/model/humanoid_template.py, then baked to `.mgeskin` by the import
// tool. The garments are cut out of that same body's surface, so they fit it
// by construction. This file is what the engine does with it:
//
//   * loads the LOD chain and the garments once, shared by every character
//   * turns a variant into a skinning palette, so proportions cost matrices
//     rather than geometry (ADR 0005)
//   * masks covered body regions when a character is dressed
//
// Everything here is load-time work; the frame path sees GPU buffers and a
// 17-joint palette per character.

namespace mge {

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

#ifndef MGE_CHARACTER_ASSET_DIR
#define MGE_CHARACTER_ASSET_DIR "assets/models"
#endif

std::string& assetDir() {
    static std::string dir = MGE_CHARACTER_ASSET_DIR;
    return dir;
}

bool loadAsset(const char* name, SkinnedMeshData& out) {
    const std::string path = assetDir() + "/" + name + ".mgeskin";
    if (!readSkinnedMeshFile(path.c_str(), out)) {
        MGE_LOGE("body", "missing character asset: %s", path.c_str());
        out.clear();
        return false;
    }
    return true;
}

const char* garmentAsset(WearableKind kind) {
    switch (kind) {
        case WearableKind::Tunic: return "garment_tunic";
        case WearableKind::Armor: return "garment_armour";
        case WearableKind::Pants: return "garment_trousers";
        case WearableKind::Boots: return "garment_boots";
        case WearableKind::HairShort: return "garment_hair_short";
        case WearableKind::HairLong: return "garment_hair_long";
        case WearableKind::Sword: return nullptr;  // held items are rigid, not fitted
    }
    return nullptr;
}

// Copies `src` into `out`, keeping only the parts whose region survives the
// mask. Masking is a triangle-range decision on the shared vertex buffer:
// a dressed character draws FEWER triangles than a bare one, and pays no
// extra vertices (CHARACTERS.md §5.4).
void copyMasked(const SkinnedMeshData& src, uint32_t regions, SkinnedMeshData& out) {
    out.clear();
    out.vertices = src.vertices;
    if (regions == kAllRegions) {
        out.indices = src.indices;
        out.parts = src.parts;
    } else {
        for (const MeshPart& part : src.parts) {
            if ((regions & regionBit(part.region)) == 0) continue;
            MeshPart kept;
            kept.region = part.region;
            kept.firstIndex = static_cast<uint32_t>(out.indices.size());
            kept.indexCount = part.indexCount;
            out.indices.insert(out.indices.end(), src.indices.begin() + part.firstIndex,
                               src.indices.begin() + part.firstIndex + part.indexCount);
            out.parts.push_back(kept);
        }
    }
    out.bounds = src.bounds;
}

}  // namespace

// ------------------------------------------------------------- assets ------

void setCharacterAssetDir(const char* dir) {
    if (dir != nullptr) assetDir() = dir;
}

const char* characterAssetDir() { return assetDir().c_str(); }

Joint mirrored(Joint j) {
    switch (j) {
        case Joint::UpperArmL: return Joint::UpperArmR;
        case Joint::ForearmL: return Joint::ForearmR;
        case Joint::HandL: return Joint::HandR;
        case Joint::ThighL: return Joint::ThighR;
        case Joint::ShinL: return Joint::ShinR;
        case Joint::FootL: return Joint::FootR;
        default: return j;
    }
}

void SkinnedMeshData::computeBounds() {
    if (vertices.empty()) {
        bounds = Aabb{};
        return;
    }
    bounds.min = bounds.max = vertices[0].position;
    for (const SkinVertex& v : vertices) bounds.extend(v.position);
}

const HumanoidVariant& templateVariant() {
    static const HumanoidVariant reference{};
    return reference;
}

const std::vector<SkinnedMeshData>& sharedTemplateLods() {
    static const std::vector<SkinnedMeshData> lods = [] {
        std::vector<SkinnedMeshData> loaded(kBodyLodCount);
        for (size_t i = 0; i < kBodyLodCount; ++i) {
            char name[64];
            std::snprintf(name, sizeof name, "humanoid_template_lod%zu", i);
            loadAsset(name, loaded[i]);
        }
        return loaded;
    }();
    return lods;
}

const SkinnedMeshData& sharedGarment(WearableKind kind) {
    static const std::vector<SkinnedMeshData> garments = [] {
        std::vector<SkinnedMeshData> loaded(8);
        for (size_t k = 0; k < loaded.size(); ++k) {
            const char* name = garmentAsset(static_cast<WearableKind>(k));
            if (name != nullptr) loadAsset(name, loaded[k]);
        }
        return loaded;
    }();
    const size_t k = static_cast<size_t>(kind);
    return garments[k < garments.size() ? k : 0];
}

void buildTemplateBody(const BodyBuildDesc& desc, SkinnedMeshData& out) {
    const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
    const size_t level = static_cast<size_t>(desc.lod) % lods.size();
    copyMasked(lods[level], desc.regions, out);
}

void buildTemplateBodyLods(uint32_t regions, std::vector<SkinnedMeshData>& out) {
    out.clear();
    out.resize(kBodyLodCount);
    for (size_t i = 0; i < kBodyLodCount; ++i) {
        BodyBuildDesc desc;
        desc.lod = static_cast<BodyLod>(i);
        desc.regions = regions;
        buildTemplateBody(desc, out[i]);
    }
}

void buildGarmentMesh(const GarmentBuildDesc& desc, SkinnedMeshData& out) {
    copyMasked(sharedGarment(desc.kind), kAllRegions, out);
}

uint32_t garmentCoverage(WearableKind kind) {
    switch (kind) {
        case WearableKind::Tunic:
        case WearableKind::Armor: return regionBit(BodyRegion::Torso);
        case WearableKind::Pants: return kRegionsLegs;
        case WearableKind::Boots: return kRegionsFeet;
        case WearableKind::HairShort:
        case WearableKind::HairLong:
        case WearableKind::Sword: return 0;
    }
    return 0;
}

// ------------------------------------------------- variants via the rig ----

void templateBindPositions(Vec3 out[kJointCount]) {
    const Skeleton skeleton = buildSkeleton(templateVariant());
    for (size_t j = 0; j < kJointCount; ++j) {
        const int8_t parent = skeleton.parent[j];
        out[j] = parent < 0 ? skeleton.bindOffset[j]
                            : out[static_cast<size_t>(parent)] + skeleton.bindOffset[j];
    }
}

namespace {

// The bone's own frame: +Y along the bone, the other two axes across it.
//
// Variant scaling is expressed in THIS frame, not in character axes. The
// template's bind pose is the base mesh's own relaxed stance, so no limb is
// axis-aligned — the arms hang ~21 degrees out and the legs splay in towards
// the ankles. Scaling in character axes would therefore make a bulky
// character's arms LONGER instead of thicker. In the bone frame, `bulk`
// always means across the bone and a length ratio always means along it.
Mat4 boneFrame(const Vec3 bind[kJointCount], const int8_t parent[kJointCount],
               const int8_t child[kJointCount], size_t j) {
    Vec3 axis{0, 1, 0};
    if (child[j] >= 0) {
        axis = bind[static_cast<size_t>(child[j])] - bind[j];
    } else if (parent[j] >= 0) {
        axis = bind[j] - bind[static_cast<size_t>(parent[j])];
    }
    if (axis.lengthSq() < 1e-12f) axis = Vec3{0, 1, 0};
    const Vec3 y = axis.normalized();
    // Helper is +Z, so a vertical bone yields exactly the identity and the
    // trunk keeps its "x is across the shoulders" meaning.
    Vec3 x = y.cross(Vec3{0, 0, 1});
    if (x.lengthSq() < 1e-6f) x = y.cross(Vec3{1, 0, 0});
    x = x.normalized();
    const Vec3 z = x.cross(y);
    Mat4 m = Mat4::identity();
    m.m[0] = x.x;  m.m[1] = x.y;  m.m[2] = x.z;
    m.m[4] = y.x;  m.m[5] = y.y;  m.m[6] = y.z;
    m.m[8] = z.x;  m.m[9] = z.y;  m.m[10] = z.z;
    return m;
}

Mat4 transposeRotation(const Mat4& m) {
    Mat4 t = Mat4::identity();
    t.m[0] = m.m[0];  t.m[1] = m.m[4];  t.m[2] = m.m[8];
    t.m[4] = m.m[1];  t.m[5] = m.m[5];  t.m[6] = m.m[9];
    t.m[8] = m.m[2];  t.m[9] = m.m[6];  t.m[10] = m.m[10];
    return t;
}

}  // namespace

void buildSkinPalette(const HumanoidVariant& variant, const Pose& pose,
                      Mat4 outPalette[kJointCount]) {
    const Skeleton tmpl = buildSkeleton(templateVariant());
    const Skeleton skeleton = buildSkeleton(variant);
    Vec3 bind[kJointCount];
    templateBindPositions(bind);

    Mat4 world[kJointCount];
    evaluatePose(skeleton, pose, world);

    int8_t firstChild[kJointCount];
    for (size_t j = 0; j < kJointCount; ++j) firstChild[j] = -1;
    for (size_t j = 0; j < kJointCount; ++j) {
        const int8_t parent = tmpl.parent[j];
        if (parent >= 0 && firstChild[static_cast<size_t>(parent)] < 0) {
            firstChild[static_cast<size_t>(parent)] = static_cast<int8_t>(j);
        }
    }
    Mat4 frames[kJointCount];
    for (size_t j = 0; j < kJointCount; ++j) {
        frames[j] = boneFrame(bind, tmpl.parent, firstChild, j);
    }

    const float heightScale = variant.height / templateVariant().height;
    const float thickness = variant.bulk * heightScale;
    const float shoulderScale = variant.shoulderWidth / templateVariant().shoulderWidth;
    const float hipScale = variant.hipWidth / templateVariant().hipWidth;
    const auto ratio = [](float a, float b) { return b > 1e-6f ? a / b : 1.0f; };

    for (size_t j = 0; j < kJointCount; ++j) {
        // Along-bone stretch (the bone this joint drives) and cross-section
        // scale (how thick the body is around it), applied in joint-local
        // space so it never leaks into child bones.
        Vec3 s{thickness, heightScale, thickness};
        switch (static_cast<Joint>(j)) {
            case Joint::Hips:
                s = {hipScale, heightScale, thickness};
                break;
            case Joint::Spine:
                s = {(hipScale + shoulderScale) * 0.5f,
                     ratio(skeleton.torsoLength, tmpl.torsoLength), thickness};
                break;
            case Joint::Chest:
                s = {shoulderScale, ratio(skeleton.torsoLength, tmpl.torsoLength), thickness};
                break;
            case Joint::Neck:
                s = {thickness, ratio(skeleton.torsoLength, tmpl.torsoLength), thickness};
                break;
            case Joint::Head: {
                const float h = ratio(skeleton.headSize, tmpl.headSize);
                s = {h, h, h};
                break;
            }
            case Joint::UpperArmL:
            case Joint::UpperArmR:
                s = {thickness, ratio(skeleton.upperArmLength, tmpl.upperArmLength), thickness};
                break;
            case Joint::ForearmL:
            case Joint::ForearmR:
                s = {thickness, ratio(skeleton.forearmLength, tmpl.forearmLength), thickness};
                break;
            case Joint::HandL:
            case Joint::HandR:
                s = {thickness, heightScale, thickness};
                break;
            case Joint::ThighL:
            case Joint::ThighR:
                s = {thickness, ratio(skeleton.thighLength, tmpl.thighLength), thickness};
                break;
            case Joint::ShinL:
            case Joint::ShinR:
                s = {thickness, ratio(skeleton.shinLength, tmpl.shinLength), thickness};
                break;
            case Joint::FootL:
            case Joint::FootR:
                s = {heightScale, heightScale, heightScale};
                break;
            default:
                break;
        }
        const Mat4& frame = frames[j];
        outPalette[j] =
            world[j] * frame * Mat4::scale(s) * transposeRotation(frame) *
            Mat4::translation(-bind[j]);
    }
}

void skinMesh(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount], MeshData& out) {
    out.vertices.resize(mesh.vertices.size());
    out.indices = mesh.indices;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const SkinVertex& sv = mesh.vertices[i];
        Vec3 position{0, 0, 0};
        Vec3 normal{0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            const float w = static_cast<float>(sv.weights[k]) * (1.0f / 255.0f);
            if (w <= 0.0f) continue;
            const Mat4& m = palette[sv.joints[k]];
            position += m.transformPoint(sv.position) * w;
            const Vec3 n{m.m[0] * sv.normal.x + m.m[4] * sv.normal.y + m.m[8] * sv.normal.z,
                         m.m[1] * sv.normal.x + m.m[5] * sv.normal.y + m.m[9] * sv.normal.z,
                         m.m[2] * sv.normal.x + m.m[6] * sv.normal.y + m.m[10] * sv.normal.z};
            normal += n * w;
        }
        out.vertices[i].position = position;
        out.vertices[i].normal = normal.lengthSq() > 0.0f ? normal.normalized() : Vec3{0, 1, 0};
    }
    out.computeBounds();
}

// --------------------------------------------------- one-call integration --

void buildPosedCharacter(const HumanoidVariant& variant, const WearableInstance* wearables,
                         size_t wearableCount, const Pose& pose, BodyLod lod,
                         std::vector<CharacterPiece>& out) {
    out.clear();
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);

    uint32_t regions = kAllRegions;
    for (size_t i = 0; i < wearableCount; ++i) regions &= ~garmentCoverage(wearables[i].kind);

    SkinnedMeshData body;
    BodyBuildDesc desc;
    desc.lod = lod;
    desc.regions = regions;
    buildTemplateBody(desc, body);
    if (!body.vertices.empty()) {
        out.emplace_back();
        skinMesh(body, palette, out.back().mesh);
        for (int c = 0; c < 4; ++c) out.back().color[c] = variant.skin[c];
    }

    for (size_t i = 0; i < wearableCount; ++i) {
        const SkinnedMeshData& garment = sharedGarment(wearables[i].kind);
        if (garment.vertices.empty()) continue;  // held items are not garments
        out.emplace_back();
        skinMesh(garment, palette, out.back().mesh);
        for (int c = 0; c < 4; ++c) out.back().color[c] = wearables[i].color[c];
    }
}

}  // namespace mge
