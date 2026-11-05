#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_device.h"
#include "dx12_presenter.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace bf::graphics;
namespace {
constexpr UINT width = 64, height = 48;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void check(HRESULT result, const char *message) {
    if (SUCCEEDED(result)) return;
    char detail[256]; std::snprintf(detail, sizeof(detail), "%s (0x%08lX)", message, (unsigned long)result);
    throw std::runtime_error(detail);
}
const GUID lifetime_key = {0xe463f124, 0x2e39, 0x43c4, {0x8a, 0x33, 0xf6, 0x1e, 0x9d, 0x08, 0xae, 0x35}};
struct Lifetime final : IUnknown {
    std::atomic<ULONG> references{1};
    bool *destroyed;
    explicit Lifetime(bool &flag) : destroyed(&flag) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references;
        if (!remaining) { *destroyed = true; delete this; }
        return remaining;
    }
};
ComPtr<ID3D12Resource> texture(ID3D12Device *device, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
                             D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Flags = flags;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(&result)), "Create test texture");
    return result;
}
struct Producer {
    ComPtr<ID3D12Resource> image, upload;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> ready;
    ComPtr<ID3D12CommandQueue> queue;
    Producer(const std::shared_ptr<Dx12DeviceContext> &context, const std::vector<uint32_t> &rgba, bool separate_queue,
             DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
        auto *device = context->device.Get();
        image = texture(device, format);
        if (separate_queue) {
            D3D12_COMMAND_QUEUE_DESC desc{};
            check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "Create test producer queue");
        } else queue = context->queue;
        const auto desc = image->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes{};
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = bytes;
        buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&upload)), "Create test producer upload");
        uint8_t *mapped{}; const D3D12_RANGE no_read{};
        check(upload->Map(0, &no_read, reinterpret_cast<void **>(&mapped)), "Map test upload");
        const size_t row_bytes = size_t(width) * (format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4);
        require(rgba.size() * sizeof(uint32_t) >= row_bytes * height, "Producer upload source dimensions");
        // Typed pixel rows use four bytes for RGBA8 or eight for RGBA16_FLOAT,
        // independently of D3D12's aligned staging-upload pitch.
        for (UINT y = 0; y < height; ++y)
            std::memcpy(mapped + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch,
                reinterpret_cast<const uint8_t *>(rgba.data()) + size_t(y) * row_bytes, row_bytes);
        upload->Unmap(0, nullptr);
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create test allocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
            IID_PPV_ARGS(&commands)), "Create test commands");
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {image.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
        commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.pResource = upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = footprint;
        to.pResource = image.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        commands->ResourceBarrier(1, &barrier);
        check(commands->Close(), "Close test commands");
        ID3D12CommandList *lists[] = {commands.Get()}; queue->ExecuteCommandLists(1, lists);
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ready)), "Create test producer fence");
        // This signal is submitted before presenter Queue::Wait, including on
        // the distinct producer queue; no CPU-signal shortcut satisfies it.
        check(queue->Signal(ready.Get(), 1), "Submit producer-ready signal");
    }
};
std::vector<uint32_t> readback() {
    std::vector<uint32_t> pixels; uint32_t actual_width{}, actual_height{};
    require(dx12_readback(pixels, actual_width, actual_height), "Presenter GPU readback");
    require(actual_width == width && actual_height == height, "Exact output dimensions");
    return pixels;
}
}
int main() {
    _putenv_s("BF_GRAPHICS_READBACK", "1"); _putenv_s("BF_DX12_DEBUG", "1"); _putenv_s("RECOMP_PRESENT_VSYNC", "0");
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BFTexturePresentTest";
    if (!RegisterClassW(&wc)) return 1;
    HWND window = CreateWindowW(wc.lpszClassName, L"GPU texture verification", WS_POPUP,
        0, 0, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 2;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    int result{};
    try {
        require(!dx12_frame_texture({}), "Inactive GPU path must reject input");
        auto context = acquire_dx12_device();
        auto second = acquire_dx12_device();
        require(context == second && context->device.Get() == second->device.Get() &&
            context->queue.Get() == second->queue.Get(), "Live clients must share device and queue identity");
        bool conflict_rejected{};
        try { acquire_dx12_device(true); } catch (const std::exception &) { conflict_rejected = true; }
        require(conflict_rejected, "Hardware/WARP conflict must fail explicitly");
        require(dx12_init(window, width, height, true), "Shared hardware presenter init");
        std::vector<uint32_t> rgba(size_t(width) * height);
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x)
            rgba[size_t(y) * width + x] = 0xff000000u | ((x * 3u) << 16) | ((y * 5u) << 8) | (x + y);
        for (bool separate_queue : {false, true}) {
            Producer producer(context, rgba, separate_queue);
            Dx12TextureFrame frame{}; frame.texture = producer.image.Get(); frame.stamp.sequence = separate_queue ? 2 : 1;
            if (separate_queue) { frame.ready_fence = producer.ready.Get(); frame.ready_value = 1; }
            require(dx12_frame_texture(frame), "Present real producer image");
            require(readback() == rgba, "GPU texture pixels must exactly match producer image");
            // A second call starting in COMMON verifies restoration under the
            // real D3D12 debug layer, rather than trusting an internal counter.
            require(dx12_frame_texture(frame), "Present restored COMMON image again");
            require(readback() == rgba, "Restored texture pixels");
        }
        require(dx12_stats().texture_frames == 4 && dx12_stats().uploads == 0 &&
            dx12_stats().spatial_passes == 0 && dx12_stats().spatial_frames == 0 && dx12_stats().generated == 0, "GPU path must avoid CPU/spatial/interpolation passes");
        {
            std::vector<uint32_t> world(size_t(width) * height, 0xff804020u);
            std::vector<uint32_t> fp16(size_t(width) * height * 2);
            for (size_t i = 0; i < fp16.size(); i += 2) {
                // IEEE binary16: premultiplied RGB (.25,.125,0), coverage .5.
                fp16[i] = 0x30003400u; fp16[i + 1] = 0x38000000u;
            }
            Producer scene(context, world, false), hud(context, fp16, false, DXGI_FORMAT_R16G16B16A16_FLOAT);
            Dx12TextureFrame frame{}; frame.texture = scene.image.Get(); frame.stamp.sequence = 7; frame.source_color_va = 0x80001000u;
            frame.overlay.texture = hud.image.Get(); frame.overlay.sequence = 6; frame.overlay.color_va = frame.source_color_va;
            require(!dx12_frame_texture(frame), "HUD must reject a different source generation");
            frame.overlay.sequence = 7; frame.overlay.color_va += 4;
            require(!dx12_frame_texture(frame), "HUD must reject a different source surface");
            frame.overlay.color_va = frame.source_color_va;
            bool overlay_destroyed{};
            auto *overlay_lifetime = new Lifetime(overlay_destroyed);
            check(hud.image->SetPrivateDataInterface(lifetime_key, overlay_lifetime), "Observe HUD resource lifetime");
            overlay_lifetime->Release();
            require(dx12_frame_texture(frame), "Present temporal output then native HUD");
            hud.image.Reset(); frame.overlay.texture = nullptr;
            require(!overlay_destroyed, "Presenter must retain caller-released HUD until queue completion");
            for (const auto pixel : readback()) for (unsigned shift : {0u,8u,16u,24u})
                require(std::abs(int((pixel >> shift) & 255) - int((0xff404050u >> shift) & 255)) <= 1,
                    "Premultiplied native HUD must compose after reconstructed scene");
            frame.overlay = {};
            require(dx12_frame_texture(frame), "GPU overlay off path");
            require(overlay_destroyed && readback() == world, "Completed HUD must release and off path preserve reconstructed pixels");
            // BGRA8 premultiplied red .25, alpha .5 over the reconstructed
            // world: host F1/keycaps must bypass both temporal reconstruction
            // and the native HUD capture. Repeated sources still update it.
            std::vector<uint32_t> host(size_t(width)*height,0x80400000u);
            frame.host_overlay=host.data();
            require(dx12_frame_texture(frame),"Present live host overlay after temporal scene");
            for(const auto pixel:readback())for(unsigned shift:{0u,8u,16u,24u})
                require(std::abs(int((pixel>>shift)&255)-int((0xff402050u>>shift)&255))<=1,"Host overlay premultiplied BGRA composition");
            std::fill(host.begin(),host.end(),0u);
            require(dx12_frame_texture(frame)&&readback()==world,"Repeated source updates transparent host overlay");
            frame.host_overlay=nullptr;
        }
        auto image = texture(context->device.Get());
        Dx12TextureFrame invalid{}; invalid.texture = image.Get();
        invalid.state = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        require(!dx12_frame_texture(invalid), "Invalid texture state rejection");
        invalid.state = D3D12_RESOURCE_STATE_COMMON; invalid.ready_value = 1;
        require(!dx12_frame_texture(invalid), "Missing ready fence rejection");
        invalid.ready_value = 0;
        auto integer_image = texture(context->device.Get(), DXGI_FORMAT_R8G8B8A8_UINT);
        invalid.texture = integer_image.Get();
        require(!dx12_frame_texture(invalid), "Unsupported integer format rejection");
        // D3D12 permits DENY_SHADER_RESOURCE only on a depth-stencil resource.
        auto denied = texture(context->device.Get(), DXGI_FORMAT_D32_FLOAT,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        invalid.texture = denied.Get();
        require(!dx12_frame_texture(invalid), "Deny-SRV resource rejection");
        // D3D12CreateDevice is a singleton per adapter, so requesting the same
        // hardware adapter again would return the presenter's device identity.
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter1> warp_adapter;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create foreign-adapter factory");
        check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp_adapter)), "Select distinct WARP adapter");
        ComPtr<ID3D12Device> foreign_device;
        check(D3D12CreateDevice(warp_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&foreign_device)), "Create distinct foreign device");
        auto foreign_image = texture(foreign_device.Get()); invalid.texture = foreign_image.Get();
        require(!dx12_frame_texture(invalid), "Foreign device image rejection");
        ComPtr<ID3D12Fence> foreign_fence;
        check(foreign_device->CreateFence(1, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&foreign_fence)), "Create foreign fence");
        invalid.texture = image.Get(); invalid.ready_fence = foreign_fence.Get(); invalid.ready_value = 1;
        require(!dx12_frame_texture(invalid) && dx12_active(), "Foreign ready fence rejection preserves CPU presenter");
        bool destroyed{};
        Producer producer(context, rgba, false);
        auto *lifetime = new Lifetime(destroyed);
        check(producer.image->SetPrivateDataInterface(lifetime_key, lifetime), "Install resource lifetime observer"); lifetime->Release();
        Dx12TextureFrame frame{}; frame.texture = producer.image.Get();
        require(dx12_frame_texture(frame), "Lifetime frame present");
        producer.image.Reset(); frame.texture = nullptr;
        require(!destroyed, "Presenter must retain producer resource after caller releases it");
        require(readback() == rgba, "Caller-released texture must complete real GPU sampling");
        std::vector<uint32_t> bgra(rgba.size(), 0xffc04020u);
        dx12_frame(bgra.data());
        require(dx12_active(), "Ordinary CPU fallback must remain available");
        require(destroyed, "Completed producer resource must be released on next frame");
        const auto cpu = readback();
        for (auto pixel : cpu) require(pixel == 0xff2040c0u, "CPU BGRA channel conversion/FSR1 regression");
        require(dx12_stats().uploads == 1 && dx12_stats().spatial_passes == 1 && dx12_stats().spatial_frames == 1,
            "CPU fallback retains real upload and reports its actual FSR1 presentation");
        dx12_shutdown();
        std::weak_ptr<Dx12DeviceContext> weak = context;
        second.reset(); context.reset();
        require(weak.expired(), "Broker must not own the shared context at exit");
        auto warp = acquire_dx12_device(true);
        require(warp->warp, "WARP can be selected explicitly after hardware clients release context");
        std::puts("PASS shared device/queue, exact GPU pixels, queue-ready fence, restored states, validation, GPU lifetime and CPU/FSR1 fallback");
    } catch (const std::exception &error) { std::fprintf(stderr, "FAIL %s\n", error.what()); result = 1; }
    dx12_shutdown(); DestroyWindow(window); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return result;
}
