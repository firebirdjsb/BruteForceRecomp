#pragma once
#include <algorithm>
#include <cstdint>

namespace bf::graphics {
struct FrameStamp { uint64_t sequence{},source_ns{},now_ns{};unsigned output_fps{}; };
struct InterpolationStep { bool upload{},reset{},pair{};float alpha{1}; };
// Presentation-only history. A pair is displayed one source interval late;
// no game tick, vblank, controller report or physics timestep is changed.
class InterpolationTimeline {
public:
    void clear(){sequence_=time_=previous_=0;pair_=false;mode_=0;}
    InterpolationStep advance(FrameStamp stamp) {
        InterpolationStep result;
        if((stamp.output_fps!=60&&stamp.output_fps!=120)||!stamp.sequence||!stamp.source_ns||stamp.now_ns<stamp.source_ns) {
            clear();result.reset=true;return result;
        }
        if(mode_!=stamp.output_fps){clear();result.reset=true;mode_=stamp.output_fps;}
        if(stamp.sequence!=sequence_) {
            result.upload=true;
            const bool continuous=sequence_&&stamp.sequence==sequence_+1&&stamp.source_ns>time_&&stamp.source_ns-time_<=100000000;
            previous_=continuous?time_:0;pair_=continuous;result.reset|=!continuous;
            sequence_=stamp.sequence;time_=stamp.source_ns;
        }
        result.pair=pair_&&stamp.now_ns-time_<=100000000;
        if(result.pair) {
            const double phase=std::clamp(double(stamp.now_ns-time_)/double(time_-previous_),0.0,1.0);
            const unsigned factor=stamp.output_fps/30;
            result.alpha=float(std::min(factor,unsigned(phase*factor)))/factor;
        }
        return result;
    }
private:
    uint64_t sequence_{},time_{},previous_{};bool pair_{};unsigned mode_{};
};
}
