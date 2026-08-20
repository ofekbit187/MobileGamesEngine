#include "mge/character/humanoid.h"

#include <cmath>

#include "mge/graphics/primitives.h"

namespace mge {

namespace {

constexpr size_t idx(Joint j) { return static_cast<size_t>(j); }

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Quat rotX(float a) { return Quat::fromAxisAngle({1, 0, 0}, a); }
Quat rotY(float a) { return Quat::fromAxisAngle({0, 1, 0}, a); }
Quat rotZ(float a) { return Quat::fromAxisAngle({0, 0, 1}, a); }

}  // namespace

void grantHumanoidActions(CharacterSystem& characters, EntityId entity) {
    characters.grant(entity, actionWalk());
    characters.grant(entity, actionJump());
    characters.grant(entity, actionUseHeld());
}

// ----------------------------------------------------------------- rig ------

Skeleton buildSkeleton(const HumanoidVariant& variant) {
    // The bind pose IS the template model's own pose (ADR 0005). The body is
    // Blender Studio's CC0 base mesh, and this rig was fitted to it — bones
    // measured inside its limbs, in its authored relaxed stance — rather than
    // the mesh being bent onto a rig. That is why the arms sit slightly away
    // from the body and the legs splay a little towards the ankles: that is
    // where the model's joints actually are.
    //
    // Every offset below is the fitted template value scaled by the variant:
    // overall size by height, limbs by their ratios, shoulders and hips by
    // their widths. Animation is unaffected — clips rotate joints, and every
    // variant shares this skeleton.
    Skeleton s{};
    const float k = variant.height / 1.75f;
    const float legK = k * (variant.legRatio / 0.50f);
    const float armK = k * (variant.armRatio / 0.44f);
    const float shoulderX = variant.shoulderWidth * 0.5f;
    const float hipX = variant.hipWidth * 0.5f;
    // Head joint (the atlas, where the skull pivots) to the crown, measured on
    // the template body: 1.750 - 1.580. It is what "the head" means everywhere
    // below, so head.y + headSize lands exactly on the variant's height.
    const float headSize = 0.170f * k * variant.headScale;

    const auto set = [&s](Joint joint, int8_t parent, Vec3 offset) {
        s.parent[idx(joint)] = parent;
        s.bindOffset[idx(joint)] = offset;
    };
    const auto p = [](Joint j) { return static_cast<int8_t>(j); };

    // --- spine: hips at 0.900 m on the template body ---
    set(Joint::Hips, -1, {0, 1.8f * variant.height * variant.legRatio * 0.5714f, 0});
    set(Joint::Spine, p(Joint::Hips), {0, 0.180f * k, 0});
    set(Joint::Chest, p(Joint::Spine), {0, 0.200f * k, -0.010f * k});
    set(Joint::Neck, p(Joint::Chest), {0, 0.220f * k, 0.000f});
    set(Joint::Head, p(Joint::Neck), {0, 0.080f * k, -0.010f * k});

    // --- arms: the model's relaxed stance, 23 degrees off vertical ---
    for (int side = 0; side < 2; ++side) {
        const float m = side == 0 ? 1.0f : -1.0f;
        const Joint upper = side == 0 ? Joint::UpperArmL : Joint::UpperArmR;
        const Joint fore = side == 0 ? Joint::ForearmL : Joint::ForearmR;
        const Joint hand = side == 0 ? Joint::HandL : Joint::HandR;
        set(upper, p(Joint::Chest), {m * shoulderX, 0.170f * k, 0.010f * k});
        set(fore, p(upper), {m * 0.114f * armK, -0.300f * armK, -0.045f * armK});
        set(hand, p(fore), {m * 0.102f * armK, -0.272f * armK, -0.046f * armK});
    }

    // --- legs: hip joints at the model's femoral heads, splaying to the ankle
    for (int side = 0; side < 2; ++side) {
        const float m = side == 0 ? 1.0f : -1.0f;
        const Joint thigh = side == 0 ? Joint::ThighL : Joint::ThighR;
        const Joint shin = side == 0 ? Joint::ShinL : Joint::ShinR;
        const Joint foot = side == 0 ? Joint::FootL : Joint::FootR;
        set(thigh, p(Joint::Hips), {m * hipX, -0.020f * k, 0.010f * k});
        set(shin, p(thigh), {m * 0.055f * legK, -0.450f * legK, -0.044f * legK});
        set(foot, p(shin), {m * 0.022f * legK, -0.315f * legK, -0.021f * legK});
    }

    // Bone lengths the body, animation and variant palette builders need.
    const auto len = [&s](Joint j) { return s.bindOffset[idx(j)].length(); };
    s.thighLength = len(Joint::ShinL);
    s.shinLength = len(Joint::FootL);
    s.upperArmLength = len(Joint::ForearmL);
    s.forearmLength = len(Joint::HandL);
    s.torsoLength = (0.180f + 0.200f + 0.220f) * k;
    s.headSize = headSize;
    return s;
}

void evaluatePose(const Skeleton& skeleton, const Pose& pose, Mat4 outWorld[kJointCount]) {
    // The joint enum is parent-before-child, so one forward pass suffices.
    for (size_t j = 0; j < kJointCount; ++j) {
        const Mat4 local =
            Mat4::translation(skeleton.bindOffset[j]) * Mat4::rotation(pose.rotation[j]);
        const int8_t parent = skeleton.parent[j];
        outWorld[j] = parent < 0 ? local : outWorld[static_cast<size_t>(parent)] * local;
    }
}

// ------------------------------------------- default animations (8.14) ------

void LocomotionAnimator::update(float dt, float speed, float runSpeed) {
    const float target = speed <= 0.01f ? 0.0f : clampf(speed / runSpeed, 0.15f, 1.0f);
    const float rate = 6.0f * dt;
    blend_ += clampf(target - blend_, -rate, rate);
    // Phase is distance-driven (speed * dt / stride): standing still freezes
    // the cycle, so feet never slide.
    const float stride = 0.75f + 0.85f * blend_;
    phase_ += speed * dt / stride;
    phase_ -= std::floor(phase_);
    idleTime_ += dt;
}

void LocomotionAnimator::samplePose(Pose& out) const {
    for (size_t j = 0; j < kJointCount; ++j) out.rotation[j] = Quat{};

    const float w = phase_ * 2.0f * kPi;
    const float s = std::sin(w);
    const float swing =
        clampf(blend_ * (0.85f + 0.45f * blend_), 0.0f, 0.85f);  // walk ~0.4 rad, run ~0.85

    // Legs counter-phase; positive X rotation swings the limb forward (-Z).
    out.rotation[idx(Joint::ThighL)] = rotX(s * swing);
    out.rotation[idx(Joint::ThighR)] = rotX(-s * swing);
    // Knees flex (foot back) hardest while the leg recovers.
    const float kneeL = swing * 1.1f * std::fmax(0.0f, std::sin(w + 2.2f));
    const float kneeR = swing * 1.1f * std::fmax(0.0f, std::sin(w + 2.2f + kPi));
    out.rotation[idx(Joint::ShinL)] = rotX(-kneeL);
    out.rotation[idx(Joint::ShinR)] = rotX(-kneeR);
    // Feet stay roughly level with the ground.
    out.rotation[idx(Joint::FootL)] = rotX(kneeL * 0.5f - s * swing * 0.4f);
    out.rotation[idx(Joint::FootR)] = rotX(kneeR * 0.5f + s * swing * 0.4f);

    // Arms counter-swing with a standing elbow bend.
    out.rotation[idx(Joint::UpperArmL)] = rotX(-s * swing * 0.7f);
    out.rotation[idx(Joint::UpperArmR)] = rotX(s * swing * 0.7f);
    const float elbow = 0.15f + 0.25f * blend_;
    out.rotation[idx(Joint::ForearmL)] = rotX(elbow);
    out.rotation[idx(Joint::ForearmR)] = rotX(elbow);

    // Torso counter-twist keeps the stride from looking stiff.
    out.rotation[idx(Joint::Spine)] = rotY(s * 0.08f * blend_);
    out.rotation[idx(Joint::Chest)] = rotY(s * 0.05f * blend_);

    // Idle layer: breathing and a slight arm hang, faded out by movement.
    const float idle = 1.0f - blend_;
    if (idle > 0.001f) {
        const float breath = std::sin(idleTime_ * 1.8f) * 0.025f * idle;
        out.rotation[idx(Joint::Chest)] = out.rotation[idx(Joint::Chest)] * rotX(breath);
        out.rotation[idx(Joint::UpperArmL)] =
            out.rotation[idx(Joint::UpperArmL)] * rotZ(0.06f * idle);
        out.rotation[idx(Joint::UpperArmR)] =
            out.rotation[idx(Joint::UpperArmR)] * rotZ(-0.06f * idle);
    }
}

// ------------------------------------------------- body & wearables ---------

namespace {

// Which regions each wearable kind claims (masking).
uint32_t coverOf(WearableKind kind) {
    switch (kind) {
        case WearableKind::Tunic:
        case WearableKind::Armor: return kCoverTorso;
        case WearableKind::Pants: return kCoverLegs;
        case WearableKind::Boots: return kCoverFeet;
        case WearableKind::HairShort:
        case WearableKind::HairLong: return kCoverScalp;
        case WearableKind::Sword: return 0;
    }
    return 0;
}

MeshData translated(MeshData mesh, const Vec3& offset) {
    for (Vertex& v : mesh.vertices) v.position += offset;
    mesh.computeBounds();
    return mesh;
}

void push(std::vector<RigPart>& out, Joint joint, MeshData mesh, const float color[4]) {
    RigPart part;
    part.joint = joint;
    part.mesh = std::move(mesh);
    for (int i = 0; i < 4; ++i) part.color[i] = color[i];
    out.push_back(std::move(part));
}

// Everything the builders below need, derived once from the variant. The
// wearable templates are generated from these SAME numbers, which is why one
// wearable fits every variant (CHARACTERS.md §5.1).
struct BodyDims {
    Skeleton skeleton;
    float scaleH;  // overall size factor vs the reference 1.75 m body
    float thick;   // limb thickness factor (bulk * scaleH)
    float depth;   // torso front-to-back
    float armR, legR, headR;
};

BodyDims dimsOf(const HumanoidVariant& v) {
    BodyDims d;
    d.skeleton = buildSkeleton(v);
    d.scaleH = v.height / 1.75f;
    d.thick = v.bulk * d.scaleH;
    d.depth = std::fmax(0.16f * d.scaleH, 0.50f * v.shoulderWidth) * v.bulk;
    d.armR = 0.055f * d.thick;
    d.legR = 0.075f * d.thick;
    d.headR = d.skeleton.headSize * 0.36f;
    return d;
}

// Torso boxes shared by the body and by torso garments; `grow` inflates them
// outward (0 for skin).
void torsoBoxes(const HumanoidVariant& v, const BodyDims& d, float grow,
                std::vector<RigPart>& out, const float color[4]) {
    const float t = d.skeleton.torsoLength;
    const Vec3 g{grow * 2.0f, grow, grow * 2.0f};
    push(out, Joint::Hips,
         translated(makeBox(Vec3{v.hipWidth * 1.15f, t * 0.30f, d.depth} + g),
                    {0, t * 0.14f, 0}),
         color);
    push(out, Joint::Spine,
         translated(makeBox(Vec3{(v.hipWidth + v.shoulderWidth) * 0.52f, t * 0.37f,
                                 d.depth * 0.95f} + g),
                    {0, t * 0.175f, 0}),
         color);
    push(out, Joint::Chest,
         translated(makeBox(Vec3{v.shoulderWidth * 0.95f, t * 0.38f, d.depth} + g),
                    {0, t * 0.175f, 0}),
         color);
}

void legParts(const BodyDims& d, float grow, std::vector<RigPart>& out, const float color[4]) {
    const Skeleton& s = d.skeleton;
    const float r = d.legR + grow;
    for (int side = 0; side < 2; ++side) {
        const Joint thigh = side == 0 ? Joint::ThighL : Joint::ThighR;
        const Joint shin = side == 0 ? Joint::ShinL : Joint::ShinR;
        push(out, thigh,
             translated(makeCapsule(r, s.thighLength + r, 12, 4), {0, -s.thighLength * 0.5f, 0}),
             color);
        push(out, shin,
             translated(makeCapsule(r * 0.8f, s.shinLength + r * 0.8f, 12, 4),
                        {0, -s.shinLength * 0.5f, 0}),
             color);
    }
}

void footParts(const BodyDims& d, float grow, std::vector<RigPart>& out, const float color[4]) {
    // Toes point forward (-Z); the foot joint sits at the ankle.
    const Vec3 size{0.095f * d.thick + grow * 2.0f, 0.055f * d.scaleH + grow,
                    0.24f * d.scaleH + grow * 2.0f};
    for (int side = 0; side < 2; ++side) {
        const Joint foot = side == 0 ? Joint::FootL : Joint::FootR;
        push(out, foot, translated(makeBox(size), {0, -0.018f * d.scaleH, -0.055f * d.scaleH}),
             color);
    }
}

MeshData buildSwordMesh(const BodyDims& d, bool sheathed) {
    // Grip + guard + blade, composed in hand-local space (blade down along -Y)
    // or chest-local space on the back when sheathed (blade up).
    const float k = d.scaleH;
    MeshData sword;
    if (!sheathed) {
        appendMesh(sword, makeCylinder(0.016f * k, 0.15f * k, 10), {0, 0, 0});
        appendMesh(sword, makeBox({0.15f * k, 0.022f * k, 0.035f * k}), {0, -0.10f * k, 0});
        appendMesh(sword, makeBox({0.045f * k, 0.62f * k, 0.014f * k}), {0, -0.43f * k, 0});
    } else {
        const float backZ = d.depth * 0.75f;
        const float top = d.skeleton.torsoLength * 0.30f;
        appendMesh(sword, makeBox({0.045f * k, 0.62f * k, 0.014f * k}),
                   {0.04f * k, top - 0.28f * k, backZ});
        appendMesh(sword, makeBox({0.15f * k, 0.022f * k, 0.035f * k}),
                   {0.04f * k, top + 0.05f * k, backZ});
        appendMesh(sword, makeCylinder(0.016f * k, 0.15f * k, 10),
                   {0.04f * k, top + 0.13f * k, backZ});
    }
    return sword;
}

}  // namespace

void buildHumanoidVisual(const HumanoidVariant& variant, const WearableInstance* wearables,
                         size_t wearableCount, std::vector<RigPart>& outParts) {
    outParts.clear();
    const BodyDims d = dimsOf(variant);
    const Skeleton& s = d.skeleton;

    uint32_t covered = 0;
    for (size_t i = 0; i < wearableCount; ++i) covered |= coverOf(wearables[i].kind);

    // --- Body, minus covered regions (masking: no cloth-through-skin ever) ---
    const float* skin = variant.skin;
    if ((covered & kCoverTorso) == 0) torsoBoxes(variant, d, 0.0f, outParts, skin);
    if ((covered & kCoverLegs) == 0) legParts(d, 0.0f, outParts, skin);
    if ((covered & kCoverFeet) == 0) footParts(d, 0.0f, outParts, skin);
    if ((covered & kCoverArms) == 0) {
        for (int side = 0; side < 2; ++side) {
            const Joint upper = side == 0 ? Joint::UpperArmL : Joint::UpperArmR;
            const Joint fore = side == 0 ? Joint::ForearmL : Joint::ForearmR;
            push(outParts, upper,
                 translated(makeCapsule(d.armR, s.upperArmLength + d.armR, 12, 4),
                            {0, -s.upperArmLength * 0.5f, 0}),
                 skin);
            push(outParts, fore,
                 translated(makeCapsule(d.armR * 0.85f, s.forearmLength + d.armR * 0.85f, 12, 4),
                            {0, -s.forearmLength * 0.5f, 0}),
                 skin);
        }
    }
    // Hands, neck, and head are always visible in v1.
    for (int side = 0; side < 2; ++side) {
        const Joint hand = side == 0 ? Joint::HandL : Joint::HandR;
        push(outParts, hand,
             translated(makeBox({d.armR * 1.7f, d.armR * 2.4f, d.armR * 1.2f}),
                        {0, -d.armR * 1.2f, 0}),
             skin);
    }
    push(outParts, Joint::Neck,
         translated(makeCylinder(d.headR * 0.55f, s.headSize * 0.40f, 12),
                    {0, s.headSize * 0.05f, 0}),
         skin);
    push(outParts, Joint::Head,
         translated(makeCapsule(d.headR, s.headSize, 14, 5), {0, s.headSize * 0.45f, 0}), skin);

    // --- Wearables: generated from the SAME proportions + layer thickness, ---
    // --- so a higher layer always encloses a lower one.                    ---
    for (size_t i = 0; i < wearableCount; ++i) {
        const WearableInstance& wear = wearables[i];
        const float t = (0.012f + 0.014f * wear.layer) * d.scaleH;
        const float* c = wear.color;
        switch (wear.kind) {
            case WearableKind::Tunic:
            case WearableKind::Armor: {
                torsoBoxes(variant, d, t, outParts, c);
                // Short sleeves over the shoulder.
                for (int side = 0; side < 2; ++side) {
                    const Joint upper = side == 0 ? Joint::UpperArmL : Joint::UpperArmR;
                    push(outParts, upper,
                         translated(makeCapsule(d.armR + t, s.upperArmLength * 0.55f + d.armR + t,
                                                12, 4),
                                    {0, -s.upperArmLength * 0.22f, 0}),
                         c);
                }
                break;
            }
            case WearableKind::Pants:
                legParts(d, t, outParts, c);
                break;
            case WearableKind::Boots: {
                footParts(d, t, outParts, c);
                // Cuff up the lower shin.
                for (int side = 0; side < 2; ++side) {
                    const Joint shin = side == 0 ? Joint::ShinL : Joint::ShinR;
                    push(outParts, shin,
                         translated(makeCylinder(d.legR * 0.8f + t * 2.0f, s.shinLength * 0.45f,
                                                 12),
                                    {0, -s.shinLength * 0.78f, 0}),
                         c);
                }
                break;
            }
            case WearableKind::HairShort:
                push(outParts, Joint::Head,
                     translated(makeCapsule(d.headR + t, s.headSize * 0.9f + t, 14, 5),
                                {0, s.headSize * 0.58f, 0}),
                     c);
                break;
            case WearableKind::HairLong: {
                push(outParts, Joint::Head,
                     translated(makeCapsule(d.headR + t, s.headSize * 0.9f + t, 14, 5),
                                {0, s.headSize * 0.58f, 0}),
                     c);
                // Back fall of hair (behind is +Z).
                push(outParts, Joint::Head,
                     translated(makeBox({s.headSize * 0.8f, s.headSize * 1.5f,
                                         s.headSize * 0.22f}),
                                {0, -s.headSize * 0.15f, d.headR * 0.85f}),
                     c);
                break;
            }
            case WearableKind::Sword:
                push(outParts, wear.sheathed ? Joint::Chest : Joint::HandR,
                     buildSwordMesh(d, wear.sheathed), c);
                break;
        }
    }
}

}  // namespace mge
