#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include "interpolation_timeline.h"

namespace bf::graphics {
struct PresentationStats { uint64_t presents{},generated{},source_frames{}; };
PresentationStats dx12_stats();
bool dx12_init(HWND window, uint32_t width, uint32_t height, bool fsr);
bool dx12_active();
bool dx12_frame(const uint32_t *bgra);
bool dx12_frame_timed(const uint32_t *bgra,FrameStamp stamp);
bool dx12_scale(unsigned factor);
void dx12_shutdown();
// Read back this presenter's last rendered output for the standalone GPU test.
bool dx12_readback(std::vector<uint32_t> &rgba, uint32_t &width, uint32_t &height);
}
