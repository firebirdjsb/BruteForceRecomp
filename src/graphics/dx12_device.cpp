#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_device.h"
#include <d3d12sdklayers.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

namespace bf::graphics {
namespace {
std::mutex broker_mutex;
std::weak_ptr<Dx12DeviceContext> broker;
bool debug_configured{}, debug_enabled{};
void check(HRESULT result, const char *operation) {
    if (SUCCEEDED(result)) return;
    char message[192];
    std::snprintf(message, sizeof(message), "%s failed (0x%08lX)", operation, (unsigned long)result);
    throw std::runtime_error(message);
}
}
std::shared_ptr<Dx12DeviceContext> acquire_dx12_device(bool warp) {
    std::lock_guard<std::mutex> lock(broker_mutex);
    if (auto existing = broker.lock()) {
        if (existing->warp != warp) throw std::runtime_error("Conflicting live hardware/WARP DX12 device request");
        check(existing->device->GetDeviceRemovedReason(), "Shared DX12 device removed");
        return existing;
    }
    auto context = std::make_shared<Dx12DeviceContext>();
    context->warp = warp;
    // Configure the process-wide debug layer at most once. Even after the last
    // context expires, exported resources could still retain an earlier device;
    // enabling the layer again then can remove that live device.
    if (!debug_configured && std::getenv("BF_DX12_DEBUG")) {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            debug_enabled = true;
            std::fprintf(stderr, "[DX12] debug validation enabled before shared device creation\n");
        } else std::fprintf(stderr, "[DX12] optional debug layer unavailable\n");
    }
    debug_configured = true;
    context->debug_enabled = debug_enabled;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create shared DXGI factory");
    if (warp) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "Select WARP adapter");
        check(adapter->GetDesc1(&context->adapter_description), "Describe WARP adapter");
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&context->device)), "Create WARP device");
    } else {
        // Preserve the presenter's first-compatible hardware-adapter policy.
        // Software rendering is selected only by an explicit WARP request.
        for (UINT index = 0; ; ++index) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = factory->EnumAdapters1(index, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            check(result, "Enumerate hardware adapter");
            DXGI_ADAPTER_DESC1 description{};
            check(adapter->GetDesc1(&description), "Describe hardware adapter");
            if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&context->device)))) {
                context->adapter_description = description;
                break;
            }
        }
        if (!context->device) throw std::runtime_error("No compatible hardware DX12 adapter");
    }
    D3D12_COMMAND_QUEUE_DESC description{};
    description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(context->device->CreateCommandQueue(&description, IID_PPV_ARGS(&context->queue)), "Create shared direct queue");
    std::fprintf(stderr, "[DX12] shared %s device: %ls\n", warp ? "WARP" : "hardware", context->adapter_description.Description);
    broker = context;
    return context;
}
}
