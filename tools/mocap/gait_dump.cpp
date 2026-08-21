// Dump the engine's own procedural locomotion as per-frame joint positions.
//
// WHY THIS IS A C++ TOOL AND NOT A PYTHON RE-IMPLEMENTATION. Task 18.1 asks
// how our shipped walk compares to a real human. That answer is only worth
// having if the "our walk" side IS the shipped walk. A Python model of
// `LocomotionAnimator` would be a second implementation that can silently
// drift from the one the game plays, and the comparison would then be
// measuring the drift instead of the walk. So this links `mge_core` and calls
// `buildSkeleton`, `LocomotionAnimator::update/samplePose` and `evaluatePose`
// exactly as the runtime does, and emits what came out.
//
// It emits POSITIONS, not rotations, on purpose: the reference side of 18.1 is
// a landmark estimator, which produces positions. Converting both sides to the
// same quantity and running one metric implementation over both (see
// `gait_compare.py`) is what stops a difference in measurement from being read
// as a difference in motion.
//
// Host analysis tool. Not shipped, not on any frame path, so the P1 zero-
// allocation rule does not bind here — but note it allocates only at startup
// and writes a stream, so it would pass anyway.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "mge/character/humanoid.h"
#include "mge/core/math.h"

using namespace mge;

namespace {

const char* jointName(Joint j) {
    switch (j) {
        case Joint::Hips: return "Hips";
        case Joint::Spine: return "Spine";
        case Joint::Chest: return "Chest";
        case Joint::Neck: return "Neck";
        case Joint::Head: return "Head";
        case Joint::UpperArmL: return "UpperArmL";
        case Joint::ForearmL: return "ForearmL";
        case Joint::HandL: return "HandL";
        case Joint::UpperArmR: return "UpperArmR";
        case Joint::ForearmR: return "ForearmR";
        case Joint::HandR: return "HandR";
        case Joint::ThighL: return "ThighL";
        case Joint::ShinL: return "ShinL";
        case Joint::FootL: return "FootL";
        case Joint::ThighR: return "ThighR";
        case Joint::ShinR: return "ShinR";
        case Joint::FootR: return "FootR";
        case Joint::Count: break;
    }
    return "?";
}

[[noreturn]] void usage() {
    std::fprintf(stderr,
                 "usage: mge_gait_dump [--speed M/S] [--fps HZ] [--seconds S]\n"
                 "                     [--run-speed M/S] [--height M] [--out FILE]\n"
                 "\n"
                 "Runs the engine's LocomotionAnimator at a constant speed and writes\n"
                 "per-frame joint world positions (character-local space) as JSON.\n");
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    float speed = 1.4f, fps = 60.0f, seconds = 8.0f, runSpeed = 4.0f, height = 1.75f;
    const char* out = nullptr;

    for (int i = 1; i < argc; ++i) {
        const auto next = [&](void) -> const char* {
            if (i + 1 >= argc) usage();
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--speed")) speed = std::atof(next());
        else if (!std::strcmp(argv[i], "--fps")) fps = std::atof(next());
        else if (!std::strcmp(argv[i], "--seconds")) seconds = std::atof(next());
        else if (!std::strcmp(argv[i], "--run-speed")) runSpeed = std::atof(next());
        else if (!std::strcmp(argv[i], "--height")) height = std::atof(next());
        else if (!std::strcmp(argv[i], "--out")) out = next();
        else usage();
    }
    if (!(speed >= 0.0f) || !(fps > 0.0f) || !(seconds > 0.0f)) usage();

    HumanoidVariant variant;
    variant.height = height;
    const Skeleton skeleton = buildSkeleton(variant);

    std::FILE* f = out ? std::fopen(out, "w") : stdout;
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }

    const int frames = static_cast<int>(seconds * fps + 0.5f);
    const float dt = 1.0f / fps;

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"source\": \"mge LocomotionAnimator\",\n");
    std::fprintf(f, "  \"speed\": %.6f,\n  \"fps\": %.6f,\n  \"frames\": %d,\n", speed, fps, frames);
    std::fprintf(f, "  \"run_speed\": %.6f,\n  \"height\": %.6f,\n", runSpeed, height);
    std::fprintf(f, "  \"note\": \"positions are character-local metres; the root carries no "
                   "translation, so ground travel is speed*t by construction\",\n");
    std::fprintf(f, "  \"joints\": [");
    for (size_t j = 0; j < kJointCount; ++j)
        std::fprintf(f, "%s\"%s\"", j ? ", " : "", jointName(static_cast<Joint>(j)));
    std::fprintf(f, "],\n");
    std::fprintf(f, "  \"skeleton\": {\"thigh\": %.6f, \"shin\": %.6f, \"upperArm\": %.6f, "
                   "\"forearm\": %.6f, \"torso\": %.6f},\n",
                 skeleton.thighLength, skeleton.shinLength, skeleton.upperArmLength,
                 skeleton.forearmLength, skeleton.torsoLength);
    std::fprintf(f, "  \"bind\": [");
    for (size_t j = 0; j < kJointCount; ++j)
        std::fprintf(f, "%s[%.6f, %.6f, %.6f]", j ? ", " : "", skeleton.bindOffset[j].x,
                     skeleton.bindOffset[j].y, skeleton.bindOffset[j].z);
    std::fprintf(f, "],\n");
    std::fprintf(f, "  \"data\": [\n");

    LocomotionAnimator anim;
    Pose pose;
    Mat4 world[kJointCount];

    for (int i = 0; i < frames; ++i) {
        anim.update(dt, speed, runSpeed);
        anim.samplePose(pose);
        evaluatePose(skeleton, pose, world);

        std::fprintf(f, "    {\"f\": %d, \"t\": %.6f, \"phase\": %.6f, \"pos\": [", i, i * dt,
                     anim.phase());
        for (size_t j = 0; j < kJointCount; ++j)
            std::fprintf(f, "%s[%.6f, %.6f, %.6f]", j ? ", " : "", world[j].m[12], world[j].m[13],
                         world[j].m[14]);
        // Local rotations too: 18.3 needs to compare against rotations, and a
        // dump that only carries positions cannot be checked against it later.
        std::fprintf(f, "], \"rot\": [");
        for (size_t j = 0; j < kJointCount; ++j)
            std::fprintf(f, "%s[%.6f, %.6f, %.6f, %.6f]", j ? ", " : "", pose.rotation[j].x,
                         pose.rotation[j].y, pose.rotation[j].z, pose.rotation[j].w);
        // Synthetic toe, because THE RIG HAS NO TOE JOINT. The leg chain ends
        // at Foot, which IS the ankle, so a three-point ankle angle cannot be
        // formed from rig joints at all — while the reference estimator gives a
        // foot_index landmark and every gait reference in the literature reports
        // foot pitch. Rather than drop the comparison, the foot's own world
        // orientation is projected forward by a nominal length. ONLY THE
        // DIRECTION IS USED downstream (foot pitch is an angle), so the length
        // is a scale that cancels; it is not a claim about foot size.
        const float toeLen = 0.11f * height;
        for (const Joint j : {Joint::FootL, Joint::FootR}) {
            const Mat4& m = world[static_cast<size_t>(j)];
            std::fprintf(f, "%s[%.6f, %.6f, %.6f]", j == Joint::FootL ? "], \"toe\": [" : ", ",
                         m.m[12] - m.m[8] * toeLen, m.m[13] - m.m[9] * toeLen,
                         m.m[14] - m.m[10] * toeLen);
        }
        std::fprintf(f, "]}%s\n", i + 1 < frames ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");

    if (out) {
        std::fclose(f);
        std::fprintf(stderr, "wrote %s: %d frames at %.1f m/s\n", out, frames, speed);
    }
    return 0;
}
