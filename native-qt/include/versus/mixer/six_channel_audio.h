#pragma once
#include "versus/mixer/audio_processor.h"
#include <deque>

namespace versus::mixer {
// Opt-in discrete inputs. No surround speaker roles or LFE filtering: each
// input keeps its own processing state and can be recorded as a separate track.
class SixChannelAudio {
public:
    struct Block {
        std::array<float,960> stereo{};
        std::array<std::array<float,480>,6> input{}, delayed{};
    };
    void reset() { delayed_.clear(); delayMs_ = -1; }
    Block process(std::deque<float> &samples, int delayMs, bool noise, bool gate, bool compress) {
        Block block;
        const auto frames = std::min<size_t>(480, samples.size()/6);
        for (size_t frame=0; frame<frames; ++frame) for (auto &channel:block.input) {
            channel[frame]=samples.front();samples.pop_front();
        }
        for (size_t channel=0;channel<6;++channel) {
            std::array<float,960> mono{};
            for (size_t frame=0;frame<480;++frame) mono[frame*2]=mono[frame*2+1]=block.input[channel][frame];
            processors_[channel].process(mono,noise,gate,compress);
            for (size_t frame=0;frame<480;++frame) block.input[channel][frame]=mono[frame*2];
        }
        if (delayMs_!=delayMs) { delayed_.assign(size_t(delayMs)*48*6,0);delayMs_=delayMs; }
        for (size_t frame=0;frame<480;++frame) for (size_t channel=0;channel<6;++channel) {
            const auto sample=block.input[channel][frame];
            // Alternating slots feed L/R at equal weight, including input 4.
            block.stereo[frame*2+channel%2]+=sample/3.f;
            if (delayMs) { delayed_.push_back(sample);block.delayed[channel][frame]=delayed_.front();delayed_.pop_front(); }
            else block.delayed[channel][frame]=sample;
        }
        return block;
    }
private:
    std::array<AudioProcessor,6> processors_;
    std::deque<float> delayed_;
    int delayMs_=-1;
};
}
