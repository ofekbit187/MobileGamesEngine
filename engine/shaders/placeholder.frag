#version 450

// Virtual-model placeholder treatment (P5): unmistakably "not the real
// asset" — diagonal hatching in world space over a flat tint, edges lifted
// by a cheap fresnel-ish term so the volume reads clearly.

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 lightDirIntensity;
    vec4 cameraPos;
} frame;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 baseColor;   // placeholder tint
    vec4 params;      // x: hatch scale (stripes per meter)
    vec4 material;    // unused here; keeps one push-constant layout engine-wide
} draw;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

void main() {
    float scale = max(draw.params.x, 0.001);
    float stripe = fract((inWorldPos.x + inWorldPos.y + inWorldPos.z) * scale);
    float hatch = step(0.5, stripe) * 0.25 + 0.75;

    vec3 n = normalize(inWorldNormal);
    vec3 v = normalize(frame.cameraPos.xyz - inWorldPos);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 2.0);

    vec3 color = draw.baseColor.rgb * hatch + rim * 0.35;
    outColor = vec4(color, draw.baseColor.a);
}
