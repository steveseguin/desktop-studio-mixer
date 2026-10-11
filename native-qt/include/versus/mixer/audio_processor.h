#pragma once
#include <rnnoise.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace versus::mixer {
class AudioProcessor {
    std::array<DenoiseState*, 2> noise{};
    float compressorGain = 1, gateGain = 1;
    int gateHold = 0;
public:
    ~AudioProcessor() { for (auto state : noise) if (state) rnnoise_destroy(state); }
    void process(std::array<float,960> &samples, bool suppress, bool gate, bool compress) {
        if (suppress) {
            for (int channel = 0; channel < 2; ++channel) {
                if (!noise[channel]) noise[channel] = rnnoise_create(nullptr);
                if (!noise[channel]) continue;
                std::array<float,480> mono;
                for (size_t i = 0; i < mono.size(); ++i) mono[i] = samples[i*2+channel]*32768;
                rnnoise_process_frame(noise[channel], mono.data(), mono.data());
                for (size_t i = 0; i < mono.size(); ++i) samples[i*2+channel] = mono[i]/32768;
            }
        } else for (auto &state : noise) if (state) { rnnoise_destroy(state); state = nullptr; }
        if (!gate && !compress) { gateGain = compressorGain = 1; gateHold = 0; return; }
        // Stereo-linked envelope prevents image movement. Smooth gain per sample
        // avoids clicks; no automatic makeup gain can unexpectedly raise noise.
        for (size_t i = 0; i < samples.size(); i += 2) {
            const float peak = std::max(std::abs(samples[i]), std::abs(samples[i+1]));
            if (peak > .0056f) gateHold = 2400; else if (gateHold) --gateHold;
            const float targetGate = !gate || gateHold ? 1.f : 0.f; // -45 dBFS
            gateGain += (targetGate-gateGain)*(targetGate > gateGain ? .004f : .00014f);
            const float target = compress && peak > .1259f ? std::pow(.1259f/peak, .75f) : 1.f; // -18 dBFS, 4:1
            compressorGain += (target-compressorGain)*(target < compressorGain ? .004f : .00014f);
            samples[i] *= gateGain*compressorGain; samples[i+1] *= gateGain*compressorGain;
        }
    }
};
}
