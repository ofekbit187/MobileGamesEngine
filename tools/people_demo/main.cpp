// people_demo: the Phase 9 proof tool. Generates a family tree and shows,
// with real engine output (● for the review board):
//   1. the tree itself — derived names under the owner's naming rulings,
//      record-only ancestors, relations (printed)
//   2. people_family.ppm — the living family rendered: parents in the back
//      row, their children in front, every body derived from inherited DNA
//   3. the voice pipeline — en + he line folders (never mixed), manifest
//      export, simulated agent delivery, random take pickup (printed)
//
// Usage: mge_people_demo [outputDir]

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "mge/audio/wav.h"
#include "mge/character/humanoid.h"
#include "mge/core/memory.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"
#include "mge/people/family_tree.h"
#include "mge/people/voice.h"

using namespace mge;

namespace {

struct RigInstance {
    Skeleton skeleton;
    std::vector<RigPart> parts;
    std::vector<GpuLodMesh> gpu;
};

bool uploadRig(Renderer& renderer, const HumanoidVariant& variant,
               const WearableInstance* wearables, size_t wearableCount, RigInstance& out) {
    out.skeleton = buildSkeleton(variant);
    buildHumanoidVisual(variant, wearables, wearableCount, out.parts);
    out.gpu.resize(out.parts.size());
    for (size_t i = 0; i < out.parts.size(); ++i) {
        LodMesh lod;
        lod.lods.push_back(out.parts[i].mesh);
        lod.computeBounds();
        if (!renderer.uploadLodMesh(lod, out.gpu[i])) return false;
    }
    return true;
}

void destroyRig(Renderer& renderer, RigInstance& rig) {
    for (GpuLodMesh& mesh : rig.gpu) renderer.destroyLodMesh(mesh);
    rig.gpu.clear();
}

void emitRig(std::vector<DrawItem>& items, const RigInstance& rig, const Pose& pose,
             const Vec3& position, float yaw) {
    Mat4 world[kJointCount];
    evaluatePose(rig.skeleton, pose, world);
    const Mat4 root =
        Mat4::translation(position) * Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, -yaw));
    for (size_t i = 0; i < rig.parts.size(); ++i) {
        DrawItem item;
        item.mesh = &rig.gpu[i];
        item.model = root * world[static_cast<size_t>(rig.parts[i].joint)];
        item.worldBounds = Aabb::fromCenterExtents(position + Vec3{0, 1.2f, 0}, {3, 3, 3});
        item.lodReference = position;
        memcpy(item.baseColor, rig.parts[i].color, sizeof item.baseColor);
        items.push_back(item);
    }
}

void removeRecursive(const std::string& path) {
    if (DIR* dir = opendir(path.c_str())) {
        while (dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.' &&
                (entry->d_name[1] == '\0' ||
                 (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
                continue;
            }
            removeRecursive(path + "/" + entry->d_name);
        }
        closedir(dir);
        rmdir(path.c_str());
    } else {
        remove(path.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";

    // ---- 1. The tree: names, rulings, relations ----------------------------
    FamilyTree tree;
    TreeGenParams params;
    params.seed = 7;
    params.generations = 3;
    params.livingGenerations = 2;
    if (!tree.generate(params, cultureAnglo())) return 1;
    const auto& people = tree.people();

    printf("family tree, seed %u, %zu people:\n", tree.seed(), people.size());
    for (uint16_t i = 0; i < people.size(); ++i) {
        const PersonRecord& p = people[i];
        std::string relations;
        if (p.spouse != kNoPerson) relations += " = " + tree.fullNameOf(p.spouse);
        std::vector<uint16_t> children;
        tree.childrenOf(i, children);
        if (!children.empty() && p.sex == Sex::Male) {
            relations += "; children:";
            for (uint16_t child : children) {
                relations += " ";
                relations += tree.firstNameOf(child);
            }
        }
        printf("  gen %u %s %-24s%s%s  voice: %s\n", p.generation,
               p.alive ? " " : "+", tree.fullNameOf(i).c_str(),
               p.marriedIn ? " (married in)" : "", relations.c_str(), p.voiceDesc);
    }

    // Determinism spot-check: the same seed replays the same family.
    FamilyTree again;
    if (!again.generate(params, cultureAnglo()) ||
        again.people().size() != people.size() ||
        memcmp(again.people().data(), people.data(),
               people.size() * sizeof(PersonRecord)) != 0) {
        fprintf(stderr, "FAIL: tree generation is not deterministic\n");
        return 1;
    }

    // ---- 2. Render the living family (bodies derived from DNA) -------------
    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    printf("device: %s\n", device.deviceName());
    Renderer renderer(device);
    RendererConfig config;
    config.width = 1280;
    config.height = 720;
    config.lightDir[0] = -0.30f;
    config.lightDir[1] = 0.85f;
    config.lightDir[2] = 0.45f;
    if (!renderer.init(config)) return 1;

    GpuLodMesh ground;
    {
        LodMesh m;
        m.lods.push_back(makePlane(70, 70));
        m.computeBounds();
        if (!renderer.uploadLodMesh(m, ground)) return 1;
    }

    std::vector<DrawItem> items;
    items.push_back([&] {
        DrawItem item;
        item.mesh = &ground;
        item.worldBounds = Aabb::fromCenterExtents({0, 0, 0}, {40, 1, 40});
        item.baseColor[0] = 0.44f;
        item.baseColor[1] = 0.48f;
        item.baseColor[2] = 0.37f;
        return item;
    }());

    // Clusters: each generation-1 couple with their children in front.
    std::vector<RigInstance> rigs;
    rigs.reserve(people.size());
    LocomotionAnimator idle;
    for (int i = 0; i < 80; ++i) idle.update(1.0f / 60.0f, 0.0f);
    Pose pose;
    idle.samplePose(pose);

    std::vector<uint16_t> couples;  // blood gen-1 people with spouses
    for (uint16_t i = 0; i < people.size(); ++i) {
        if (people[i].generation == 1 && !people[i].marriedIn &&
            people[i].spouse != kNoPerson) {
            couples.push_back(i);
        }
    }
    int rendered = 0;
    const float clusterSpan = 8.5f;
    const float clusterBase = -clusterSpan * 0.5f * (couples.size() - 1);
    for (size_t c = 0; c < couples.size(); ++c) {
        const uint16_t bloodParent = couples[c];
        const uint16_t spouse = people[bloodParent].spouse;
        const float cx = clusterBase + clusterSpan * c;
        const auto renderPerson = [&](uint16_t index, Vec3 position) -> bool {
            rigs.emplace_back();
            const HumanoidVariant variant =
                variantOf(people[index].genome, people[index].sex);
            if (!uploadRig(renderer, variant, nullptr, 0, rigs.back())) return false;
            emitRig(items, rigs.back(), pose, position, kPi);
            ++rendered;
            return true;
        };
        if (!renderPerson(bloodParent, {cx - 1.0f, 0, -3.6f})) return 1;
        if (!renderPerson(spouse, {cx + 1.0f, 0, -3.6f})) return 1;
        std::vector<uint16_t> children;
        tree.childrenOf(people[bloodParent].sex == Sex::Male ? bloodParent : spouse,
                        children);
        for (size_t k = 0; k < children.size(); ++k) {
            const float kx = cx + (static_cast<float>(k) - (children.size() - 1) * 0.5f) * 1.9f;
            if (!renderPerson(children[k], {kx, 0, 0.6f})) return 1;
        }
    }
    printf("rendered %d living family members (%zu couples)\n", rendered, couples.size());

    Camera camera;
    camera.eye = {0.0f, 3.3f, 10.6f};
    camera.target = {0.0f, 0.9f, -1.4f};
    camera.aspect = static_cast<float>(config.width) / config.height;
    RenderStats stats;
    if (!renderer.renderFrame(camera, items.data(), items.size(), &stats)) return 1;
    const size_t pixelBytes = static_cast<size_t>(config.width) * config.height * 4;
    std::vector<uint8_t> pixels(pixelBytes);
    if (!renderer.readback(pixels.data(), pixels.size())) return 1;
    size_t nonSky = 0;
    for (size_t i = 0; i < pixelBytes; i += 4) {
        if (pixels[i] != 135 || pixels[i + 1] != 168 || pixels[i + 2] != 214) ++nonSky;
    }
    const double share = static_cast<double>(nonSky) / (pixelBytes / 4);
    const std::string capturePath = outDir + "/people_family.ppm";
    if (FILE* f = fopen(capturePath.c_str(), "wb")) {
        fprintf(f, "P6\n%u %u\n255\n", config.width, config.height);
        for (size_t i = 0; i < pixelBytes; i += 4) fwrite(&pixels[i], 1, 3, f);
        fclose(f);
    }
    printf("%s: drawn %u, non-sky %.1f%%\n", capturePath.c_str(), stats.drawn,
           share * 100.0);

    // ---- 3. Voices: two languages, never mixed; manifest; takes ------------
    const std::string voiceRoot = outDir + "/voices";
    removeRecursive(voiceRoot);  // a fresh pipeline every run
    uint32_t speaker = 0;
    for (uint16_t i = 0; i < people.size(); ++i) {
        if (!people[i].alive) continue;
        if (speaker == 0) speaker = people[i].personId;
        writeVoiceDescription(voiceRoot.c_str(), "en", people[i].personId,
                              people[i].voiceDesc);
        writeVoiceLine(voiceRoot.c_str(), "en", people[i].personId, "greeting", "warm",
                       "[sigh] Fine morning, friend. The mill's been busy.");
        writeVoiceDescription(voiceRoot.c_str(), "he", people[i].personId,
                              people[i].voiceDesc);
        writeVoiceLine(voiceRoot.c_str(), "he", people[i].personId, "greeting", "warm",
                       "\xd7\x91\xd7\x95\xd7\xa7\xd7\xa8 \xd7\x98\xd7\x95\xd7\x91, "
                       "\xd7\x99\xd7\x93\xd7\x99\xd7\x93\xd7\x99");
    }
    if (!validateVoices(voiceRoot.c_str(), "en", tree) ||
        !validateVoices(voiceRoot.c_str(), "he", tree)) {
        fprintf(stderr, "FAIL: a living person has no text line\n");
        return 1;
    }
    const size_t unrecordedBefore = exportVoiceManifest(
        voiceRoot.c_str(), "en", tree, (voiceRoot + "/en_manifest.json").c_str());

    // Simulated external agent: deliver two takes for one line.
    WavData wav;
    wav.sampleRate = 22050;
    wav.channels = 1;
    wav.samples.assign(22050, 800);  // 1 s placeholder tone
    const std::string lineDir =
        personVoiceDir(voiceRoot.c_str(), "en", speaker) + "/lines/greeting";
    if (!writeWav((lineDir + "/take1.wav").c_str(), wav) ||
        !writeWav((lineDir + "/take2.wav").c_str(), wav)) {
        return 1;
    }
    PersonVoice voice;
    if (!loadPersonVoice(voiceRoot.c_str(), "en", speaker, voice)) return 1;
    const std::string* take = nullptr;
    for (const VoiceLine& line : voice.lines) {
        if (line.id == "greeting") take = pickTake(line, tree.seed());
    }
    WavData delivered;
    if (take == nullptr || !loadWav(take->c_str(), delivered)) {
        fprintf(stderr, "FAIL: delivered take not picked up\n");
        return 1;
    }
    const size_t unrecordedAfter = exportVoiceManifest(
        voiceRoot.c_str(), "en", tree, (voiceRoot + "/en_manifest.json").c_str());
    printf("voices: %zu lines unrecorded -> agent delivers 2 takes -> %zu left; "
           "picked %s (%.1f s)\n",
           unrecordedBefore, unrecordedAfter, take->c_str(), delivered.seconds());

    // ---- cleanup: GPU budget must return to zero (P1) -----------------------
    for (RigInstance& rig : rigs) destroyRig(renderer, rig);
    renderer.destroyLodMesh(ground);
    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    const bool ok = rendered >= 6 && share > 0.3 && residual == 0 &&
                    unrecordedAfter == unrecordedBefore - 1;
    if (!ok) {
        fprintf(stderr, "FAIL: rendered=%d share=%.2f residual=%zu manifest %zu->%zu\n",
                rendered, share, residual, unrecordedBefore, unrecordedAfter);
        return 1;
    }
    printf("OK\n");
    return 0;
}
