#pragma once

// `.mgetex` — the baked runtime texture container (docs/TEXTURING.md §4), the
// house-style sibling of `.mgemesh`: a header, a per-mip index table, then the
// payload. Each mip is its own byte range so a distant chunk can hold mip 4
// and pull finer levels on approach without re-reading the file.
//
// Written by the baker, read by the runtime. KTX2/PNG never reach a device,
// the way glTF never does.

#include <cstdint>
#include <vector>

#include "mge/graphics/texture_data.h"

namespace mge {

void serializeTexture(const TextureData& texture, std::vector<uint8_t>& out);
bool deserializeTexture(const uint8_t* data, size_t size, TextureData& out);

bool writeTextureFile(const char* path, const TextureData& texture);
bool readTextureFile(const char* path, TextureData& out);

// Reads the header and the mip index WITHOUT the payload: what a streaming
// residency decision needs in order to ask for one level. This is the reason
// the index table is separate from the pixels.
struct TextureInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    ColorSpace colorSpace = ColorSpace::Linear;
    TextureUsage usage = TextureUsage::Albedo;
    std::vector<TextureMip> mips;  // offsets are absolute file offsets
};
bool readTextureInfo(const char* path, TextureInfo& out);

// Reads one mip level's bytes only — the per-mip streaming read.
bool readTextureMip(const char* path, const TextureInfo& info, uint32_t level,
                    std::vector<uint8_t>& out);

}  // namespace mge
