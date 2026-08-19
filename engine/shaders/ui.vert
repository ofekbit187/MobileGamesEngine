#version 450

// UI overlay: pixel-space quads to clip space. Vulkan clip Y is down, same
// as UI pixel space, so no flip.

layout(push_constant) uniform UiPush {
    vec2 screenSize;
} push;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;

void main() {
    vec2 ndc = inPos / push.screenSize * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outUv = inUv;
    outColor = inColor;
}
