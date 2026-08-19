// Phase 7: description validation, manifest export, directory fulfillment.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "mge/framework/virtual_models.h"
#include "mge/graphics/mesh_io.h"
#include "mge/graphics/primitives.h"
#include "test_framework.h"

using namespace mge;

namespace {
std::string tmpDir() {
    std::string dir = "/tmp";
    if (const char* t = getenv("TMPDIR")) dir = t;
    return dir;
}

VirtualModelDesc stallDesc() {
    VirtualModelDesc desc;
    desc.proportions = {2.4f, 2.1f, 1.8f};
    desc.description = "wooden market stall with an open front counter";
    desc.style = "rustic medieval, hand-hewn";
    desc.materials = "oak, canvas, rope";
    desc.features = "striped canvas roof, worn planks";
    return desc;
}

std::string readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return "";
    std::string out;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    fclose(f);
    return out;
}
}  // namespace

MGE_TEST(virtual_desc_validation) {
    VirtualModelDesc good = stallDesc();
    std::string error;
    MGE_CHECK(validateVirtualModelDesc(good, &error));

    VirtualModelDesc empty = good;
    empty.description = "";
    MGE_CHECK(!validateVirtualModelDesc(empty, &error));
    MGE_CHECK(!error.empty());

    VirtualModelDesc flat = good;
    flat.proportions.y = 0.0f;
    MGE_CHECK(!validateVirtualModelDesc(flat, &error));

    VirtualModelDesc huge = good;
    huge.proportions.x = 900.0f;
    MGE_CHECK(!validateVirtualModelDesc(huge, &error));
}

MGE_TEST(manifest_export_and_directory_fulfillment) {
    AssetRegistry assets;
    const AssetId stallId = assets.registerVirtualModel("prop/market_stall", stallDesc());
    VirtualModelDesc wellDesc;
    wellDesc.proportions = {1.8f, 1.4f, 1.8f};
    wellDesc.shape = PlaceholderShape::Cylinder;
    wellDesc.description = "stone village well with a wooden crank";
    const AssetId wellId = assets.registerVirtualModel("prop/well", wellDesc);
    // A real mesh asset must NOT appear in the manifest.
    LodMesh box;
    box.lods.push_back(makeBox({1, 1, 1}));
    box.computeBounds();
    assets.registerMesh("prop/crate", std::move(box));

    // Export (task 7.5): both unfulfilled virtuals, structured fields intact.
    const std::string manifestPath = tmpDir() + "/manifest.json";
    MGE_CHECK(exportManifest(assets, manifestPath.c_str()) == 2);
    const std::string json = readFile(manifestPath);
    MGE_CHECK(json.find("mge-virtual-model-manifest") != std::string::npos);
    MGE_CHECK(json.find("prop/market_stall") != std::string::npos);
    MGE_CHECK(json.find("striped canvas roof, worn planks") != std::string::npos);
    MGE_CHECK(json.find("oak, canvas, rope") != std::string::npos);
    MGE_CHECK(json.find("\"shape\": \"cylinder\"") != std::string::npos);
    MGE_CHECK(json.find(fulfillmentFileName(stallId)) != std::string::npos);
    MGE_CHECK(json.find("prop/crate") == std::string::npos);

    // The "agent" delivers only the stall.
    LodMesh delivered;
    delivered.lods.push_back(makeBox({2.4f, 2.1f, 1.8f}));
    delivered.computeBounds();
    const std::string deliveryPath = tmpDir() + "/" + fulfillmentFileName(stallId);
    MGE_CHECK(writeMeshFile(deliveryPath.c_str(), delivered));

    MGE_CHECK(fulfillFromDirectory(assets, tmpDir().c_str()) == 1);
    MGE_CHECK(assets.find(stallId)->kind == AssetKind::Mesh);       // fulfilled
    MGE_CHECK(assets.find(wellId)->kind == AssetKind::VirtualModel);  // still pending

    // Re-export: only the well remains.
    MGE_CHECK(exportManifest(assets, manifestPath.c_str()) == 1);
    remove(deliveryPath.c_str());
    remove(manifestPath.c_str());
}

MGE_TEST(append_mesh_composition) {
    MeshData composite = makeBox({2, 0.2f, 2});          // table top
    appendMesh(composite, makeBox({0.2f, 1, 0.2f}), {0.8f, -0.6f, 0.8f});  // a leg
    MGE_CHECK(composite.vertices.size() == 48);
    MGE_CHECK(composite.indices.size() == 72);
    // Indices of the appended part reference appended vertices.
    for (uint32_t i : composite.indices) MGE_CHECK(i < composite.vertices.size());
    MGE_CHECK_NEAR(composite.bounds.min.y, -1.1f, 1e-5);
}
