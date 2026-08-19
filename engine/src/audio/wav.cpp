#include "mge/audio/wav.h"

#include <cstdio>
#include <cstring>

#include "mge/core/log.h"

namespace mge {

namespace {
constexpr const char* kTag = "wav";

struct ChunkHeader {
    char id[4];
    uint32_t size;
};
}  // namespace

bool loadWav(const char* path, WavData& out) {
    out = WavData{};
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;

    char riff[4], wave[4];
    uint32_t riffSize = 0;
    bool ok = fread(riff, 4, 1, f) == 1 && fread(&riffSize, 4, 1, f) == 1 &&
              fread(wave, 4, 1, f) == 1 && memcmp(riff, "RIFF", 4) == 0 &&
              memcmp(wave, "WAVE", 4) == 0;

    uint16_t format = 0, bitsPerSample = 0;
    bool haveFmt = false, haveData = false;
    while (ok && !(haveFmt && haveData)) {
        ChunkHeader chunk;
        if (fread(&chunk, sizeof(chunk), 1, f) != 1) break;
        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            uint16_t channels = 0;
            uint32_t sampleRate = 0, byteRate = 0;
            uint16_t blockAlign = 0;
            ok = fread(&format, 2, 1, f) == 1 && fread(&channels, 2, 1, f) == 1 &&
                 fread(&sampleRate, 4, 1, f) == 1 && fread(&byteRate, 4, 1, f) == 1 &&
                 fread(&blockAlign, 2, 1, f) == 1 && fread(&bitsPerSample, 2, 1, f) == 1;
            if (ok && chunk.size > 16) fseek(f, chunk.size - 16, SEEK_CUR);
            out.channels = channels;
            out.sampleRate = sampleRate;
            haveFmt = true;
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            out.samples.resize(chunk.size / sizeof(int16_t));
            ok = out.samples.empty() ||
                 fread(out.samples.data(), 1, chunk.size, f) >= chunk.size / 2;
            haveData = true;
        } else {
            fseek(f, chunk.size + (chunk.size & 1), SEEK_CUR);  // chunks pad to even
        }
    }
    fclose(f);

    if (!ok || !haveFmt || !haveData || format != 1 /* PCM */ || bitsPerSample != 16 ||
        out.channels == 0 || out.sampleRate == 0) {
        MGE_LOGE(kTag, "%s: not a PCM16 wav", path);
        out = WavData{};
        return false;
    }
    return true;
}

bool writeWav(const char* path, const WavData& in) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    const uint32_t dataSize = static_cast<uint32_t>(in.samples.size() * sizeof(int16_t));
    const uint32_t riffSize = 36 + dataSize;
    const uint32_t byteRate = in.sampleRate * in.channels * 2;
    const uint16_t blockAlign = static_cast<uint16_t>(in.channels * 2);
    const uint16_t format = 1, bits = 16;
    const uint32_t fmtSize = 16;
    bool ok = fwrite("RIFF", 4, 1, f) == 1 && fwrite(&riffSize, 4, 1, f) == 1 &&
              fwrite("WAVE", 4, 1, f) == 1 && fwrite("fmt ", 4, 1, f) == 1 &&
              fwrite(&fmtSize, 4, 1, f) == 1 && fwrite(&format, 2, 1, f) == 1 &&
              fwrite(&in.channels, 2, 1, f) == 1 && fwrite(&in.sampleRate, 4, 1, f) == 1 &&
              fwrite(&byteRate, 4, 1, f) == 1 && fwrite(&blockAlign, 2, 1, f) == 1 &&
              fwrite(&bits, 2, 1, f) == 1 && fwrite("data", 4, 1, f) == 1 &&
              fwrite(&dataSize, 4, 1, f) == 1 &&
              (in.samples.empty() ||
               fwrite(in.samples.data(), 1, dataSize, f) == dataSize);
    fclose(f);
    return ok;
}

}  // namespace mge
