#include "mge/framework/virtual_models.h"

#include <cstdio>
#include <vector>

#include "mge/core/log.h"
#include "mge/graphics/mesh_io.h"

namespace mge {

namespace {
constexpr const char* kTag = "fulfill";

void jsonEscape(const std::string& in, std::string& out) {
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20 || c < 0) out += c;
        }
    }
}

const char* shapeName(PlaceholderShape shape) {
    switch (shape) {
        case PlaceholderShape::Box: return "box";
        case PlaceholderShape::Cylinder: return "cylinder";
        case PlaceholderShape::Capsule: return "capsule";
    }
    return "box";
}
}  // namespace

bool validateVirtualModelDesc(const VirtualModelDesc& desc, std::string* error) {
    auto fail = [&](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (desc.description.empty()) return fail("description is empty — an agent can't build it");
    if (desc.description.size() < 8) return fail("description too short to be buildable");
    const Vec3& p = desc.proportions;
    if (p.x < 0.01f || p.y < 0.01f || p.z < 0.01f) return fail("proportions must be positive");
    if (p.x > 500 || p.y > 500 || p.z > 500) return fail("proportions implausibly large (>500m)");
    return true;
}

std::string fulfillmentFileName(AssetId id) {
    char name[32];
    snprintf(name, sizeof(name), "%016llx.mgemesh", static_cast<unsigned long long>(id));
    return name;
}

size_t exportManifest(const AssetRegistry& assets, const char* jsonPath) {
    std::vector<const AssetRecord*> pending;
    assets.unfulfilled(pending);

    std::string json = "{\n  \"format\": \"mge-virtual-model-manifest\",\n  \"version\": 1,\n"
                       "  \"delivery\": \"bake each model to <id>.mgemesh (glTF via "
                       "mge_asset_import) at the given proportions, origin at the base "
                       "center\",\n  \"models\": [\n";
    for (size_t i = 0; i < pending.size(); ++i) {
        const AssetRecord* record = pending[i];
        const VirtualModelDesc& desc = record->virtualDesc;
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "    {\n      \"id\": \"%016llx\",\n      \"file\": \"%s\",\n",
                 static_cast<unsigned long long>(record->id),
                 fulfillmentFileName(record->id).c_str());
        json += buffer;
        auto field = [&](const char* key, const std::string& value, bool comma = true) {
            json += "      \"";
            json += key;
            json += "\": \"";
            jsonEscape(value, json);
            json += comma ? "\",\n" : "\"\n";
        };
        field("name", record->name);
        snprintf(buffer, sizeof(buffer),
                 "      \"proportions_m\": [%.3f, %.3f, %.3f],\n      \"shape\": \"%s\",\n"
                 "      \"collidable\": %s,\n",
                 desc.proportions.x, desc.proportions.y, desc.proportions.z,
                 shapeName(desc.shape), desc.collidable ? "true" : "false");
        json += buffer;
        field("description", desc.description);
        field("style", desc.style);
        field("materials", desc.materials);
        field("features", desc.features, false);
        json += i + 1 < pending.size() ? "    },\n" : "    }\n";
    }
    json += "  ]\n}\n";

    FILE* f = fopen(jsonPath, "wb");
    if (f == nullptr) {
        MGE_LOGE(kTag, "cannot write manifest: %s", jsonPath);
        return 0;
    }
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    MGE_LOGI(kTag, "manifest: %zu unfulfilled virtual model(s) -> %s", pending.size(), jsonPath);
    return pending.size();
}

size_t fulfillFromDirectory(AssetRegistry& assets, const char* directory) {
    std::vector<const AssetRecord*> pending;
    assets.unfulfilled(pending);

    size_t fulfilled = 0;
    for (const AssetRecord* record : pending) {
        const std::string path =
            std::string(directory) + "/" + fulfillmentFileName(record->id);
        LodMesh mesh;
        if (!readMeshFile(path.c_str(), mesh)) continue;  // not delivered yet — fine
        if (assets.fulfill(record->id, std::move(mesh))) {
            ++fulfilled;
        }
    }
    return fulfilled;
}

}  // namespace mge
