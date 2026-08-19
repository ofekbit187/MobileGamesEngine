#version 450

// Basic lit opaque: one directional light + hemispheric ambient.
// Mobile-first: cheap, no textures yet (base color factor only).

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 lightDirIntensity;
    vec4 cameraPos;
} frame;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 baseColor;
    vec4 params;
} draw;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(inWorldNormal);
    vec3 l = normalize(frame.lightDirIntensity.xyz);
    float ndl = max(dot(n, l), 0.0);
    // Hemispheric ambient: sky-tinted from above, ground-tinted from below.
    float hemi = n.y * 0.5 + 0.5;
    vec3 ambient = mix(vec3(0.18, 0.16, 0.13), vec3(0.32, 0.34, 0.38), hemi);
    vec3 color = draw.baseColor.rgb * (ambient + ndl * frame.lightDirIntensity.w);
    outColor = vec4(color, draw.baseColor.a);
}
