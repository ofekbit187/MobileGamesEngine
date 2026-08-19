#version 450

// UI overlay: vertex color modulated by atlas coverage (R8). Solid quads
// sample the reserved white texel, so one pipeline draws everything.

layout(set = 0, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    float coverage = texture(atlas, inUv).r;
    outColor = vec4(inColor.rgb, inColor.a * coverage);
}
