#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>

namespace bf::graphics {
// Native FSR work and presentation share this device and direct queue.
// The 11On12 translator uses a separate queue on this device; unwrap/return
// fences transfer ownership without a same-queue self-wait on older runtimes.
// Clients own the context; the broker holds only a weak reference so COM objects
// are released with their clients rather than during static destruction.
struct Dx12DeviceContext {
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    DXGI_ADAPTER_DESC1 adapter_description{};
    bool warp{}, debug_enabled{};
};
// Throws on initialization failure or a conflicting live hardware/WARP request.
// BF_DX12_DEBUG is observed before creating a device, never enabled afterwards.
std::shared_ptr<Dx12DeviceContext> acquire_dx12_device(bool warp = false);
}
