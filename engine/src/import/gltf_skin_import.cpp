#include "mge/import/gltf_skin_import.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "cgltf.h"

namespace mge {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
}

const char* jointName(Joint joint) {
    switch (joint) {
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
        default: return "";
    }
}

// Which region a vertex belongs to, from the bone that moves it most. Doing
// it from the rig rather than from position means the segmentation follows
// the body even when the body changes.
BodyRegion regionOf(Joint joint) {
    switch (joint) {
        case Joint::Head: return BodyRegion::Scalp;
        case Joint::Neck: return BodyRegion::Neck;
        case Joint::UpperArmL:
        case Joint::ForearmL: return BodyRegion::ArmL;
        case Joint::UpperArmR:
        case Joint::ForearmR: return BodyRegion::ArmR;
        case Joint::HandL: return BodyRegion::HandL;
        case Joint::HandR: return BodyRegion::HandR;
        case Joint::ThighL:
        case Joint::ShinL: return BodyRegion::LegL;
        case Joint::ThighR:
        case Joint::ShinR: return BodyRegion::LegR;
        case Joint::FootL: return BodyRegion::FootL;
        case Joint::FootR: return BodyRegion::FootR;
        default: return BodyRegion::Torso;  // hips, spine, chest
    }
}

const cgltf_accessor* findAttribute(const cgltf_primitive& primitive,
                                    cgltf_attribute_type type, int index = 0) {
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
        const cgltf_attribute& attr = primitive.attributes[a];
        if (attr.type == type && attr.index == index) return attr.data;
    }
    return nullptr;
}

}  // namespace

bool importSkinnedGltf(const char* path, SkinnedMeshData& out, std::string* error) {
    out.clear();

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        setError(error, std::string("cannot parse ") + path);
        return false;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        setError(error, "cannot load buffers");
        cgltf_free(data);
        return false;
    }

    // --- locate the skinned mesh ---
    const cgltf_node* skinnedNode = nullptr;
    for (cgltf_size n = 0; n < data->nodes_count && skinnedNode == nullptr; ++n) {
        if (data->nodes[n].mesh != nullptr && data->nodes[n].skin != nullptr) {
            skinnedNode = &data->nodes[n];
        }
    }
    if (skinnedNode == nullptr) {
        setError(error, "no skinned mesh in file");
        cgltf_free(data);
        return false;
    }
    const cgltf_skin& skin = *skinnedNode->skin;

    // --- map the file's joints onto the canonical rig, by name ---
    std::vector<uint8_t> jointRemap(skin.joints_count, 0xFF);
    std::unordered_map<std::string, uint8_t> canonical;
    for (size_t j = 0; j < kJointCount; ++j) {
        canonical[jointName(static_cast<Joint>(j))] = static_cast<uint8_t>(j);
    }
    size_t matched = 0;
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
        const char* name = skin.joints[j] != nullptr ? skin.joints[j]->name : nullptr;
        if (name == nullptr) continue;
        auto it = canonical.find(name);
        if (it != canonical.end()) {
            jointRemap[j] = it->second;
            ++matched;
        }
    }
    if (matched != kJointCount) {
        setError(error, "model is not rigged to the canonical humanoid skeleton (" +
                            std::to_string(matched) + "/" + std::to_string(kJointCount) +
                            " joints matched by name)");
        cgltf_free(data);
        return false;
    }

    // --- vertices ---
    std::vector<BodyRegion> vertexRegion;
    for (cgltf_size p = 0; p < skinnedNode->mesh->primitives_count; ++p) {
        const cgltf_primitive& prim = skinnedNode->mesh->primitives[p];
        if (prim.type != cgltf_primitive_type_triangles) continue;

        const cgltf_accessor* positions = findAttribute(prim, cgltf_attribute_type_position);
        const cgltf_accessor* normals = findAttribute(prim, cgltf_attribute_type_normal);
        const cgltf_accessor* uvs = findAttribute(prim, cgltf_attribute_type_texcoord);
        const cgltf_accessor* joints = findAttribute(prim, cgltf_attribute_type_joints);
        const cgltf_accessor* weights = findAttribute(prim, cgltf_attribute_type_weights);
        if (positions == nullptr || joints == nullptr || weights == nullptr) {
            setError(error, "primitive is missing positions or skinning attributes");
            cgltf_free(data);
            return false;
        }

        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        for (cgltf_size v = 0; v < positions->count; ++v) {
            SkinVertex vertex;
            float pos[3] = {0, 0, 0};
            cgltf_accessor_read_float(positions, v, pos, 3);
            vertex.position = {pos[0], pos[1], pos[2]};
            if (normals != nullptr) {
                float n[3] = {0, 1, 0};
                cgltf_accessor_read_float(normals, v, n, 3);
                vertex.normal = Vec3{n[0], n[1], n[2]}.normalized();
            } else {
                vertex.normal = {0, 1, 0};
            }
            if (uvs != nullptr) {
                float uv[2] = {0, 0};
                cgltf_accessor_read_float(uvs, v, uv, 2);
                for (int k = 0; k < 2; ++k) {
                    const float t = uv[k] < 0.0f ? 0.0f : (uv[k] > 1.0f ? 1.0f : uv[k]);
                    vertex.uv[k] = static_cast<uint16_t>(t * 65535.0f + 0.5f);
                }
            }

            cgltf_uint idx[4] = {0, 0, 0, 0};
            float w[4] = {0, 0, 0, 0};
            cgltf_accessor_read_uint(joints, v, idx, 4);
            cgltf_accessor_read_float(weights, v, w, 4);

            // Remap, drop influences from unmapped joints, renormalise.
            float total = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (idx[k] >= jointRemap.size() || jointRemap[idx[k]] == 0xFF) w[k] = 0.0f;
                total += w[k];
            }
            if (total <= 0.0f) {
                w[0] = 1.0f;
                total = 1.0f;
                idx[0] = 0;
            }
            int best = 0;
            int assigned = 0;
            for (int k = 0; k < 4; ++k) {
                const uint8_t joint = (idx[k] < jointRemap.size() && jointRemap[idx[k]] != 0xFF)
                                          ? jointRemap[idx[k]]
                                          : 0;
                const int q = static_cast<int>(w[k] / total * 255.0f + 0.5f);
                vertex.joints[k] = joint;
                vertex.weights[k] = static_cast<uint8_t>(q < 0 ? 0 : (q > 255 ? 255 : q));
                assigned += vertex.weights[k];
                if (w[k] > w[best]) best = k;
            }
            // Rounding must land exactly on 255 — the shader divides by it, so
            // a sum of 256 stretches the vertex and a sum of 511 doubles it.
            // The correction goes to the dominant influence, where it is
            // invisible, and is clamped so it can never wrap.
            int fix = 255 - assigned;
            const int corrected = static_cast<int>(vertex.weights[best]) + fix;
            vertex.weights[best] = static_cast<uint8_t>(corrected < 0 ? 0 :
                                                        (corrected > 255 ? 255 : corrected));
            out.vertices.push_back(vertex);
            vertexRegion.push_back(regionOf(static_cast<Joint>(vertex.joints[best])));
        }

        std::vector<uint32_t> primIndices;
        if (prim.indices != nullptr) {
            primIndices.reserve(prim.indices->count);
            for (cgltf_size i = 0; i < prim.indices->count; ++i) {
                primIndices.push_back(base + static_cast<uint32_t>(
                                                 cgltf_accessor_read_index(prim.indices, i)));
            }
        } else {
            for (cgltf_size i = 0; i < positions->count; ++i) {
                primIndices.push_back(base + static_cast<uint32_t>(i));
            }
        }
        out.indices.insert(out.indices.end(), primIndices.begin(), primIndices.end());
    }
    cgltf_free(data);

    if (out.vertices.empty() || out.indices.size() < 3) {
        setError(error, "skinned mesh has no triangles");
        return false;
    }

    // --- group triangles by region so masking is a draw-range decision ---
    std::vector<std::vector<uint32_t>> byRegion(kBodyRegionCount);
    for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
        // A triangle belongs to the region most of its corners belong to.
        int votes[kBodyRegionCount] = {0};
        for (int k = 0; k < 3; ++k) {
            votes[static_cast<size_t>(vertexRegion[out.indices[i + k]])] += 1;
        }
        size_t winner = 0;
        for (size_t r = 1; r < kBodyRegionCount; ++r) {
            if (votes[r] > votes[winner]) winner = r;
        }
        byRegion[winner].insert(byRegion[winner].end(),
                                {out.indices[i], out.indices[i + 1], out.indices[i + 2]});
    }
    out.indices.clear();
    for (size_t r = 0; r < kBodyRegionCount; ++r) {
        if (byRegion[r].empty()) continue;
        MeshPart part;
        part.region = static_cast<BodyRegion>(r);
        part.firstIndex = static_cast<uint32_t>(out.indices.size());
        part.indexCount = static_cast<uint32_t>(byRegion[r].size());
        out.indices.insert(out.indices.end(), byRegion[r].begin(), byRegion[r].end());
        out.parts.push_back(part);
    }

    out.computeBounds();
    return true;
}

}  // namespace mge
