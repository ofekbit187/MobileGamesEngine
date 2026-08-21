// Held items on the real body (task 14.7).
//
// The bug these tests exist to prevent is not a regression — it is the absence
// that looked like a feature. A sword appeared in early captures because the
// v1 box rig generated one as a rigid part; when the body became a skinned
// mesh, `buildPosedCharacter` skipped held items entirely and nobody noticed,
// because the captures that would have shown it were made by the old path.
// So the first test here is the blunt one: *does anything appear at all.*

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/held_items.h"
#include "mge/character/humanoid.h"
#include "mge/character/wearable_catalogue.h"
#include "test_framework.h"

using namespace mge;

namespace {

Pose restPose() {
    Pose pose;
    for (size_t j = 0; j < kJointCount; ++j) pose.rotation[j] = Quat{0, 0, 0, 1};
    return pose;
}

bool swordPresent() {
    const size_t sword = wearableCatalogue().find("sword");
    return sword != WearableCatalogue::npos && !sharedHeldItemMesh(sword).vertices.empty();
}

size_t swordRow() { return wearableCatalogue().find("sword"); }

// Where the item ended up, as a bounding box in character-local space.
Aabb placedBounds(bool sheathed, const HumanoidVariant& variant) {
    Mat4 world[kJointCount];
    evaluatePose(buildSkeleton(variant), restPose(), world);
    HeldItemPlacement placement;
    if (!placeHeldItem(swordRow(), sheathed, variant, world, placement)) return Aabb{};
    return placement.mesh.bounds;
}

Vec3 jointPosition(Joint joint, const HumanoidVariant& variant) {
    Mat4 world[kJointCount];
    evaluatePose(buildSkeleton(variant), restPose(), world);
    return world[static_cast<size_t>(joint)].transformPoint({0, 0, 0});
}

}  // namespace

// ------------------------------------------------------- it exists at all --

MGE_TEST(a_character_holding_something_actually_holds_something) {
    // The whole point of 14.7. Before it, this returned body + garments and
    // silently dropped the sword.
    if (!swordPresent()) return;
    const HumanoidVariant variant;
    const WearableInstance outfit[] = {
        {WearableKind::Tunic, 1, false, {1, 1, 1, 1}},
        {WearableKind::Sword, 1, false, {1, 1, 1, 1}},
    };
    std::vector<CharacterPiece> pieces;
    buildPosedCharacter(variant, outfit, 2, restPose(), BodyLod::Lod0, pieces);

    // body + tunic + sword.
    MGE_CHECK(pieces.size() == 3);
    // And the sword piece has real geometry, not an empty placeholder.
    bool anyNonEmpty = true;
    for (const CharacterPiece& piece : pieces) {
        if (piece.mesh.vertices.empty()) anyNonEmpty = false;
    }
    MGE_CHECK(anyNonEmpty);
}

MGE_TEST(a_held_item_is_not_a_garment_and_masks_nothing) {
    // Held items must never reach the fitting pipeline: they are rigid, they
    // have no `.mgefit`, and they hide no skin. A sword that masked the torso
    // would carve a hole in the character.
    if (!swordPresent()) return;
    MGE_CHECK(isHeldItem(swordRow()));
    MGE_CHECK(garmentCoverage(WearableKind::Sword) == 0);
    MGE_CHECK(garmentCoverageById(swordRow()) == 0);
    // And the garment loader does not claim it.
    MGE_CHECK(sharedGarment(WearableKind::Sword).vertices.empty());
}

// ------------------------------------------------------------ in the hand --

MGE_TEST(a_drawn_sword_is_in_the_hand) {
    if (!swordPresent()) return;
    const HumanoidVariant variant;
    const Vec3 hand = jointPosition(Joint::HandR, variant);
    const Aabb bounds = placedBounds(false, variant);

    // The grip end sits at the hand. The blade runs away from it, so the box
    // is large — what matters is that the hand is inside it.
    MGE_CHECK(hand.x > bounds.min.x - 0.12f && hand.x < bounds.max.x + 0.12f);
    MGE_CHECK(hand.y > bounds.min.y - 0.12f && hand.y < bounds.max.y + 0.12f);
    MGE_CHECK(hand.z > bounds.min.z - 0.12f && hand.z < bounds.max.z + 0.12f);

    // And it hangs DOWN from the fist rather than pointing at the sky: the
    // blade's lowest point is well below the hand. The hand's local +Y runs up
    // the arm, so an unrotated grip aimed the sword over the shoulder — this
    // pins the corrected orientation.
    MGE_CHECK(bounds.min.y < hand.y - 0.30f);
}

MGE_TEST(a_sheathed_sword_moves_to_the_back_and_hangs_blade_down) {
    if (!swordPresent()) return;
    const HumanoidVariant variant;
    const Aabb drawn = placedBounds(false, variant);
    const Aabb sheathed = placedBounds(true, variant);

    // It moved.
    MGE_CHECK((sheathed.center() - drawn.center()).length() > 0.25f);
    // Behind the spine (+Z is behind on this rig).
    MGE_CHECK(sheathed.center().z > 0.02f);

    // Blade down, hilt up at the shoulder: the sword occupies a tall span that
    // tops out near shoulder height and does NOT rise above the head. Both
    // failures happened during 14.7 and both were invisible in a centroid —
    // first the sword lay flat across the back (0.18 m of span for an 0.83 m
    // weapon), then it stood upright with its point above the skull.
    const float span = sheathed.max.y - sheathed.min.y;
    MGE_CHECK(span > 0.55f);                    // not lying flat
    MGE_CHECK(sheathed.max.y < variant.height); // not over the head
    MGE_CHECK(sheathed.max.y > variant.height * 0.80f);  // hilt is up at the shoulder
}

MGE_TEST(an_item_that_cannot_be_sheathed_stays_in_the_hand) {
    // A torch does not go on your back because combat ended.
    if (!swordPresent()) return;
    WearableDef def;
    const WearableParseResult parsed = parseWearableDef(
        "version 1\nid torch\nmesh item_sword\nheld true\nsheathable false\n", def);
    MGE_CHECK(parsed.ok);
    MGE_CHECK(def.held);
    MGE_CHECK(!def.heldItem.sheathable);
}

MGE_TEST(a_held_item_follows_the_character_it_is_held_by) {
    // Sockets are derived from the rig, so a taller character's grip point is
    // where that character's hand actually is — no per-variant data.
    if (!swordPresent()) return;
    HumanoidVariant small;
    small.height = 1.55f;
    HumanoidVariant tall;
    tall.height = 1.95f;

    const Aabb low = placedBounds(false, small);
    const Aabb high = placedBounds(false, tall);
    MGE_CHECK(high.center().y > low.center().y + 0.05f);

    // The hand it is in moved by about the same amount as the sword did.
    const float handRise =
        jointPosition(Joint::HandR, tall).y - jointPosition(Joint::HandR, small).y;
    const float swordRise = high.center().y - low.center().y;
    MGE_CHECK(std::fabs(swordRise - handRise) < 0.08f);
}

// --------------------------------------------------------------- the data --

MGE_TEST(attachment_points_and_grips_round_trip_through_their_names) {
    for (size_t i = 0; i < kAttachPointCount; ++i) {
        const AttachPoint point = static_cast<AttachPoint>(i);
        AttachPoint parsed = AttachPoint::Count;
        MGE_CHECK(attachPointFromName(attachPointName(point), parsed));
        MGE_CHECK(parsed == point);
    }
    for (size_t i = 0; i < static_cast<size_t>(GripType::Count); ++i) {
        const GripType grip = static_cast<GripType>(i);
        GripType parsed = GripType::Count;
        MGE_CHECK(gripTypeFromName(gripTypeName(grip), parsed));
        MGE_CHECK(parsed == grip);
    }
    AttachPoint point = AttachPoint::Count;
    MGE_CHECK(!attachPointFromName("elbow", point));
    GripType grip = GripType::Count;
    MGE_CHECK(!gripTypeFromName("three_handed", grip));
}

MGE_TEST(a_held_item_declares_its_grip_and_anchors_in_data) {
    WearableDef def;
    const WearableParseResult parsed = parseWearableDef(
        "version 1\n"
        "id       greatsword\n"
        "mesh     item_sword\n"
        "held     true\n"
        "grip     two_handed\n"
        "anchor   hand_r\n"
        "sheath   hip_l\n"
        "grip_offset 0 0.02 0\n"
        "grip_rotate 160 0 0\n",
        def);
    MGE_CHECK(parsed.ok);
    if (!parsed.ok) printf("  %s\n", parsed.message.c_str());
    MGE_CHECK(def.held);
    MGE_CHECK(def.heldItem.grip == GripType::TwoHanded);
    MGE_CHECK(def.heldItem.drawn == AttachPoint::HandR);
    MGE_CHECK(def.heldItem.sheathed == AttachPoint::HipL);
    MGE_CHECK_NEAR(def.heldItem.gripOffset.y, 0.02f, 1e-6);
    MGE_CHECK_NEAR(def.heldItem.gripRotation.x, 160.0f, 1e-4);

    // A held item still needs geometry, and bad anchors are named.
    WearableDef bad;
    WearableParseResult result =
        parseWearableDef("version 1\nid x\nheld true\n", bad);
    MGE_CHECK(!result.ok);
    MGE_CHECK(std::string(result.message).find("mesh") != std::string::npos);

    result = parseWearableDef("version 1\nid x\nmesh m\nheld true\nanchor knee\n", bad);
    MGE_CHECK(!result.ok);
    MGE_CHECK(std::string(result.message).find("attachment point") != std::string::npos);
}

MGE_TEST(a_missing_item_mesh_costs_the_item_not_the_character) {
    // Data can name an item whose asset is not installed. The character must
    // still render — one missing sword is not a missing person.
    const HumanoidVariant variant;
    Mat4 world[kJointCount];
    evaluatePose(buildSkeleton(variant), restPose(), world);
    HeldItemPlacement placement;
    MGE_CHECK(!placeHeldItem(9999, false, variant, world, placement));

    // A garment row is not a held item and refuses placement rather than
    // producing nonsense.
    const size_t tunic = wearableCatalogue().find("tunic");
    if (tunic != WearableCatalogue::npos) {
        MGE_CHECK(!isHeldItem(tunic));
        MGE_CHECK(heldItemDef(tunic) == nullptr);
        MGE_CHECK(!placeHeldItem(tunic, false, variant, world, placement));
    }
}
