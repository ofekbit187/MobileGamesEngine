#include "mge/import/gltf_import.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace mge {

namespace {

void setError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

// Multiplies a cgltf world matrix (column-major float[16]) with a point/vector.
Vec3 transformPoint(const float* m, const Vec3& v) {
    return {
        m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
        m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
        m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14],
    };
}

Vec3 transformDirection(const float* m, const Vec3& v) {
    return {
        m[0] * v.x + m[4] * v.y + m[8] * v.z,
        m[1] * v.x + m[5] * v.y + m[9] * v.z,
        m[2] * v.x + m[6] * v.y + m[10] * v.z,
    };
}

bool appendPrimitive(const cgltf_primitive& primitive, const float* world, MeshData& out) {
    if (primitive.type != cgltf_primitive_type_triangles) return true;  // skip non-triangles

    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
        const cgltf_attribute& attr = primitive.attributes[a];
        if (attr.type == cgltf_attribute_type_position) positions = attr.data;
        if (attr.type == cgltf_attribute_type_normal) normals = attr.data;
    }
    if (positions == nullptr) return false;

    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());
    for (cgltf_size v = 0; v < positions->count; ++v) {
        float p[3] = {0, 0, 0};
        cgltf_accessor_read_float(positions, v, p, 3);
        Vertex vertex;
        vertex.position = transformPoint(world, {p[0], p[1], p[2]});
        if (normals != nullptr) {
            float n[3] = {0, 1, 0};
            cgltf_accessor_read_float(normals, v, n, 3);
            vertex.normal = transformDirection(world, {n[0], n[1], n[2]}).normalized();
        }
        out.vertices.push_back(vertex);
    }

    if (primitive.indices != nullptr) {
        for (cgltf_size i = 0; i < primitive.indices->count; ++i) {
            out.indices.push_back(
                baseVertex + static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i)));
        }
    } else {
        for (cgltf_size i = 0; i < positions->count; ++i) {
            out.indices.push_back(baseVertex + static_cast<uint32_t>(i));
        }
    }

    // No normals in the source: generate flat face normals.
    if (normals == nullptr) {
        const size_t indexStart = out.indices.size() - (primitive.indices != nullptr
                                                            ? primitive.indices->count
                                                            : positions->count);
        for (size_t i = indexStart; i + 2 < out.indices.size(); i += 3) {
            Vertex& a = out.vertices[out.indices[i]];
            Vertex& b = out.vertices[out.indices[i + 1]];
            Vertex& c = out.vertices[out.indices[i + 2]];
            const Vec3 n =
                (b.position - a.position).cross(c.position - a.position).normalized();
            a.normal = b.normal = c.normal = n;
        }
    }
    return true;
}

}  // namespace

bool importGltf(const char* path, LodMesh& out, std::string* error) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
        setError(error, "failed to parse glTF");
        return false;
    }
    if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
        setError(error, "failed to load glTF buffers");
        cgltf_free(data);
        return false;
    }

    out = LodMesh{};
    out.lods.emplace_back();
    MeshData& mesh = out.lods[0];

    bool ok = true;
    for (cgltf_size n = 0; n < data->nodes_count && ok; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (node.mesh == nullptr) continue;
        float world[16];
        cgltf_node_transform_world(&node, world);
        for (cgltf_size p = 0; p < node.mesh->primitives_count && ok; ++p) {
            ok = appendPrimitive(node.mesh->primitives[p], world, mesh);
        }
    }
    cgltf_free(data);

    if (!ok || mesh.vertices.empty() || mesh.indices.empty()) {
        setError(error, ok ? "glTF contains no triangle geometry" : "unsupported primitive data");
        out = LodMesh{};
        return false;
    }
    out.computeBounds();
    return true;
}

}  // namespace mge
