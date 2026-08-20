#version 450

// Skinned vertex stage (task 8.10): the ONE template body mesh, deformed on
// the GPU by this character's 17-joint palette and 15 morph weights.
// Proportions ride the palette (per-joint scale), shape rides the morph
// deltas — so a crowd shares one mesh and one delta buffer, and differs only
// by 17 matrices plus 15 floats each (the P1 claim of the variation design,
// ADR 0007 + ADR 0009).
//
// ORDER IS A CONTRACT: shape is applied to the BIND-POSE vertex, before any
// bone touches it, because that is what the CPU reference skinMesh() does and
// `mge_skin_test` compares the two pixel by pixel.

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 lightDirIntensity;
    vec4 cameraPos;
} frame;

const int kJointCount = 17;

// Per character: the joint palette and, alongside it, this character's shape.
// Weights are packed four to a vec4 (std140 gives an array of floats a 16-byte
// stride, which would waste 180 of 240 bytes in every slot).
layout(set = 0, binding = 1) uniform SkinPalette {
    mat4 joints[kJointCount];
    vec4 morphWeights[4];
} skin;

// Per MESH: the sparse morph deltas, uploaded once and shared by every
// character drawn with this mesh. One flat uint array, because the layout is
// three variable-length tables and std430 allows only one of those:
//
//   data[0]                    first word of the entry table
//   data[1]                    vertex count (the offset table is one longer)
//   data[2 .. 17]              per-target quantization scale, as float bits
//   data[18 .. 18+vertexCount] per-vertex prefix offsets into the entry table
//   data[entryBase ..]         3 words per delta, 12 bytes — the same 12 bytes
//                              the .mgeskin file spends on it (ADR 0009 §6)
//
// A vertex reads its own [first, last) slice, so a face parameter costs the
// few dozen vertices it moves and nothing on the other 1600.
layout(set = 1, binding = 0, std430) readonly buffer MorphData {
    uint data[];
} morph;

const uint kMorphEntryBaseWord = 0u;
const uint kMorphScaleWord = 2u;
const uint kMorphOffsetWord = 18u;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 baseColor;
    vec4 params;  // x != 0: this draw carries morph targets and weights
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

// The packed delta is signed; GLSL has no int16/int8, so sign-extend by
// shifting the field up to the top of a 32-bit int and back down.
int signExtend16(uint bits) { return int(bits << 16) >> 16; }
int signExtend8(uint bits) { return int(bits << 24) >> 24; }

void main() {
    vec3 bindPosition = inPosition;
    vec3 bindNormal = inNormal;

    // --- the shape pass, in bind space (ADR 0009) ---
    if (draw.params.x != 0.0) {
        uint entryBase = morph.data[kMorphEntryBaseWord];
        uint slot = kMorphOffsetWord + uint(gl_VertexIndex);
        uint first = morph.data[slot];
        uint last = morph.data[slot + 1u];

        vec3 deltaPosition = vec3(0.0);
        vec3 deltaNormal = vec3(0.0);
        for (uint e = first; e < last; ++e) {
            uint word = entryBase + e * 3u;
            uint w0 = morph.data[word];
            uint w1 = morph.data[word + 1u];
            uint w2 = morph.data[word + 2u];

            uint target = (w2 >> 8u) & 0xffu;
            float weight = skin.morphWeights[target >> 2u][target & 3u];
            float scale = uintBitsToFloat(morph.data[kMorphScaleWord + target]);

            deltaPosition += vec3(signExtend16(w0), signExtend16(w0 >> 16u),
                                  signExtend16(w1)) *
                             (weight * scale * (1.0 / 32767.0));
            deltaNormal += vec3(signExtend8(w1 >> 16u), signExtend8(w1 >> 24u),
                                signExtend8(w2)) *
                           (weight * (1.0 / 127.0));
        }
        bindPosition += deltaPosition;
        bindNormal = normalize(bindNormal + deltaNormal);
    }

    // --- then the palette ---
    // Four influences is the mobile-universal maximum; the template uses at
    // most two, so the tail terms are weight 0 and cost nothing visible.
    mat4 blended =
        skin.joints[inJoints.x] * inWeights.x +
        skin.joints[inJoints.y] * inWeights.y +
        skin.joints[inJoints.z] * inWeights.z +
        skin.joints[inJoints.w] * inWeights.w;

    vec4 skinnedPos = blended * vec4(bindPosition, 1.0);
    vec3 skinnedNormal = mat3(blended) * bindNormal;

    vec4 world = draw.model * skinnedPos;
    outWorldPos = world.xyz;
    outWorldNormal = normalize(mat3(draw.model) * skinnedNormal);
    gl_Position = frame.viewProj * world;
}
