#version 450

// Basic lit opaque: one directional light + hemispheric ambient, now textured.
//
// TWO fetches at most (docs/TEXTURING.md §3): albedo, and an optional packed
// AO/roughness/mask map in the fixed ORM channel order. Mobile tile-based GPUs
// are filtering-bound long before they are bandwidth-bound, so a third sampler
// would be a permanent per-pixel tax.
//
// Colour space is the TEXTURE's property, never the shader's: albedo is bound
// in an sRGB-typed format and the hardware linearises on fetch, while the
// packed map is linear. There is deliberately no pow() here — a shader-side
// gamma correction is how a pipeline ends up double-correcting.

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 lightDirIntensity;
    vec4 cameraPos;
} frame;

layout(push_constant) uniform DrawData {
    mat4 model;
    vec4 baseColor;
    vec4 params;
    vec4 material;  // x roughness, y AO, z has-packed, w UV scale
} draw;

// Set 2 is the material. Both samplers are always bound: a material with no
// map gets the renderer's 1x1 white, which keeps one pipeline instead of a
// textured and an untextured permutation.
layout(set = 2, binding = 0) uniform sampler2D albedoMap;
layout(set = 2, binding = 1) uniform sampler2D packedMap;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 albedo = texture(albedoMap, inUv) * draw.baseColor;

    // AO and roughness come from the packed map when there is one, and from
    // per-material constants when there is not. A material with no packed map
    // is normal, not lazy (TEXTURING §3).
    float ao = draw.material.y;
    if (draw.material.z != 0.0) {
        vec3 packed = texture(packedMap, inUv).rgb;  // R=AO G=roughness B=mask
        ao = packed.r;
    }

    vec3 n = normalize(inWorldNormal);
    vec3 l = normalize(frame.lightDirIntensity.xyz);
    float ndl = max(dot(n, l), 0.0);
    // Hemispheric ambient: sky-tinted from above, ground-tinted from below.
    float hemi = n.y * 0.5 + 0.5;
    vec3 ambient = mix(vec3(0.18, 0.16, 0.13), vec3(0.32, 0.34, 0.38), hemi);
    // AO occludes ambient only — it is a visibility term for the sky, not a
    // licence to darken direct sun, which is why baked-in shadow never belongs
    // in albedo.
    vec3 color = albedo.rgb * (ambient * ao + ndl * frame.lightDirIntensity.w);
    outColor = vec4(color, albedo.a);
}
