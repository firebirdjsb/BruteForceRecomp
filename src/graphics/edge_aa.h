#pragma once
#include <cstdint>
#include <vector>

namespace bf::graphics {
// Spatial, contrast-adaptive directional AA on completed BGRA source frames.
// It needs no depth/history and therefore works before every host presenter.
class EdgeAA {
    std::vector<uint32_t> source_, filtered_;
    std::vector<uint8_t> luma_;
    unsigned width_{},height_{};
    bool ready_{};
public:
    // Repeated presentation of identical source pixels reuses the result.
    // Returns false for invalid dimensions; leaves the caller's frame intact.
    bool apply(uint32_t *pixels,unsigned width,unsigned height);
};
}
