#pragma once

// Minimal wav support (task 9.10): load/validate delivered voice takes
// (PCM16 RIFF). This is the first brick of the audio pillar — an output
// sink/mixer joins when audio is dictated; until then takes are validated
// and inspected headlessly.

#include <cstdint>
#include <vector>

namespace mge {

struct WavData {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    std::vector<int16_t> samples;  // interleaved

    double seconds() const {
        return sampleRate == 0 || channels == 0
                   ? 0.0
                   : static_cast<double>(samples.size() / channels) / sampleRate;
    }
};

bool loadWav(const char* path, WavData& out);
bool writeWav(const char* path, const WavData& in);

}  // namespace mge
