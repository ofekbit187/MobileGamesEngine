#pragma once

// Garment surface binding (Phase 13, ADR 0008) — the baked data that makes a
// garment fit a body nobody generated.
//
// Until v3 the fitting guarantee held by construction: body and garment came
// out of the same profile code, so they could not drift apart. An imported
// artist mesh ends that. What replaces it is *baked data*: every garment
// vertex is stored as a position ON the body's surface — which triangle, where
// in that triangle, and how far off it — instead of as a position in space.
// Deform the body and the garment follows, because the garment's vertices are
// defined in terms of the body's.
//
// The split that makes this affordable (P1 + P12):
//   * BAKE (host, `mge/import/garment_fit.h`) — find the surface point, build
//     the binding, transfer the weights. Slow, thorough, done once per garment.
//   * RUNTIME (here) — evaluate the binding against a morphed body at spawn or
//     equip, on a job lane, cached per (garment, morph-set).
//   * FRAME — nothing. The refitted vertices are skinned by the same palette
//     as always: one pose evaluation, one palette upload, one skin pass.
//
// Bone-scale variants (height, bulk, shoulders) never reach this code at all —
// they ride the skinning palette and always have. Only *morph*-driven shape
// (belly, chest, seat) needs a re-fit, because a morph moves the surface the
// garment is bound to.

#include <atomic>
#include <cstdint>
#include <vector>

#include "mge/character/body_mesh.h"
#include "mge/core/math.h"

namespace mge {

class JobSystem;

// ------------------------------------------------------------ the hash -----

// Content hash of a skinned mesh — the identity a binding is baked against
// (task 13.3). ADR 0008: the committed `.mgeskin` is canonical, not the script
// that produced it, so the hash is taken over the delivered asset's bytes.
//
// Covers what a binding actually depends on: bind-pose positions and normals,
// joint influences, the index buffer, and the region table. Deliberately NOT
// the morph targets — a binding is evaluated *through* morphs, so adding a
// shape parameter must not invalidate every garment in the catalogue, while
// changing a vertex's index or position must.
uint64_t skinnedMeshContentHash(const SkinnedMeshData& mesh);

// ---------------------------------------------------------- the binding ----

// One garment vertex, expressed on the base surface. 16 bytes: at 1600
// vertices a garment's binding is 25 KB, baked once and shared by every
// character wearing it.
//
// The offset is stored in the triangle's own tangent frame, not in character
// space, so it rotates with the surface: when a belly morph pushes the abdomen
// out and tilts it, the tunic over it moves out and tilts too, keeping its
// thickness instead of being swallowed. Constant magnitude in that frame IS
// constant cloth thickness — which is why no area-scaling term appears here.
struct SurfaceBind {
    uint32_t triangle = 0;         // index into the base's triangle list
    uint16_t bary[2] = {0, 0};     // u, v / 65535 within that triangle; w = 1-u-v
    int16_t offset[3] = {0, 0, 0};  // * scale / 32767, in the surface tangent frame
    uint16_t pad = 0;
};
static_assert(sizeof(SurfaceBind) == 16, "surface bind layout is a file-format contract");

struct GarmentBinding {
    uint64_t baseHash = 0;      // content hash of the surface bound against (13.3)
    // The body at the root of the layer chain. For layer 0 this equals
    // `baseHash`; for an outer layer the base is the layer beneath it, and
    // this is what still ties the binding to a specific body. Carried so a
    // mismatch can say "the body changed" instead of "some surface changed".
    uint64_t rootBodyHash = 0;
    float offsetScale = 0;      // metres that int16 32767 stands for
    uint32_t baseVertexCount = 0;
    uint32_t baseTriangleCount = 0;
    uint8_t layer = 0;          // 0 base / 1 mid / 2 outer
    std::vector<SurfaceBind> binds;  // one per garment vertex, in garment order

    bool empty() const { return binds.empty(); }
    void clear() {
        baseHash = 0;
        rootBodyHash = 0;
        offsetScale = 0;
        baseVertexCount = 0;
        baseTriangleCount = 0;
        layer = 0;
        binds.clear();
    }
};

// Why a binding was refused — the pipeline never silently mis-fits (13.3).
enum class BindingCheck : uint8_t {
    Ok = 0,
    BaseHashMismatch,   // the body changed: a contract-version event, ADR 0008
    BaseTopologyChanged,  // vertex/triangle counts differ from the bake
    GarmentSizeMismatch,  // binding was baked for a different garment mesh
    Empty,
};

const char* bindingCheckReason(BindingCheck check);

// Validates a binding against the surface it is about to be evaluated on.
// Called before every evaluation: a binding whose body has moved on is
// REFUSED, loudly, rather than producing a garment that fits nothing.
BindingCheck checkBinding(const GarmentBinding& binding, const SkinnedMeshData& base,
                          size_t garmentVertexCount);

// ------------------------------------------------------- the evaluation ----

// The bind-pose surface of a mesh after its morphs are applied — the surface a
// binding is evaluated against. Buffers are resized to the mesh's vertex count
// and reused across calls, so a warm caller allocates nothing.
void morphedSurface(const SkinnedMeshData& mesh, const float weights[kMorphCount],
                    std::vector<Vec3>& outPosition, std::vector<Vec3>& outNormal);

// Re-evaluates a garment's bind-pose vertices from a deformed base surface.
// `outVertices` starts as the authored garment vertices (UVs, joints and
// weights are carried through untouched) and comes back with positions and
// normals re-derived from the surface. Returns false if the binding does not
// pass `checkBinding`.
//
// This is the whole runtime half of the fitting guarantee, and it is O(garment
// vertices) with no search: the search happened at bake time.
bool applyBinding(const GarmentBinding& binding, const SkinnedMeshData& base,
                  const std::vector<Vec3>& basePosition, const std::vector<Vec3>& baseNormal,
                  std::vector<SkinVertex>& outVertices);

// ------------------------------------------------------------ the cache ----

// Identity of a morph-set: two characters with the same shape share a fit.
// A family of NPCs built from one variant pays for one re-fit between them.
uint64_t morphSetKey(const float weights[kMorphCount]);

// Re-fit at spawn/equip, on a job lane, cached per (garment, morph-set)
// (task 13.4). Fixed capacity, refuse at cap — like every other engine cache,
// this one never grows to fit demand (P1).
//
// Requesting a fit is not a blocking call: a miss queues the work on the
// Decode lane and answers `Pending`. The caller draws the garment unrefitted
// until the fit lands, exactly as the streaming system draws a lower LOD until
// the real one arrives (P2) — a slightly loose tunic for one frame, never a
// stalled frame.
class GarmentFitCache {
public:
    static constexpr size_t kCapacity = 32;

    enum class Status : uint8_t {
        Ready,    // `out` points at the refitted vertices
        Pending,  // queued or running; ask again next spawn/equip tick
        Refused,  // cache full, or the binding failed its check
    };

    // `jobs` may be null, in which case fits are computed inline on the
    // calling thread (tools and tests take this path).
    void configure(JobSystem* jobs) { jobs_ = jobs; }

    // The pointers must outlive the request: they are the shared, process-wide
    // template and garment assets, which is exactly what they are in practice.
    Status request(uint32_t garmentId, const GarmentBinding& binding,
                   const SkinnedMeshData& base, const SkinnedMeshData& garment,
                   const float weights[kMorphCount], const std::vector<SkinVertex>** out);

    void reset();

    size_t residentCount() const;
    size_t refusedCount() const { return refused_; }
    // Why the last Refused answer happened — the self-serve reason (P12).
    const char* lastRefusal() const { return lastRefusal_; }

private:
    enum class State : uint8_t { Free = 0, Computing, Ready };

    struct Entry {
        uint64_t key = 0;
        std::atomic<uint8_t> state{static_cast<uint8_t>(State::Free)};
        uint32_t lastUse = 0;
        std::vector<SkinVertex> vertices;
        // Inputs the job reads; owned by the caller, stable for the fit's life.
        const GarmentBinding* binding = nullptr;
        const SkinnedMeshData* base = nullptr;
        const SkinnedMeshData* garment = nullptr;
        float weights[kMorphCount] = {};
        std::vector<Vec3> position;  // scratch, reused per entry
        std::vector<Vec3> normal;
    };

    static void runFit(Entry& entry);

    Entry entries_[kCapacity];
    JobSystem* jobs_ = nullptr;
    uint32_t clock_ = 0;
    size_t refused_ = 0;
    const char* lastRefusal_ = "";
};

// ---------------------------------------------------------------- I/O ------

// Bindings ship baked, next to the garment they belong to (`.mgefit`).
void serializeBinding(const GarmentBinding& binding, std::vector<uint8_t>& out);
bool deserializeBinding(const uint8_t* data, size_t size, GarmentBinding& out);
bool writeBindingFile(const char* path, const GarmentBinding& binding);
bool readBindingFile(const char* path, GarmentBinding& out);

}  // namespace mge
