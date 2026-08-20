#include "texture_bake.h"

#include <cmath>
#include <cstring>

namespace mgetex {

using namespace mge;

namespace {

// sRGB <-> linear, the exact piecewise curve. Mips are filtered in linear
// space and re-encoded; box-filtering sRGB values darkens every level and is
// the second most common texture defect (docs/TEXTURING.md §7).
float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
float linearToSrgb(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

uint8_t toByte(float v) {
    const float clamped = v < 0 ? 0 : (v > 1 ? 1 : v);
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

// Deterministic value noise — same seed, same pixels, every build (§8).
uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float noise01(uint32_t x, uint32_t y, uint32_t seed) {
    return static_cast<float>(hash(x, y, seed) & 0xffffffu) / static_cast<float>(0xffffff);
}
// Wrapping smooth noise, so a tiling surface has no seam at the edge.
float smoothNoise(float x, float y, uint32_t period, uint32_t seed) {
    const uint32_t x0 = static_cast<uint32_t>(x) % period;
    const uint32_t y0 = static_cast<uint32_t>(y) % period;
    const uint32_t x1 = (x0 + 1) % period;
    const uint32_t y1 = (y0 + 1) % period;
    const float fx = x - std::floor(x);
    const float fy = y - std::floor(y);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = noise01(x0, y0, seed), n10 = noise01(x1, y0, seed);
    const float n01 = noise01(x0, y1, seed), n11 = noise01(x1, y1, seed);
    return (n00 * (1 - sx) + n10 * sx) * (1 - sy) + (n01 * (1 - sx) + n11 * sx) * sy;
}
float fbm(float u, float v, uint32_t basePeriod, uint32_t seed) {
    float sum = 0, amplitude = 0.5f, total = 0;
    uint32_t period = basePeriod;
    for (int octave = 0; octave < 4; ++octave) {
        sum += smoothNoise(u * period, v * period, period, seed + octave * 71) * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        period *= 2;
    }
    return sum / total;
}

}  // namespace

SourceImage makeUvChart(uint32_t size) {
    SourceImage image;
    // The UV field is DATA, not colour: it must not be gamma-encoded, or the
    // value read back is not the value written.
    image.colorSpace = ColorSpace::Linear;
    image.resize(size, size);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            uint8_t* p = image.at(x, y);
            // Texel centres, so the value at the centre of texel i is
            // (i + 0.5) / size — exactly what a linear sampler returns there.
            p[0] = static_cast<uint8_t>((x * 255) / (size - 1));
            p[1] = static_cast<uint8_t>((y * 255) / (size - 1));
            p[2] = 0;
            p[3] = 255;
        }
    }
    return image;
}

SourceImage makeChecker(uint32_t size, uint32_t squares, const uint8_t a[4],
                        const uint8_t b[4]) {
    SourceImage image;
    image.colorSpace = ColorSpace::Srgb;
    image.resize(size, size);
    const uint32_t cell = size / squares;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const bool even = ((x / cell) + (y / cell)) % 2 == 0;
            memcpy(image.at(x, y), even ? a : b, 4);
        }
    }
    return image;
}

SourceImage makePlaster(uint32_t size, uint32_t seed) {
    SourceImage image;
    image.colorSpace = ColorSpace::Srgb;
    image.resize(size, size);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / size, v = static_cast<float>(y) / size;
            // Bone/limewash: value variation, almost no hue variation (§2).
            const float n = fbm(u, v, 8, seed) * 0.55f + fbm(u, v, 32, seed + 5) * 0.45f;
            const float value = 0.62f + (n - 0.5f) * 0.26f;
            uint8_t* p = image.at(x, y);
            p[0] = toByte(value * 1.02f);
            p[1] = toByte(value * 0.99f);
            p[2] = toByte(value * 0.92f);
            p[3] = 255;
        }
    }
    return image;
}

SourceImage makePlank(uint32_t size, uint32_t seed) {
    SourceImage image;
    image.colorSpace = ColorSpace::Srgb;
    image.resize(size, size);
    const uint32_t planks = 6;
    const uint32_t plankHeight = size / planks;
    for (uint32_t y = 0; y < size; ++y) {
        const uint32_t plank = y / plankHeight;
        const float rowShift = noise01(plank, 0, seed) * 0.5f;
        const float plankTone = 0.82f + noise01(plank, 1, seed) * 0.36f;
        for (uint32_t x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / size + rowShift;
            const float v = static_cast<float>(y) / size;
            // Grain runs along the plank; the cross-grain frequency is what
            // reads as wood at three metres.
            const float grain = fbm(u * 0.5f, v * 6.0f, 16, seed + plank * 13);
            float value = (0.30f + grain * 0.22f) * plankTone;
            // The dark gap between planks — geometry does not carry it, the
            // texture does (§2: texture carries material, geometry silhouette).
            const uint32_t inPlank = y % plankHeight;
            if (inPlank == 0 || inPlank == plankHeight - 1) value *= 0.45f;
            uint8_t* p = image.at(x, y);
            p[0] = toByte(value * 1.00f);
            p[1] = toByte(value * 0.76f);
            p[2] = toByte(value * 0.52f);
            p[3] = 255;
        }
    }
    return image;
}

SourceImage makeGround(uint32_t size, uint32_t seed) {
    SourceImage image;
    image.colorSpace = ColorSpace::Srgb;
    image.resize(size, size);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / size, v = static_cast<float>(y) / size;
            const float coarse = fbm(u, v, 6, seed);
            const float fine = fbm(u, v, 48, seed + 9);
            const float value = 0.26f + coarse * 0.16f + fine * 0.10f;
            // Umber earth with a little moss in the hollows.
            const float moss = coarse < 0.42f ? (0.42f - coarse) * 0.9f : 0.0f;
            uint8_t* p = image.at(x, y);
            p[0] = toByte(value * (1.00f - moss * 0.45f));
            p[1] = toByte(value * (0.86f + moss * 0.35f));
            p[2] = toByte(value * (0.58f - moss * 0.10f));
            p[3] = 255;
        }
    }
    return image;
}

SourceImage makePacked(const SourceImage& albedo, float roughness) {
    SourceImage image;
    // A DATA map: linear, always. A roughness map sampled through sRGB is a
    // bug that looks like an art problem (§4).
    image.colorSpace = ColorSpace::Linear;
    image.resize(albedo.width, albedo.height);
    for (uint32_t y = 0; y < albedo.height; ++y) {
        for (uint32_t x = 0; x < albedo.width; ++x) {
            const uint8_t* src = albedo.at(x, y);
            // Cavity-style AO derived from the albedo's own value: darker
            // crevices occlude more. Crude, but it is a stand-in for an
            // authored map, and it exercises the channel order.
            const float value = (src[0] * 0.299f + src[1] * 0.587f + src[2] * 0.114f) / 255.0f;
            uint8_t* p = image.at(x, y);
            p[0] = toByte(0.55f + value * 0.45f);  // R = ambient occlusion
            p[1] = toByte(roughness);              // G = roughness
            p[2] = 0;                              // B = mask (throwaway channel)
            p[3] = 255;
        }
    }
    return image;
}

namespace {

// Box-filter one level down, in linear space.
SourceImage halve(const SourceImage& src) {
    SourceImage dst;
    dst.colorSpace = src.colorSpace;
    dst.resize(src.width > 1 ? src.width / 2 : 1, src.height > 1 ? src.height / 2 : 1);
    const bool srgb = src.colorSpace == ColorSpace::Srgb;
    for (uint32_t y = 0; y < dst.height; ++y) {
        for (uint32_t x = 0; x < dst.width; ++x) {
            const uint32_t sx = x * 2, sy = y * 2;
            const uint32_t x1 = src.width > 1 ? sx + 1 : sx;
            const uint32_t y1 = src.height > 1 ? sy + 1 : sy;
            float accum[4] = {0, 0, 0, 0};
            const uint32_t xs[2] = {sx, x1};
            const uint32_t ys[2] = {sy, y1};
            for (uint32_t j = 0; j < 2; ++j) {
                for (uint32_t i = 0; i < 2; ++i) {
                    const uint8_t* p = src.at(xs[i], ys[j]);
                    for (int c = 0; c < 3; ++c) {
                        const float v = p[c] / 255.0f;
                        accum[c] += srgb ? srgbToLinear(v) : v;
                    }
                    accum[3] += p[3] / 255.0f;
                }
            }
            uint8_t* d = dst.at(x, y);
            for (int c = 0; c < 3; ++c) {
                const float linear = accum[c] * 0.25f;
                d[c] = toByte(srgb ? linearToSrgb(linear) : linear);
            }
            d[3] = toByte(accum[3] * 0.25f);
        }
    }
    return dst;
}

}  // namespace

bool bakeTexture(const SourceImage& source, TextureFormat format, TextureUsage usage,
                 TextureData& out) {
    if (source.width == 0 || source.height == 0) return false;
    // The standard demands power-of-two (§4/§9), and the mip chain below
    // assumes it. Refuse rather than silently resampling.
    if ((source.width & (source.width - 1)) != 0 ||
        (source.height & (source.height - 1)) != 0) {
        return false;
    }
    if (format != TextureFormat::Rgba8) return false;  // only the reference pack is generated here

    out = TextureData{};
    out.width = source.width;
    out.height = source.height;
    out.format = format;
    out.colorSpace = source.colorSpace;
    out.usage = usage;

    SourceImage level = source;
    const uint32_t levels = fullMipCount(source.width, source.height);
    for (uint32_t i = 0; i < levels; ++i) {
        TextureMip mip;
        mip.width = level.width;
        mip.height = level.height;
        mip.offset = out.pixels.size();
        mip.size = mipByteSize(format, level.width, level.height);
        out.pixels.insert(out.pixels.end(), level.rgba.begin(), level.rgba.end());
        out.mips.push_back(mip);
        if (i + 1 < levels) level = halve(level);
    }
    const char* reason = "";
    return validateTexture(out, &reason);
}

double mip0Psnr(const SourceImage& source, const TextureData& baked) {
    if (baked.mips.empty()) return 0.0;
    const TextureMip& mip = baked.mips[0];
    if (mip.width != source.width || mip.height != source.height) return 0.0;
    const size_t count = source.rgba.size();
    if (mip.size < count) return 0.0;
    double sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const double d = static_cast<double>(source.rgba[i]) - baked.pixels[mip.offset + i];
        sum += d * d;
    }
    const double mse = sum / count;
    if (mse <= 0.0) return 99.0;  // lossless: reported as the cap, not infinity
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

}  // namespace mgetex
