#include "mge/framework/asset_registry.h"

#include <cmath>

#include "mge/core/log.h"
#include "mge/graphics/primitives.h"

namespace mge {

namespace {
constexpr const char* kTag = "assets";

LodMesh makePlaceholderVolume(const VirtualModelDesc& desc) {
    LodMesh mesh;
    const Vec3& p = desc.proportions;
    switch (desc.shape) {
        case PlaceholderShape::Box:
            mesh.lods.push_back(makeBox(p));
            break;
        case PlaceholderShape::Cylinder:
            mesh.lods.push_back(makeCylinder(0.5f * std::fmax(p.x, p.z), p.y));
            break;
        case PlaceholderShape::Capsule:
            mesh.lods.push_back(makeCapsule(0.5f * std::fmax(p.x, p.z), p.y));
            break;
    }
    mesh.computeBounds();
    return mesh;
}
}  // namespace

AssetId AssetRegistry::registerMesh(const char* name, LodMesh mesh) {
    const AssetId id = assetIdFromName(name);
    auto it = records_.find(id);
    if (it != records_.end()) {
        if (it->second->name != name) {
            MGE_LOGE(kTag, "asset id collision: '%s' vs '%s'", name, it->second->name.c_str());
            return kInvalidAsset;
        }
        it->second->mesh = std::move(mesh);  // re-registration updates content
        return id;
    }
    auto record = std::make_unique<AssetRecord>();
    record->id = id;
    record->name = name;
    record->kind = AssetKind::Mesh;
    record->mesh = std::move(mesh);
    records_.emplace(id, std::move(record));
    return id;
}

AssetId AssetRegistry::registerVirtualModel(const char* name, VirtualModelDesc desc) {
    const AssetId id = assetIdFromName(name);
    if (records_.count(id) != 0) {
        MGE_LOGW(kTag, "virtual model '%s' already registered", name);
        return id;
    }
    auto record = std::make_unique<AssetRecord>();
    record->id = id;
    record->name = name;
    record->kind = AssetKind::VirtualModel;
    record->mesh = makePlaceholderVolume(desc);
    record->virtualDesc = std::move(desc);
    records_.emplace(id, std::move(record));
    return id;
}

bool AssetRegistry::fulfill(AssetId id, LodMesh mesh) {
    auto it = records_.find(id);
    if (it == records_.end() || it->second->kind != AssetKind::VirtualModel) return false;

    mesh.computeBounds();
    const Vec3 real = mesh.bounds.max - mesh.bounds.min;
    const Vec3& declared = it->second->virtualDesc.proportions;
    auto off = [](float a, float b) { return b > 0.01f && std::fabs(a - b) / b > 0.25f; };
    if (off(real.x, declared.x) || off(real.y, declared.y) || off(real.z, declared.z)) {
        MGE_LOGW(kTag,
                 "fulfilling '%s': real size %.2fx%.2fx%.2f deviates >25%% from declared "
                 "%.2fx%.2fx%.2f",
                 it->second->name.c_str(), real.x, real.y, real.z, declared.x, declared.y,
                 declared.z);
    }
    it->second->mesh = std::move(mesh);
    it->second->kind = AssetKind::Mesh;  // fulfilled: now a real asset, same id
    MGE_LOGI(kTag, "fulfilled virtual model '%s'", it->second->name.c_str());
    return true;
}

void AssetRegistry::unload(AssetId id) {
    auto it = records_.find(id);
    if (it == records_.end() || it->second->kind != AssetKind::Mesh) return;
    it->second->mesh = LodMesh{};
}

const AssetRecord* AssetRegistry::find(AssetId id) const {
    auto it = records_.find(id);
    return it != records_.end() ? it->second.get() : nullptr;
}

void AssetRegistry::unfulfilled(std::vector<const AssetRecord*>& out) const {
    for (const auto& [id, record] : records_) {
        if (record->kind == AssetKind::VirtualModel) out.push_back(record.get());
    }
}

}  // namespace mge
