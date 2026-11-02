// Test DX12 texture upload -> GPU copy -> readback (WARP, no game assets).
// Validates row pitch, explicit barriers and fence-gated guest visibility.
#ifdef _WIN32
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "gpu/d3d12_fence_bridge.h"
using Microsoft::WRL::ComPtr;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "DX12 texture round-trip failed at line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main() {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        std::puts("SKIP: DXGI unavailable");
        return 77;
    }
    ComPtr<IDXGIAdapter> warp;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
        std::puts("SKIP: WARP DX12 adapter unavailable");
        return 77;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::puts("SKIP: WARP DX12 feature level unavailable");
        return 77;
    }
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    CHECK(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    CHECK(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&allocator))));
    CHECK(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
    CHECK(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));

    constexpr UINT width = 7;
    constexpr UINT height = 5;
    constexpr UINT bytesPerPixel = 4;
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 total = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows,
                                  &rowBytes, &total);
    CHECK(rows == height && rowBytes == width * bytesPerPixel);
    CHECK(footprint.Footprint.RowPitch >= rowBytes && total > 0);

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = total;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    // Explicit single-node masks also work with the software WARP adapter.
    for (D3D12_HEAP_PROPERTIES *properties : {&defaultHeap, &uploadHeap, &readbackHeap}) {
        properties->CreationNodeMask = 1;
        properties->VisibleNodeMask = 1;
    }
    ComPtr<ID3D12Resource> texture, upload, readback;
    CHECK(SUCCEEDED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,
       &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))));
    CHECK(SUCCEEDED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
       &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))));
    CHECK(SUCCEEDED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
       &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))));

    std::uint8_t expected[width * height * bytesPerPixel] = {};
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width * bytesPerPixel; ++x)
            expected[y * width * bytesPerPixel + x] =
                static_cast<std::uint8_t>((x * 19 + y * 37 + 9) & 255);
    std::uint8_t *mapped = nullptr;
    CHECK(SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped))));
    for (UINT y = 0; y < height; ++y)
        std::memcpy(mapped + footprint.Offset + y * footprint.Footprint.RowPitch,
                    expected + y * width * bytesPerPixel, width * bytesPerPixel);
    upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION fromUpload = {};
    fromUpload.pResource = upload.Get();
    fromUpload.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    fromUpload.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION toTexture = {};
    toTexture.pResource = texture.Get();
    toTexture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    toTexture.SubresourceIndex = 0;
    list->CopyTextureRegion(&toTexture, 0, 0, 0, &fromUpload, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION fromTexture = toTexture;
    D3D12_TEXTURE_COPY_LOCATION toReadback = fromUpload;
    toReadback.pResource = readback.Get();
    list->CopyTextureRegion(&toReadback, 0, 0, 0, &fromTexture, nullptr);
    CHECK(SUCCEEDED(list->Close()));
    ID3D12CommandList *lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);

    bf::gpu::GuestCompletionTimeline timeline;
    CHECK(SUCCEEDED(bf::gpu::signal_gpu_queue(queue.Get(), fence.Get(), timeline, 1)));
    CHECK(timeline.enqueue_signal(1, 0x80001000u, 0x1234u));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(event != nullptr);
    HRESULT hr = fence->SetEventOnCompletion(1, event);
    DWORD wait = SUCCEEDED(hr) ? WaitForSingleObject(event, 10000) : WAIT_FAILED;
    CloseHandle(event);
    CHECK(SUCCEEDED(hr) && wait == WAIT_OBJECT_0);
    std::uint32_t signalValue = 0;
    CHECK(bf::gpu::poll_gpu_queue(fence.Get(), timeline,
        [&](std::uint32_t address, std::uint32_t value) {
            if (address != 0x80001000u) return false;
            signalValue = value;
            return true;
        }) == bf::gpu::RetireResult::ok);
    CHECK(signalValue == 0x1234u && timeline.pending() == 0);

    void *readMapped = nullptr;
    D3D12_RANGE readRange = {0, static_cast<SIZE_T>(total)};
    CHECK(SUCCEEDED(readback->Map(0, &readRange, &readMapped)));
    const auto *output = static_cast<const std::uint8_t *>(readMapped);
    bool same = true;
    for (UINT y = 0; y < height; ++y)
        if (std::memcmp(output + footprint.Offset + y * footprint.Footprint.RowPitch,
                        expected + y * width * bytesPerPixel, width * bytesPerPixel) != 0)
            same = false;
    D3D12_RANGE written = {0, 0};
    readback->Unmap(0, &written);
    CHECK(same);
    std::puts("D3D12 WARP upload, resource barrier, readback and fence passed");
    return 0;
}
#endif
