// Synthetic DX12/D3D11On12 compatibility smoke; no game XBE or guest code.
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "gpu/d3d12_fence_bridge.h"

using Microsoft::WRL::ComPtr;
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "D3D12 compatibility check failed: %s at line %d\n", #condition, __LINE__); return 1; } } while (0)

int main() {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        std::puts("SKIP: no DXGI factory on this test runner");
        return 77;
    }
    ComPtr<IDXGIAdapter> warp;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
        std::puts("SKIP: no DX12 WARP adapter on this test runner");
        return 77;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::puts("SKIP: WARP D3D12 device unsupported on this test runner");
        return 77;
    }
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    REQUIRE(SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12Fence> fence;
    REQUIRE(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));

    // Verify an actual D3D11 device can target this D3D12 queue. This checks
    // the interop entry point, NOT that our game renderer is connected yet.
    ComPtr<ID3D11Device> device11;
    ComPtr<ID3D11DeviceContext> context11;
    IUnknown *queues[] = {queue.Get()};
    REQUIRE(SUCCEEDED(D3D11On12CreateDevice(device.Get(),
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, queues, 1, 0,
        device11.GetAddressOf(), context11.GetAddressOf(), nullptr)));

    bf::gpu::GuestCompletionTimeline timeline;
    REQUIRE(SUCCEEDED(bf::gpu::signal_gpu_queue(queue.Get(), fence.Get(), timeline, 1)));
    REQUIRE(timeline.enqueue_signal(1, 0x80002000u, 42));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    REQUIRE(event != nullptr);
    HRESULT status = fence->SetEventOnCompletion(1, event);
    DWORD wait = SUCCEEDED(status) ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
    CloseHandle(event);
    REQUIRE(SUCCEEDED(status) && wait == WAIT_OBJECT_0);
    std::uint32_t observed = 0;
    std::size_t retired = 0;
    REQUIRE(bf::gpu::poll_gpu_queue(fence.Get(), timeline,
        [&](std::uint32_t address, std::uint32_t value) {
            if (address != 0x80002000u) return false;
            observed = value;
            return true;
        }, &retired) == bf::gpu::RetireResult::ok);
    REQUIRE(retired == 1 && observed == 42);
    std::puts("D3D12 WARP fence and D3D11On12 interoperability smoke passed");
    return 0;
}
#endif
