#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace bf::graphics {
bool dx12_init(HWND window, uint32_t width, uint32_t height, bool fsr);
bool dx12_active();
bool dx12_frame(const uint32_t *bgra);
bool dx12_scale(unsigned factor);
void dx12_shutdown();
// Read back this presenter's last rendered output for the standalone GPU test.
bool dx12_readback(std::vector<uint32_t> &rgba, uint32_t &width, uint32_t &height);
}
