#version 450

// Shared vertex stage for all v1 materials (lit, unlit, placeholder).

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 lightDirIntensity;  // xyz: direction to light (normalized), w: intensity
    vec4 cameraPos;
} frame;

layout(push_constant) uniform DrawData {
    mat4 model;       // 64 B
    vec4 baseColor;   // 16 B
    vec4 params;      // 16 B  (placeholder: x = pattern scale)
    vec4 material;    // 16 B  (x roughness, y AO, z has-packed, w UV scale)
} draw;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
// UV is in METRES of surface (mesh_data.h), so the material's own scale sets
// the tiling rate and a wall and the floor beside it match without the
// content author computing anything.
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUv;

void main() {
    vec4 world = draw.model * vec4(inPosition, 1.0);
    outWorldPos = world.xyz;
    // Uniform-scale assumption for v1; inverse-transpose lands with the
    // full material system.
    outWorldNormal = mat3(draw.model) * inNormal;
    outUv = inUv * draw.material.w;
    gl_Position = frame.viewProj * world;
}
