#include "mge/graphics/texture_io.h"

#include <cstdio>
#include <cstring>

#include "mge/core/log.h"

namespace mge {

namespace {

constexpr char kMagic[4] = {'M', 'G', 'E', 'T'};
constexpr uint32_t kVersion = 1;
constexpr const char* kTag = "texture_io";

struct FileHeader {
    char magic[4];
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint8_t format;
    uint8_t colorSpace;
    uint8_t usage;
    uint8_t reserved;
    uint32_t mipCount;
    uint64_t payloadBytes;
};

struct MipEntry {
    uint32_t width;
    uint32_t height;
    uint64_t offset;  // relative to the start of the payload
    uint64_t size;
};

void append(std::vector<uint8_t>& out, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

bool take(const uint8_t*& cursor, size_t& remaining, void* dest, size_t size) {
    if (remaining < size) return false;
    memcpy(dest, cursor, size);
    cursor += size;
    remaining -= size;
    return true;
}

size_t payloadStart(uint32_t mipCount) {
    return sizeof(FileHeader) + static_cast<size_t>(mipCount) * sizeof(MipEntry);
}

}  // namespace

void serializeTexture(const TextureData& texture, std::vector<uint8_t>& out) {
    FileHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version = kVersion;
    header.width = texture.width;
    header.height = texture.height;
    header.format = static_cast<uint8_t>(texture.format);
    header.colorSpace = static_cast<uint8_t>(texture.colorSpace);
    header.usage = static_cast<uint8_t>(texture.usage);
    header.mipCount = static_cast<uint32_t>(texture.mips.size());
    header.payloadBytes = texture.pixels.size();
    append(out, &header, sizeof(header));

    for (const TextureMip& mip : texture.mips) {
        MipEntry entry{mip.width, mip.height, mip.offset, mip.size};
        append(out, &entry, sizeof(entry));
    }
    append(out, texture.pixels.data(), texture.pixels.size());
}

bool deserializeTexture(const uint8_t* data, size_t size, TextureData& out) {
    const uint8_t* cursor = data;
    size_t remaining = size;

    FileHeader header{};
    if (!take(cursor, remaining, &header, sizeof(header)) ||
        memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion ||
        header.mipCount == 0 || header.mipCount > 16 ||
        header.format >= static_cast<uint8_t>(TextureFormat::Count)) {
        return false;
    }

    out = TextureData{};
    out.width = header.width;
    out.height = header.height;
    out.format = static_cast<TextureFormat>(header.format);
    out.colorSpace = static_cast<ColorSpace>(header.colorSpace);
    out.usage = static_cast<TextureUsage>(header.usage);
    out.mips.resize(header.mipCount);
    for (TextureMip& mip : out.mips) {
        MipEntry entry{};
        if (!take(cursor, remaining, &entry, sizeof(entry))) {
            out.clear();
            return false;
        }
        mip.width = entry.width;
        mip.height = entry.height;
        mip.offset = entry.offset;
        mip.size = entry.size;
    }
    if (remaining < header.payloadBytes) {
        out.clear();
        return false;
    }
    out.pixels.resize(static_cast<size_t>(header.payloadBytes));
    memcpy(out.pixels.data(), cursor, out.pixels.size());

    // A malformed texture is refused here, not clamped into something the GPU
    // will read past the end of.
    const char* reason = "";
    if (!validateTexture(out, &reason)) {
        MGE_LOGE(kTag, "refused texture: %s", reason);
        out.clear();
        return false;
    }
    return true;
}

bool writeTextureFile(const char* path, const TextureData& texture) {
    const char* reason = "";
    if (!validateTexture(texture, &reason)) {
        MGE_LOGE(kTag, "refused to write %s: %s", path, reason);
        return false;
    }
    std::vector<uint8_t> blob;
    serializeTexture(texture, blob);
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = fwrite(blob.data(), 1, blob.size(), f) == blob.size();
    fclose(f);
    return ok;
}

bool readTextureFile(const char* path, TextureData& out) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    std::vector<uint8_t> blob(static_cast<size_t>(size));
    const bool readOk = fread(blob.data(), 1, blob.size(), f) == blob.size();
    fclose(f);
    if (!readOk || !deserializeTexture(blob.data(), blob.size(), out)) {
        MGE_LOGE(kTag, "bad texture file: %s", path);
        return false;
    }
    return true;
}

bool readTextureInfo(const char* path, TextureInfo& out) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    FileHeader header{};
    if (fread(&header, 1, sizeof(header), f) != sizeof(header) ||
        memcmp(header.magic, kMagic, 4) != 0 || header.version != kVersion ||
        header.mipCount == 0 || header.mipCount > 16 ||
        header.format >= static_cast<uint8_t>(TextureFormat::Count)) {
        fclose(f);
        return false;
    }
    out = TextureInfo{};
    out.width = header.width;
    out.height = header.height;
    out.format = static_cast<TextureFormat>(header.format);
    out.colorSpace = static_cast<ColorSpace>(header.colorSpace);
    out.usage = static_cast<TextureUsage>(header.usage);
    out.mips.resize(header.mipCount);
    const size_t base = payloadStart(header.mipCount);
    bool ok = true;
    for (TextureMip& mip : out.mips) {
        MipEntry entry{};
        if (fread(&entry, 1, sizeof(entry), f) != sizeof(entry)) {
            ok = false;
            break;
        }
        mip.width = entry.width;
        mip.height = entry.height;
        mip.offset = base + entry.offset;  // absolute, ready to seek to
        mip.size = entry.size;
    }
    fclose(f);
    if (!ok) out = TextureInfo{};
    return ok;
}

bool readTextureMip(const char* path, const TextureInfo& info, uint32_t level,
                    std::vector<uint8_t>& out) {
    if (level >= info.mips.size()) return false;
    const TextureMip& mip = info.mips[level];
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    if (fseek(f, static_cast<long>(mip.offset), SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(mip.size));
    const bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

}  // namespace mge
