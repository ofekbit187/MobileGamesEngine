#version 450

// Skinned vertex stage (task 8.10): the ONE template body mesh, deformed on
// the GPU by this character's 17-joint palette. Variants ride the palette
// (per-joint scale), so a crowd shares one mesh and differs only by 17
// matrices each — the P1 claim of the template-body design.

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 lightDirIntensity;
    vec4 cameraPos;
} frame;

const int kJointCount = 17;

layout(set = 0, binding = 1) uniform SkinPalette {
    mat4 joints[kJointCount];
} skin;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 baseColor;
    vec4 params;
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

void main() {
    // Four influences is the mobile-universal maximum; the template uses at
    // most two, so the tail terms are weight 0 and cost nothing visible.
    mat4 blended =
        skin.joints[inJoints.x] * inWeights.x +
        skin.joints[inJoints.y] * inWeights.y +
        skin.joints[inJoints.z] * inWeights.z +
        skin.joints[inJoints.w] * inWeights.w;

    vec4 skinnedPos = blended * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(blended) * inNormal;

    vec4 world = draw.model * skinnedPos;
    outWorldPos = world.xyz;
    outWorldNormal = normalize(mat3(draw.model) * skinnedNormal);
    gl_Position = frame.viewProj * world;
}
