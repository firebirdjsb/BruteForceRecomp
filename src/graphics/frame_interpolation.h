#pragma once
#include "interpolation_timeline.h"
#include <d3d12.h>
#include <memory>
namespace bf::graphics {
class FrameInterpolation {
public:
    FrameInterpolation(ID3D12Device *device,unsigned width,unsigned height);
    ~FrameInterpolation();
    // Source must be COPY_SOURCE. Returned resources are shader-readable.
    ID3D12Resource *record(ID3D12GraphicsCommandList *commands,ID3D12Resource *source,FrameStamp stamp);
    void reset();
    bool generated() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
