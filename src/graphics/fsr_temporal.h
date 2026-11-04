#pragma once
#include <d3d12.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bf::graphics {
struct FsrVersion { uint64_t id; std::string name; };
// Inputs are one rendered scene frame, before HUD/text and interpolation.
// Motion is previous minus current position, in unjittered render pixels;
// it must include camera, rigid-object AND skinned-vertex motion.
struct TemporalSceneFrame {
    ID3D12Resource *color{}, *depth{}, *motion{}, *reactive{}, *composition{};
    uint64_t sequence{}, scene{};
    float jitter_x{}, jitter_y{}, elapsed_ms{}, near_plane{}, far_plane{}, vertical_fov{};
    float position[3]{}, up[3]{0,1,0}, right[3]{1,0,0}, forward[3]{0,0,1};
    float meters_per_unit{1};
    bool reset{};
};
struct FsrOptions {
    unsigned render_width{}, render_height{}, output_width{}, output_height{};
    bool inverted_depth{}, nonlinear_color{}, frame_generation{};
    // Zero selects the SDK default. Explicit IDs must come from versions().
    uint64_t upscale_version{}, frame_generation_version{};
};
// Optional AMD signed-binary integration. No synthesized or substituted game
// depth/motion inputs. Call only on the render thread with valid scene data.
class FsrTemporal {
public:
    FsrTemporal(ID3D12Device *, ID3D12CommandQueue *, const std::filesystem::path &dll_directory);
    ~FsrTemporal();
    FsrTemporal(const FsrTemporal &) = delete;
    FsrTemporal &operator=(const FsrTemporal &) = delete;
    std::vector<FsrVersion> versions(bool frame_generation) const;
    void configure(const FsrOptions &);
    std::string upscale_name() const;
    std::string frame_generation_name() const;
    void jitter(unsigned sequence, float &x, float &y) const;
    // All inputs: NON_PIXEL_SHADER_RESOURCE. Both outputs: UAV. The SDK
    // restores these states. Caller rebinds descriptor heaps/pipeline after
    // dispatch. Resources must survive completion of this command list.
    // generated may be null only when frame_generation is false.
    void record(ID3D12GraphicsCommandList *, const TemporalSceneFrame &,
                ID3D12Resource *upscaled, ID3D12Resource *generated = nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
