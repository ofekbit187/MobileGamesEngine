// mge_garment_fit — bakes the surface bindings the runtime fits garments with
// (Phase 13, ADR 0008).
//
// This is the whole content pipeline for a wearable, and per P12 it is meant
// to be run by whoever made the garment, not by an engineer: point it at the
// body and a garment, read the pass/fail line, fix what it names. Output is a
// `.mgefit` next to the `.mgeskin` it belongs to.
//
// Run with no arguments to re-bake every shipped garment — which is what a
// body change requires, and what `garment_bindings_match_the_shipped_body`
// fails to remind you of.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/character/garment_binding.h"
#include "mge/graphics/mesh_io.h"
#include "mge/import/garment_fit.h"

using namespace mge;

namespace {

#ifndef MGE_ASSET_MODEL_DIR
#define MGE_ASSET_MODEL_DIR "assets/models"
#endif

struct Shipped {
    const char* asset;
    uint8_t layer;
};

// The catalogue as it stands. A garment is data: adding one here is a data
// change, and adding one to the ENGINE is not required at all (P12) — this
// list exists only so "re-bake everything" has something to iterate.
const Shipped kShipped[] = {
    {"garment_tunic", 1},   {"garment_trousers", 1}, {"garment_boots", 1},
    {"garment_hair_short", 1}, {"garment_hair_long", 1}, {"garment_armour", 2},
};

std::string modelPath(const std::string& dir, const std::string& name, const char* ext) {
    return dir + "/" + name + ext;
}

bool bakeOne(const std::string& dir, const SkinnedMeshData& body, const std::string& name,
             uint8_t layer, bool& outOk) {
    SkinnedMeshData garment;
    if (!readSkinnedMeshFile(modelPath(dir, name, ".mgeskin").c_str(), garment)) {
        std::printf("  %-20s MISSING (%s.mgeskin)\n", name.c_str(), name.c_str());
        outOk = false;
        return false;
    }

    FitParams params;
    GarmentBinding binding;
    FitReport report;
    const bool ok = fitGarment(body, garment, layer, params, binding, report);
    std::printf("  %-20s %s\n", name.c_str(), report.message);
    if (!ok) {
        outOk = false;
        return false;
    }

    if (!writeBindingFile(modelPath(dir, name, ".mgefit").c_str(), binding)) {
        std::printf("  %-20s FAILED to write %s.mgefit\n", name.c_str(), name.c_str());
        outOk = false;
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : MGE_ASSET_MODEL_DIR;
    setCharacterAssetDir(dir.c_str());

    SkinnedMeshData body;
    if (!readSkinnedMeshFile(modelPath(dir, "humanoid_template_lod0", ".mgeskin").c_str(),
                             body)) {
        std::printf("no template body in %s\n", dir.c_str());
        return 1;
    }

    std::printf("body: %zu vertices, %zu triangles, hash %016llx\n", body.vertices.size(),
                body.triangleCount(),
                static_cast<unsigned long long>(skinnedMeshContentHash(body)));
    std::printf("baking %zu garment bindings into %s\n\n",
                sizeof kShipped / sizeof kShipped[0], dir.c_str());

    bool ok = true;
    for (const Shipped& item : kShipped) bakeOne(dir, body, item.asset, item.layer, ok);

    std::printf("\n%s\n", ok ? "all bindings baked" : "SOME BINDINGS FAILED");
    return ok ? 0 : 1;
}
