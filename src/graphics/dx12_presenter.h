#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include "interpolation_timeline.h"
#include <d3d12.h>

namespace bf::graphics {
struct PresentationStats {
    uint64_t presents{},generated{},source_frames{};
    uint64_t uploads{},spatial_passes{};
    uint64_t texture_frames{};
    uint64_t spatial_frames{}; // Includes repeated displays of a cached FSR1 image.
};
// A completed producer image on the shared DX12 device. The producer declares
// its current state and the state required after sampling; all subresources
// must have that state. Only a single typed, non-MSAA texture2D is accepted.
// Subsequent producer reuse must be ordered after sampling on the shared queue,
// or wait for that queue's completion before using it on another queue.
// Optional ready_fence/value transfers ordering from a different producer
// queue. Both must be supplied together; same-direct-queue work needs neither.
// That value's Signal must already be submitted before calling this API;
// otherwise a shared-queue Wait could block the producer that must signal it.
// The presenter retains the image and fence until its GPU completion fence.
struct Dx12UiOverlay {
    ID3D12Resource *texture{}; // RGBA16_FLOAT premultiplied RGB + coverage.
    uint64_t sequence{};
    uint32_t color_va{};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};
    D3D12_RESOURCE_STATES restore_state{D3D12_RESOURCE_STATE_COMMON};
};
struct Dx12TextureFrame {
    ID3D12Resource *texture{};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};
    D3D12_RESOURCE_STATES restore_state{D3D12_RESOURCE_STATE_COMMON};
    ID3D12Fence *ready_fence{};
    uint64_t ready_value{};
    FrameStamp stamp{};
    uint32_t source_color_va{};
    // Optional native HUD from this exact source surface/generation. The frame
    // ready fence, if supplied, must order both images. Overlay dimensions are
    // the original framebuffer dimensions, independent of temporal output size.
    Dx12UiOverlay overlay{};
    // Optional live host F1/keycap layer, premultiplied BGRA8 at the original
    // framebuffer dimensions. Copied synchronously during this call, then
    // composited after the reconstructed scene and its native HUD.
    const uint32_t *host_overlay{};
};
PresentationStats dx12_stats();
bool dx12_init(HWND window, uint32_t width, uint32_t height, bool fsr);
bool dx12_active();
bool dx12_frame(const uint32_t *bgra);
bool dx12_frame_timed(const uint32_t *bgra,FrameStamp stamp);
// Presents the producer's actual GPU pixels directly, without CPU upload,
// spatial upscaling or optical-flow interpolation. Returns presentation success
// (unlike the legacy CPU entry points, whose result reports vsync pacing).
// Invalid input is rejected without disabling the existing presenter.
bool dx12_frame_texture(const Dx12TextureFrame &frame);
bool dx12_scale(unsigned factor);
// The selected frame is a 640x480 anamorphic rendering of a true 16:9 camera.
// Source texture dimensions stay unchanged; only its presentation aspect changes.
bool dx12_aspect(bool wide);
void dx12_shutdown();
// Read back this presenter's last rendered output for the standalone GPU test.
bool dx12_readback(std::vector<uint32_t> &rgba, uint32_t &width, uint32_t &height);
}
