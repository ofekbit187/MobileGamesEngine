#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"

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

bool loadBinding(const char* name, GarmentBinding& out) {
    const std::string path = assetDir() + "/" + name + ".mgefit";
    if (!readBindingFile(path.c_str(), out)) {
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
    // Morph targets index the VERTEX buffer, which masking never touches —
    // only triangle ranges are dropped — so they carry over unchanged.
    out.morphs = src.morphs;
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

// The baked surface binding for a garment (ADR 0008, task 13.2). Absent when
// the garment has not been through `mge_garment_fit` — the character still
// renders, it just cannot follow morph-driven shape.
const GarmentBinding& sharedGarmentBinding(WearableKind kind) {
    static const std::vector<GarmentBinding> bindings = [] {
        std::vector<GarmentBinding> loaded(8);
        for (size_t k = 0; k < loaded.size(); ++k) {
            const char* name = garmentAsset(static_cast<WearableKind>(k));
            if (name != nullptr) loadBinding(name, loaded[k]);
        }
        return loaded;
    }();
    const size_t k = static_cast<size_t>(kind);
    return bindings[k < bindings.size() ? k : 0];
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

// ------------------------------------------------------------- morphs ------

const char* morphName(Morph morph) {
    switch (morph) {
        case Morph::BodyChest: return "body.chest";
        case Morph::BodyBelly: return "body.belly";
        case Morph::BodySeat: return "body.seat";
        case Morph::BodyMuscle: return "body.muscle";
        case Morph::BodyNeck: return "body.neck";
        case Morph::FaceSkull: return "face.skull";
        case Morph::FaceBrow: return "face.brow";
        case Morph::FaceCheeks: return "face.cheeks";
        case Morph::FaceJawWidth: return "face.jawWidth";
        case Morph::FaceChin: return "face.chin";
        case Morph::FaceNoseLength: return "face.noseLength";
        case Morph::FaceNoseWidth: return "face.noseWidth";
        case Morph::FaceMouth: return "face.mouth";
        case Morph::FaceEyes: return "face.eyes";
        case Morph::FaceEars: return "face.ears";
        default: return "";
    }
}

void morphWeights(const HumanoidVariant& requested, float out[kMorphCount]) {
    const HumanoidVariant v = clampToScope(requested);
    out[static_cast<size_t>(Morph::BodyChest)] = v.chest;
    out[static_cast<size_t>(Morph::BodyBelly)] = v.belly;
    out[static_cast<size_t>(Morph::BodySeat)] = v.seat;
    out[static_cast<size_t>(Morph::BodyMuscle)] = v.muscle;
    out[static_cast<size_t>(Morph::BodyNeck)] = v.neck;
    out[static_cast<size_t>(Morph::FaceSkull)] = v.face.skull;
    out[static_cast<size_t>(Morph::FaceBrow)] = v.face.brow;
    out[static_cast<size_t>(Morph::FaceCheeks)] = v.face.cheeks;
    out[static_cast<size_t>(Morph::FaceJawWidth)] = v.face.jawWidth;
    out[static_cast<size_t>(Morph::FaceChin)] = v.face.chin;
    out[static_cast<size_t>(Morph::FaceNoseLength)] = v.face.noseLength;
    out[static_cast<size_t>(Morph::FaceNoseWidth)] = v.face.noseWidth;
    out[static_cast<size_t>(Morph::FaceMouth)] = v.face.mouth;
    out[static_cast<size_t>(Morph::FaceEyes)] = v.face.eyes;
    out[static_cast<size_t>(Morph::FaceEars)] = v.face.ears;
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
    float none[kMorphCount] = {0};
    skinMesh(mesh, palette, none, out);
}

void skinMesh(const SkinnedMeshData& mesh, const Mat4 palette[kJointCount],
              const float weights[kMorphCount], MeshData& out) {
    out.vertices.resize(mesh.vertices.size());
    out.indices = mesh.indices;

    // The shape pass, in bind space, before any bone touches the vertex —
    // the order the shader will use, so this stays the reference (ADR 0009).
    // Only the vertices a target actually moves are visited, so a face
    // parameter costs a few dozen adds on a 1640-vertex body.
    static thread_local std::vector<Vec3> morphedPosition;
    static thread_local std::vector<Vec3> morphedNormal;
    bool anyMorph = false;
    for (size_t t = 0; t < kMorphCount && !anyMorph; ++t) {
        anyMorph = weights[t] < -1e-4f || weights[t] > 1e-4f;
    }
    if (anyMorph) {
        morphedPosition.assign(mesh.vertices.size(), Vec3{0, 0, 0});
        morphedNormal.assign(mesh.vertices.size(), Vec3{0, 0, 0});
        for (const MorphTarget& target : mesh.morphs) {
            const float w = weights[static_cast<size_t>(target.morph)];
            if (w > -1e-4f && w < 1e-4f) continue;
            const float positionScale = w * target.scale * (1.0f / 32767.0f);
            const float normalScale = w * (1.0f / 127.0f);
            for (const MorphDelta& d : target.deltas) {
                if (d.vertex >= mesh.vertices.size()) continue;
                Vec3& p = morphedPosition[d.vertex];
                p.x += static_cast<float>(d.position[0]) * positionScale;
                p.y += static_cast<float>(d.position[1]) * positionScale;
                p.z += static_cast<float>(d.position[2]) * positionScale;
                Vec3& n = morphedNormal[d.vertex];
                n.x += static_cast<float>(d.normal[0]) * normalScale;
                n.y += static_cast<float>(d.normal[1]) * normalScale;
                n.z += static_cast<float>(d.normal[2]) * normalScale;
            }
        }
    }

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const SkinVertex& sv = mesh.vertices[i];
        const Vec3 bindPosition =
            anyMorph ? sv.position + morphedPosition[i] : sv.position;
        const Vec3 bindNormal =
            anyMorph ? (sv.normal + morphedNormal[i]).normalized() : sv.normal;
        Vec3 position{0, 0, 0};
        Vec3 normal{0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            const float w = static_cast<float>(sv.weights[k]) * (1.0f / 255.0f);
            if (w <= 0.0f) continue;
            const Mat4& m = palette[sv.joints[k]];
            position += m.transformPoint(bindPosition) * w;
            const Vec3 n{m.m[0] * bindNormal.x + m.m[4] * bindNormal.y + m.m[8] * bindNormal.z,
                         m.m[1] * bindNormal.x + m.m[5] * bindNormal.y + m.m[9] * bindNormal.z,
                         m.m[2] * bindNormal.x + m.m[6] * bindNormal.y + m.m[10] * bindNormal.z};
            normal += n * w;
        }
        out.vertices[i].position = position;
        out.vertices[i].normal = normal.lengthSq() > 0.0f ? normal.normalized() : Vec3{0, 1, 0};
    }
    out.computeBounds();
}

// --------------------------------------------------- one-call integration --

// Outward-cascading masking (CHARACTERS.md §5.4): a garment sealed under a
// strictly outer one that covers everything it covers is never seen, so it is
// not skinned and not drawn. Armour over a tunic costs the armour alone.
bool wearableHidden(const WearableInstance* wearables, size_t count, size_t index) {
    if (wearables == nullptr || index >= count) return false;
    const uint32_t mine = garmentCoverage(wearables[index].kind);
    if (mine == 0) return false;  // hair and held items hide nothing and are never hidden
    for (size_t j = 0; j < count; ++j) {
        if (j == index || wearables[j].layer <= wearables[index].layer) continue;
        if ((garmentCoverage(wearables[j].kind) & mine) == mine) return true;
    }
    return false;
}

void buildPosedCharacter(const HumanoidVariant& variant, const WearableInstance* wearables,
                         size_t wearableCount, const Pose& pose, BodyLod lod,
                         std::vector<CharacterPiece>& out) {
    out.clear();
    Mat4 palette[kJointCount];
    buildSkinPalette(variant, pose, palette);
    float shape[kMorphCount];
    morphWeights(variant, shape);

    uint32_t regions = kAllRegions;
    for (size_t i = 0; i < wearableCount; ++i) regions &= ~garmentCoverage(wearables[i].kind);

    SkinnedMeshData body;
    BodyBuildDesc desc;
    desc.lod = lod;
    desc.regions = regions;
    buildTemplateBody(desc, body);
    if (!body.vertices.empty()) {
        out.emplace_back();
        skinMesh(body, palette, shape, out.back().mesh);
        for (int c = 0; c < 4; ++c) out.back().color[c] = variant.skin[c];
    }

    // Does this character have any morph-driven shape at all? Bone-scale
    // variants (height, bulk, shoulders) ride the palette and need no re-fit;
    // only a morph moves the surface a garment is bound to.
    bool anyShape = false;
    for (size_t t = 0; t < kMorphCount && !anyShape; ++t) {
        anyShape = shape[t] < -1e-4f || shape[t] > 1e-4f;
    }

    for (size_t i = 0; i < wearableCount; ++i) {
        const SkinnedMeshData& garment = sharedGarment(wearables[i].kind);
        if (garment.vertices.empty()) continue;  // held items are not garments
        if (wearableHidden(wearables, wearableCount, i)) continue;

        // Garments follow the variant's PROPORTIONS through the same palette.
        // Following its SHAPE is the surface binding's job (ADR 0008): the
        // garment's vertices are re-derived from the morphed body before
        // skinning, so a heavy belly pushes the tunic out instead of coming
        // through it. Bindings are baked against the UNMASKED LOD0 body —
        // that is the surface they were baked against, and the hash check
        // refuses anything else.
        const SkinnedMeshData* posed = &garment;
        SkinnedMeshData* refitted = nullptr;
        if (anyShape) {
            const GarmentBinding& binding = sharedGarmentBinding(wearables[i].kind);
            const std::vector<SkinnedMeshData>& lods = sharedTemplateLods();
            if (!binding.empty() && !lods.empty()) {
                static GarmentFitCache cache;
                static thread_local SkinnedMeshData scratch;
                const std::vector<SkinVertex>* vertices = nullptr;
                if (cache.request(static_cast<uint32_t>(wearables[i].kind), binding, lods[0],
                                  garment, shape, &vertices) == GarmentFitCache::Status::Ready &&
                    vertices != nullptr && vertices->size() == garment.vertices.size()) {
                    scratch = garment;
                    scratch.vertices = *vertices;
                    refitted = &scratch;
                    posed = refitted;
                }
                // Pending or Refused: draw the garment unrefitted rather than
                // stall or drop it. A slightly loose tunic for a moment beats a
                // missing one, and a refusal has already logged its reason.
            }
        }

        out.emplace_back();
        skinMesh(*posed, palette, out.back().mesh);
        for (int c = 0; c < 4; ++c) out.back().color[c] = wearables[i].color[c];
    }
}

}  // namespace mge
