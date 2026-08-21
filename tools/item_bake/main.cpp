// mge_item_bake — bakes the engine's shipped held-item meshes to `.mgemesh`.
//
// Held items are ordinary static meshes: anything an artist exports through
// `mge_asset_import` works, and nothing about a held item is compiled into the
// engine (task 13.12 applies here too). This tool exists only because the
// engine ships a sword and there is no artist asset for it yet — it composes
// one from primitives ONCE, offline, and commits the result. The runtime reads
// the `.mgemesh` and knows nothing about how it was made.
//
// The sword is modelled in GRIP SPACE: the origin is where the hand closes on
// it, and the blade runs down -Y. That convention is what lets the item's grip
// meet the hand's socket with no per-item animation (CHARACTERS.md §6.1).

#include <cstdio>
#include <string>

#include "mge/graphics/mesh_data.h"
#include "mge/graphics/mesh_io.h"
#include "mge/graphics/primitives.h"

using namespace mge;

namespace {

#ifndef MGE_ASSET_MODEL_DIR
#define MGE_ASSET_MODEL_DIR "assets/models"
#endif

// An arming sword: grip, guard, blade. Proportions are real — a 0.95 m
// weapon with a 0.62 m blade — because MODELING.md §1 measures rather than
// eyeballs, and a sword that is secretly toy-sized reads as one.
MeshData buildSword() {
    MeshData sword;
    // Grip: the hand closes here, so it straddles the origin.
    appendMesh(sword, makeCylinder(0.016f, 0.15f, 10), {0, 0.02f, 0});
    // Pommel, just below the hand.
    appendMesh(sword, makeBox({0.034f, 0.030f, 0.034f}), {0, -0.075f, 0});
    // Crossguard, above the fist where the blade begins.
    appendMesh(sword, makeBox({0.15f, 0.022f, 0.035f}), {0, 0.105f, 0});
    // Blade, running up +Y away from the hand.
    appendMesh(sword, makeBox({0.045f, 0.62f, 0.014f}), {0, 0.43f, 0});
    sword.computeBounds();
    return sword;
}

bool write(const std::string& dir, const char* name, const MeshData& mesh) {
    LodMesh lod;
    lod.lods.push_back(mesh);
    lod.computeBounds();
    const std::string path = dir + "/" + name + ".mgemesh";
    if (!writeMeshFile(path.c_str(), lod)) {
        std::printf("  %-16s FAILED to write %s\n", name, path.c_str());
        return false;
    }
    std::printf("  %-16s %zu triangles, %.2f m tall -> %s.mgemesh\n", name,
                mesh.indices.size() / 3, mesh.bounds.max.y - mesh.bounds.min.y, name);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : MGE_ASSET_MODEL_DIR;
    std::printf("baking held-item meshes into %s\n\n", dir.c_str());

    bool ok = true;
    ok = write(dir, "item_sword", buildSword()) && ok;

    std::printf("\n%s\n", ok ? "done" : "SOME ITEMS FAILED");
    return ok ? 0 : 1;
}
