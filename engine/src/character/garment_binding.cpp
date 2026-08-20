#include "mge/character/garment_binding.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "mge/core/jobs.h"
#include "mge/core/log.h"

namespace mge {

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void hashBytes(const void* data, size_t size, uint64_t& h) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime;
    }
}

// Positions are hashed through their bit pattern: two builds that differ by
// one ulp ARE a different body as far as a baked binding is concerned, and
// saying so loudly is the entire point of the contract hash (B-14).
void hashFloat(float f, uint64_t& h) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    hashBytes(&bits, sizeof bits, h);
}

void hashVec3(const Vec3& v, uint64_t& h) {
    hashFloat(v.x, h);
    hashFloat(v.y, h);
    hashFloat(v.z, h);
}

// An orthonormal frame carried by a triangle: the surface normal plus a
// tangent taken from the triangle's first edge. Degenerate triangles fall back
// to a fixed frame so a bad triangle cannot produce NaNs downstream.
struct TriangleFrame {
    Vec3 origin{0, 0, 0};
    Vec3 axis[3];  // 0 = tangent, 1 = bitangent, 2 = normal
};

TriangleFrame triangleFrame(const Vec3& a, const Vec3& b, const Vec3& c) {
    TriangleFrame f;
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    Vec3 n = e1.cross(e2);
    if (n.lengthSq() <= 1e-20f) {
        f.axis[0] = Vec3{1, 0, 0};
        f.axis[1] = Vec3{0, 0, 1};
        f.axis[2] = Vec3{0, 1, 0};
        return f;
    }
    n = n.normalized();
    Vec3 t = e1;
    if (t.lengthSq() <= 1e-20f) t = Vec3{1, 0, 0};
    t = (t - n * n.dot(t));
    t = t.lengthSq() > 1e-20f ? t.normalized() : Vec3{1, 0, 0};
    f.axis[0] = t;
    f.axis[1] = n.cross(t);
    f.axis[2] = n;
    return f;
}

}  // namespace

// ------------------------------------------------------------ the hash -----

uint64_t skinnedMeshContentHash(const SkinnedMeshData& mesh) {
    uint64_t h = kFnvOffset;
    const uint64_t vertexCount = mesh.vertices.size();
    const uint64_t indexCount = mesh.indices.size();
    const uint64_t partCount = mesh.parts.size();
    hashBytes(&vertexCount, sizeof vertexCount, h);
    hashBytes(&indexCount, sizeof indexCount, h);
    hashBytes(&partCount, sizeof partCount, h);
    for (const SkinVertex& v : mesh.vertices) {
        hashVec3(v.position, h);
        hashVec3(v.normal, h);
        hashBytes(v.uv, sizeof v.uv, h);
        hashBytes(v.joints, sizeof v.joints, h);
        hashBytes(v.weights, sizeof v.weights, h);
    }
    for (uint32_t index : mesh.indices) hashBytes(&index, sizeof index, h);
    for (const MeshPart& part : mesh.parts) {
        const uint8_t region = static_cast<uint8_t>(part.region);
        hashBytes(&region, sizeof region, h);
        hashBytes(&part.firstIndex, sizeof part.firstIndex, h);
        hashBytes(&part.indexCount, sizeof part.indexCount, h);
    }
    return h;
}

// ---------------------------------------------------------- the binding ----

const char* bindingCheckReason(BindingCheck check) {
    switch (check) {
        case BindingCheck::Ok: return "ok";
        case BindingCheck::BaseHashMismatch:
            return "body content hash does not match the one this binding was baked against "
                   "- the body changed; re-bake the garment (ADR 0008)";
        case BindingCheck::BaseTopologyChanged:
            return "body vertex/triangle count differs from the bake - the body's topology "
                   "changed; re-bake the garment (ADR 0008)";
        case BindingCheck::GarmentSizeMismatch:
            return "binding has a different vertex count than the garment mesh - this binding "
                   "belongs to another garment";
        case BindingCheck::Empty: return "binding is empty";
    }
    return "unknown";
}

BindingCheck checkBinding(const GarmentBinding& binding, const SkinnedMeshData& base,
                          size_t garmentVertexCount) {
    if (binding.binds.empty()) return BindingCheck::Empty;
    if (binding.binds.size() != garmentVertexCount) return BindingCheck::GarmentSizeMismatch;
    if (binding.baseVertexCount != base.vertices.size() ||
        binding.baseTriangleCount != base.triangleCount()) {
        return BindingCheck::BaseTopologyChanged;
    }
    // The hash is the real gate; the counts above only give a sharper message
    // for the commonest way a body moves on.
    if (binding.baseHash != skinnedMeshContentHash(base)) return BindingCheck::BaseHashMismatch;
    return BindingCheck::Ok;
}

// ------------------------------------------------------- the evaluation ----

void morphedSurface(const SkinnedMeshData& mesh, const float weights[kMorphCount],
                    std::vector<Vec3>& outPosition, std::vector<Vec3>& outNormal) {
    const size_t count = mesh.vertices.size();
    outPosition.resize(count);
    outNormal.resize(count);
    for (size_t i = 0; i < count; ++i) {
        outPosition[i] = mesh.vertices[i].position;
        outNormal[i] = mesh.vertices[i].normal;
    }
    if (weights == nullptr) return;
    // Sparse by construction: a target visits only the vertices it moves, so a
    // face parameter costs a few dozen adds on a 1640-vertex body.
    for (const MorphTarget& target : mesh.morphs) {
        const float w = weights[static_cast<size_t>(target.morph)];
        if (w > -1e-4f && w < 1e-4f) continue;
        const float positionScale = w * target.scale * (1.0f / 32767.0f);
        const float normalScale = w * (1.0f / 127.0f);
        for (const MorphDelta& d : target.deltas) {
            if (d.vertex >= count) continue;
            Vec3& p = outPosition[d.vertex];
            p.x += static_cast<float>(d.position[0]) * positionScale;
            p.y += static_cast<float>(d.position[1]) * positionScale;
            p.z += static_cast<float>(d.position[2]) * positionScale;
            Vec3& n = outNormal[d.vertex];
            n.x += static_cast<float>(d.normal[0]) * normalScale;
            n.y += static_cast<float>(d.normal[1]) * normalScale;
            n.z += static_cast<float>(d.normal[2]) * normalScale;
        }
    }
    for (Vec3& n : outNormal) {
        n = n.lengthSq() > 1e-12f ? n.normalized() : Vec3{0, 1, 0};
    }
}

bool applyBinding(const GarmentBinding& binding, const SkinnedMeshData& base,
                  const std::vector<Vec3>& basePosition, const std::vector<Vec3>& baseNormal,
                  std::vector<SkinVertex>& outVertices) {
    if (binding.binds.size() != outVertices.size()) return false;
    if (basePosition.size() != base.vertices.size()) return false;
    if (baseNormal.size() != base.vertices.size()) return false;

    const float scale = binding.offsetScale * (1.0f / 32767.0f);
    const size_t triangleCount = base.triangleCount();

    for (size_t i = 0; i < binding.binds.size(); ++i) {
        const SurfaceBind& bind = binding.binds[i];
        if (bind.triangle >= triangleCount) continue;  // refused earlier; be safe
        const uint32_t i0 = base.indices[bind.triangle * 3 + 0];
        const uint32_t i1 = base.indices[bind.triangle * 3 + 1];
        const uint32_t i2 = base.indices[bind.triangle * 3 + 2];
        if (i0 >= basePosition.size() || i1 >= basePosition.size() || i2 >= basePosition.size()) {
            continue;
        }
        const Vec3& a = basePosition[i0];
        const Vec3& b = basePosition[i1];
        const Vec3& c = basePosition[i2];

        const float u = static_cast<float>(bind.bary[0]) * (1.0f / 65535.0f);
        const float v = static_cast<float>(bind.bary[1]) * (1.0f / 65535.0f);
        const float w = 1.0f - u - v;

        // The surface point this vertex is pinned to, then the stored offset
        // put back through the triangle's CURRENT frame — that is the whole
        // trick: the frame moved with the morph, so the garment did too.
        const Vec3 surface = a * w + b * u + c * v;
        const TriangleFrame frame = triangleFrame(a, b, c);
        const float ox = static_cast<float>(bind.offset[0]) * scale;
        const float oy = static_cast<float>(bind.offset[1]) * scale;
        const float oz = static_cast<float>(bind.offset[2]) * scale;
        const Vec3 offset = frame.axis[0] * ox + frame.axis[1] * oy + frame.axis[2] * oz;

        outVertices[i].position = surface + offset;
        // The garment's normal follows the surface normal it was bound to, so
        // lighting stays continuous across the body/garment seam.
        const Vec3 n = (baseNormal[i0] * w + baseNormal[i1] * u + baseNormal[i2] * v);
        outVertices[i].normal = n.lengthSq() > 1e-12f ? n.normalized() : frame.axis[2];
    }
    return true;
}

// ------------------------------------------------------------ the cache ----

uint64_t morphSetKey(const float weights[kMorphCount]) {
    uint64_t h = kFnvOffset;
    for (size_t i = 0; i < kMorphCount; ++i) {
        // Quantized to 1/1000 so floating-point noise in a variant does not
        // split the cache into near-identical entries.
        const int32_t q = static_cast<int32_t>(std::lround(weights[i] * 1000.0f));
        hashBytes(&q, sizeof q, h);
    }
    return h;
}

void GarmentFitCache::runFit(Entry& entry) {
    morphedSurface(*entry.base, entry.weights, entry.position, entry.normal);
    entry.vertices = entry.garment->vertices;
    const bool ok = applyBinding(*entry.binding, *entry.base, entry.position, entry.normal,
                                 entry.vertices);
    if (!ok) entry.vertices.clear();
    entry.state.store(static_cast<uint8_t>(State::Ready), std::memory_order_release);
}

GarmentFitCache::Status GarmentFitCache::request(uint32_t garmentId,
                                                 const GarmentBinding& binding,
                                                 const SkinnedMeshData& base,
                                                 const SkinnedMeshData& garment,
                                                 const float weights[kMorphCount],
                                                 const std::vector<SkinVertex>** out) {
    if (out != nullptr) *out = nullptr;

    const BindingCheck check = checkBinding(binding, base, garment.vertices.size());
    if (check != BindingCheck::Ok) {
        ++refused_;
        lastRefusal_ = bindingCheckReason(check);
        MGE_LOGE("wearables", "garment %u refused: %s", garmentId, lastRefusal_);
        return Status::Refused;
    }

    uint64_t key = kFnvOffset;
    hashBytes(&garmentId, sizeof garmentId, key);
    const uint64_t morphKey = morphSetKey(weights);
    hashBytes(&morphKey, sizeof morphKey, key);
    if (key == 0) key = 1;  // 0 marks a free slot

    ++clock_;

    // Hit?
    for (Entry& entry : entries_) {
        if (entry.key != key) continue;
        const State state = static_cast<State>(entry.state.load(std::memory_order_acquire));
        if (state == State::Ready) {
            entry.lastUse = clock_;
            if (entry.vertices.empty()) {
                ++refused_;
                lastRefusal_ = "re-fit produced no vertices";
                return Status::Refused;
            }
            if (out != nullptr) *out = &entry.vertices;
            return Status::Ready;
        }
        return Status::Pending;
    }

    // Miss: take a free slot, or evict the least recently used READY one.
    // A computing entry is never evicted — a job is reading it.
    Entry* slot = nullptr;
    for (Entry& entry : entries_) {
        if (entry.key == 0) {
            slot = &entry;
            break;
        }
    }
    if (slot == nullptr) {
        uint32_t oldest = 0;
        for (Entry& entry : entries_) {
            const State state = static_cast<State>(entry.state.load(std::memory_order_acquire));
            if (state != State::Ready) continue;
            if (slot == nullptr || entry.lastUse < oldest) {
                slot = &entry;
                oldest = entry.lastUse;
            }
        }
    }
    if (slot == nullptr) {
        // Every slot is mid-fit. Refuse rather than grow (P1); the caller
        // draws unrefitted and asks again.
        ++refused_;
        lastRefusal_ = "fit cache full - every slot is computing";
        return Status::Refused;
    }

    slot->key = key;
    slot->lastUse = clock_;
    slot->binding = &binding;
    slot->base = &base;
    slot->garment = &garment;
    for (size_t i = 0; i < kMorphCount; ++i) slot->weights[i] = weights[i];
    slot->state.store(static_cast<uint8_t>(State::Computing), std::memory_order_release);

    if (jobs_ == nullptr) {
        runFit(*slot);
        const State state = static_cast<State>(slot->state.load(std::memory_order_acquire));
        if (state == State::Ready && !slot->vertices.empty()) {
            if (out != nullptr) *out = &slot->vertices;
            return Status::Ready;
        }
        ++refused_;
        lastRefusal_ = "re-fit produced no vertices";
        return Status::Refused;
    }

    Entry* entry = slot;
    if (!jobs_->submit(Lane::Decode, [entry] { GarmentFitCache::runFit(*entry); })) {
        // The lane is full: give the slot back and let the caller retry. The
        // frame is never blocked for a garment fit.
        slot->key = 0;
        slot->state.store(static_cast<uint8_t>(State::Free), std::memory_order_release);
        ++refused_;
        lastRefusal_ = "decode lane full - retry next tick";
        return Status::Refused;
    }
    return Status::Pending;
}

void GarmentFitCache::reset() {
    for (Entry& entry : entries_) {
        entry.key = 0;
        entry.lastUse = 0;
        entry.binding = nullptr;
        entry.base = nullptr;
        entry.garment = nullptr;
        entry.vertices.clear();
        entry.position.clear();
        entry.normal.clear();
        entry.state.store(static_cast<uint8_t>(State::Free), std::memory_order_release);
    }
    clock_ = 0;
    refused_ = 0;
    lastRefusal_ = "";
}

size_t GarmentFitCache::residentCount() const {
    size_t count = 0;
    for (const Entry& entry : entries_) {
        if (entry.key != 0) ++count;
    }
    return count;
}

// ---------------------------------------------------------------- I/O ------

namespace {

constexpr uint32_t kBindingMagic = 0x54494647u;  // "GFIT"
constexpr uint32_t kBindingVersion = 1;

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

template <typename T>
bool take(const uint8_t*& cursor, const uint8_t* end, T& value) {
    if (static_cast<size_t>(end - cursor) < sizeof(T)) return false;
    std::memcpy(&value, cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

}  // namespace

void serializeBinding(const GarmentBinding& binding, std::vector<uint8_t>& out) {
    out.clear();
    put(out, kBindingMagic);
    put(out, kBindingVersion);
    put(out, binding.baseHash);
    put(out, binding.rootBodyHash);
    put(out, binding.offsetScale);
    put(out, binding.baseVertexCount);
    put(out, binding.baseTriangleCount);
    put(out, static_cast<uint32_t>(binding.layer));
    put(out, static_cast<uint32_t>(binding.binds.size()));
    for (const SurfaceBind& bind : binding.binds) put(out, bind);
}

bool deserializeBinding(const uint8_t* data, size_t size, GarmentBinding& out) {
    out.clear();
    if (data == nullptr) return false;
    const uint8_t* cursor = data;
    const uint8_t* end = data + size;
    uint32_t magic = 0, version = 0, layer = 0, count = 0;
    if (!take(cursor, end, magic) || magic != kBindingMagic) return false;
    if (!take(cursor, end, version) || version != kBindingVersion) return false;
    if (!take(cursor, end, out.baseHash)) return false;
    if (!take(cursor, end, out.rootBodyHash)) return false;
    if (!take(cursor, end, out.offsetScale)) return false;
    if (!take(cursor, end, out.baseVertexCount)) return false;
    if (!take(cursor, end, out.baseTriangleCount)) return false;
    if (!take(cursor, end, layer)) return false;
    if (!take(cursor, end, count)) return false;
    out.layer = static_cast<uint8_t>(layer);
    if (static_cast<size_t>(end - cursor) < count * sizeof(SurfaceBind)) return false;
    out.binds.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!take(cursor, end, out.binds[i])) return false;
    }
    return true;
}

bool writeBindingFile(const char* path, const GarmentBinding& binding) {
    std::vector<uint8_t> bytes;
    serializeBinding(binding, bytes);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) return false;
    const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    return written == bytes.size();
}

bool readBindingFile(const char* path, GarmentBinding& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(file);
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (read != bytes.size()) return false;
    return deserializeBinding(bytes.data(), bytes.size(), out);
}

}  // namespace mge
