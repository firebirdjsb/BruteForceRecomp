#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_presenter.h"
#include "shaders.h"
#include "frame_interpolation.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#define A_CPU 1
#include "ffx_a.h"
#include "ffx_fsr1.h"

namespace bf::graphics {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        char message[192];
        std::snprintf(message, sizeof(message), "%s failed (0x%08lX)", operation, (unsigned long)result);
        throw std::runtime_error(message);
    }
}
struct Includes final : ID3DInclude {
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID *data, UINT *bytes) override {
        if (!std::strcmp(name, "ffx_a.h")) { *data = shaders::ffx_a.data(); *bytes = static_cast<UINT>(shaders::ffx_a.size()); }
        else if (!std::strcmp(name, "ffx_fsr1.h")) { *data = shaders::ffx_fsr1.data(); *bytes = static_cast<UINT>(shaders::ffx_fsr1.size()); }
        else return E_FAIL;
        return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};
ComPtr<ID3DBlob> compile(const char *entry, const char *profile, const char *define = nullptr) {
    Includes includes;
    D3D_SHADER_MACRO macros[] = {{define, "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, errors;
    const auto result = D3DCompile(shaders::source, sizeof(shaders::source) - 1, "BruteForce-present",
        define ? macros : nullptr, &includes, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result) && errors) std::fprintf(stderr, "[DX12] shader: %s\n", (const char *)errors->GetBufferPointer());
    check(result, entry);
    return code;
}
D3D12_RESOURCE_DESC texture_desc(UINT width, UINT height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Flags = flags;
    return desc;
}
struct Presenter {
    HWND window{};
    UINT width{}, height{}, back_width{}, back_height{}, output_width{}, output_height{};
    UINT scale_factor{},compute_width{},compute_height{};
    UINT descriptor_size{}, rtv_size{}, last_back{};
    bool fsr{}, vsync{true}, valid{}, has_frame{}, capture{};
    UINT64 serial{};
    HANDLE event{};
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue1> debug_messages;
    DWORD debug_cookie{};
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12DescriptorHeap> descriptors, rtvs;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> draw, easu, rcas;
    ComPtr<ID3D12Resource> input, upload, intermediate, scaled, readback_buffer;
    std::array<ComPtr<ID3D12Resource>, 2> back;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT upload_layout{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readback_layout{};
    UINT64 readback_bytes{};
    D3D12_RESOURCE_STATES input_state{D3D12_RESOURCE_STATE_COPY_DEST};
    std::unique_ptr<FrameInterpolation> interpolation;
    PresentationStats stats;uint64_t last_source{};
    std::vector<uint32_t> uploaded_pixels;
    ID3D12Resource *scaled_source{};
    uint64_t scaled_sequence{};
    bool scaled_valid{};

    ~Presenter() {
        // All resources referenced by a command list remain owned until its fence.
        try { wait(); } catch (const std::exception &error) { std::fprintf(stderr, "[DX12] shutdown: %s\n", error.what()); }
        if (debug_messages) debug_messages->UnregisterMessageCallback(debug_cookie);
        if (event) CloseHandle(event);
    }
    void wait() {
        if (!fence || !serial) return;
        check(device->GetDeviceRemovedReason(), "GPU device removed");
        if (fence->GetCompletedValue() >= serial) return;
        check(fence->SetEventOnCompletion(serial, event), "SetEventOnCompletion");
        if (WaitForSingleObject(event, 5000) != WAIT_OBJECT_0) {
            check(device->GetDeviceRemovedReason(), "GPU device removed");
            throw std::runtime_error("GPU fence exceeded five seconds");
        }
        check(device->GetDeviceRemovedReason(), "GPU device removed");
    }
    void submit() {
        check(commands->Close(), "Close command list");
        ID3D12CommandList *lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        check(queue->Signal(fence.Get(), ++serial), "Signal fence");
    }
    void begin() {
        wait();
        check(allocator->Reset(), "Reset allocator");
        check(commands->Reset(allocator.Get(), nullptr), "Reset command list");
    }
    void transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        if (before == after) return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        commands->ResourceBarrier(1, &barrier);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(UINT index) const {
        auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += SIZE_T(index) * descriptor_size; return handle;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE gpu(UINT index) const {
        auto handle = descriptors->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += UINT64(index) * descriptor_size; return handle;
    }
    ComPtr<ID3D12Resource> resource(D3D12_RESOURCE_DESC desc, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES properties{}; properties.Type = heap;
        properties.CreationNodeMask = properties.VisibleNodeMask = 1;
        ComPtr<ID3D12Resource> result;
        check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state,
            nullptr, IID_PPV_ARGS(&result)), "Create resource");
        return result;
    }
    ComPtr<ID3D12Resource> buffer(UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = bytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return resource(desc, heap, state);
    }
    void srv(ID3D12Resource *texture, UINT slot) {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{}; desc.Format = texture->GetDesc().Format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; desc.Texture2D.MipLevels = 1;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        device->CreateShaderResourceView(texture, &desc, cpu(slot));
    }
    void uav(ID3D12Resource *texture, UINT slot) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{}; desc.Format = texture->GetDesc().Format;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(texture, nullptr, &desc, cpu(slot));
    }
    void pipelines() {
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[3]{};
        for (UINT i = 0; i < 2; ++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i].DescriptorTable = {1, &ranges[i]};
        }
        // Four uint4 FSR constants followed by output dimensions and padding: 80 bytes.
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[2].Constants = {0, 0, 20};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX; sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC signature{3, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> blob, errors;
        check(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "Serialize root signature");
        check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "Create root signature");
        auto vertex = compile("vertexMain", "vs_5_1"), pixel = compile("pixelMain", "ps_5_1");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = root.Get();
        pipeline.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        pipeline.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        auto &blend = pipeline.BlendState.RenderTarget[0];
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        pipeline.SampleMask = UINT_MAX;
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.StencilReadMask = pipeline.DepthStencilState.StencilWriteMask = 0xff;
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1; pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline.SampleDesc.Count = 1;
        check(device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&draw)), "Create draw pipeline");
        if (fsr) {
            auto make = [&](const char *define, ComPtr<ID3D12PipelineState> &target) {
                auto shader = compile("computeMain", "cs_5_1", define);
                D3D12_COMPUTE_PIPELINE_STATE_DESC compute{}; compute.pRootSignature = root.Get();
                compute.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
                check(device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&target)), "Create FSR pipeline");
            };
            make("BF_EASU", easu); make("BF_RCAS", rcas);
        }
    }
    void resize(UINT w, UINT h) {
        scaled_valid=false;scaled_source=nullptr;
        wait(); has_frame = false;
        if(interpolation)interpolation->reset();
        for (auto &image : back) image.Reset();
        intermediate.Reset(); scaled.Reset(); readback_buffer.Reset();
        check(swap->ResizeBuffers(2, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "Resize swap chain");
        back_width = w; back_height = h;
        auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < 2; ++i) {
            check(swap->GetBuffer(i, IID_PPV_ARGS(&back[i])), "Get back buffer");
            device->CreateRenderTargetView(back[i].Get(), nullptr, rtv); rtv.ptr += rtv_size;
        }
        if (capture) {
            const auto desc = back[0]->GetDesc();
            device->GetCopyableFootprints(&desc, 0, 1, 0, &readback_layout, nullptr, nullptr, &readback_bytes);
            readback_buffer = buffer(readback_bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        }
        const double scale = std::min(double(w) / width, double(h) / height);
        output_width = std::max(1u, UINT(std::lround(width * scale)));
        output_height = std::max(1u, UINT(std::lround(height * scale)));
        compute_width=scale_factor?width*scale_factor:output_width;
        compute_height=scale_factor?height*scale_factor:output_height;
        if(compute_width>16384||compute_height>16384)throw std::runtime_error("Upscale target exceeds the device texture limit");
        if (fsr && output_width >= width && output_height >= height) {
            const auto flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            intermediate = resource(texture_desc(compute_width, compute_height, DXGI_FORMAT_R16G16B16A16_FLOAT, flags),
                D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scaled = resource(texture_desc(compute_width, compute_height, DXGI_FORMAT_R8G8B8A8_UNORM, flags),
                D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            // Descriptor slots: input SRV, EASU UAV/SRV, RCAS UAV/SRV.
            uav(intermediate.Get(), 1); srv(intermediate.Get(), 2);
            uav(scaled.Get(), 3); srv(scaled.Get(), 4);
        }
        std::fprintf(stderr, "[DX12] output %ux%u in %ux%u window; scaling=%s\n", output_width, output_height, w, h,
            scaled ? "FSR1 EASU + RCAS" : "bilinear");
        if(scaled)std::fprintf(stderr,"[DX12] FSR source %ux%u -> %ux%u (%s)\n",width,height,compute_width,compute_height,scale_factor?"fixed factor":"match window");
    }
    void initialize(HWND hwnd, UINT w, UINT h, bool use_fsr) {
        if (!IsWindow(hwnd) || !w || !h || w > 8192 || h > 8192) throw std::runtime_error("Invalid framebuffer/window");
        window = hwnd; width = w; height = h; fsr = use_fsr;
        capture = std::getenv("BF_GRAPHICS_READBACK") != nullptr;
        if (std::getenv("BF_DX12_DEBUG")) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer(); std::fprintf(stderr, "[DX12] debug validation enabled\n");
            } else std::fprintf(stderr, "[DX12] optional debug layer unavailable\n");
        }
        if (const char *v = std::getenv("RECOMP_PRESENT_VSYNC")) vsync = *v != '0';
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Create DXGI factory");
        ComPtr<IDXGIAdapter1> selected;
        for (UINT i = 0; ; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            if (!adapter) break;
            DXGI_ADAPTER_DESC1 desc{}; adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
                selected = adapter;
                std::fprintf(stderr, "[DX12] hardware: %ls\n", desc.Description); break;
            }
        }
        if (!device) throw std::runtime_error("No compatible hardware DX12 adapter");
        if (std::getenv("BF_DX12_DEBUG") && SUCCEEDED(device.As(&debug_messages))) {
            check(debug_messages->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
                D3D12_MESSAGE_ID id, LPCSTR description, void *) {
                if (severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                    std::fprintf(stderr, "[DX12] debug %u: %s\n", unsigned(id), description);
            }, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &debug_cookie), "Register debug messages");
        }
        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "Create command queue");
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create allocator");
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)), "Create command list");
        check(commands->Close(), "Close initial command list");
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) throw std::runtime_error("Cannot create GPU fence event");
        DXGI_SWAP_CHAIN_DESC1 swap_desc{};
        swap_desc.Width = w; swap_desc.Height = h; swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_desc.SampleDesc.Count = 1; swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = 2; swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initial;
        check(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &swap_desc, nullptr, nullptr, &initial), "Create swap chain");
        check(initial.As(&swap), "Query swap chain");
        check(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "Window association");
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.NumDescriptors = 5;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors)), "Create SRV/UAV heap");
        descriptor_size = device->GetDescriptorHandleIncrementSize(heap.Type);
        heap.NumDescriptors = 2; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvs)), "Create RTV heap");
        rtv_size = device->GetDescriptorHandleIncrementSize(heap.Type);
        auto desc = texture_desc(w, h, DXGI_FORMAT_B8G8R8A8_UNORM);
        input = resource(desc, D3D12_HEAP_TYPE_DEFAULT, input_state); srv(input.Get(), 0);
        UINT64 bytes{}; device->GetCopyableFootprints(&desc, 0, 1, 0, &upload_layout, nullptr, nullptr, &bytes);
        upload = buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        pipelines();
        RECT rect{}; if (!GetClientRect(window, &rect)) throw std::runtime_error("Cannot read client size");
        resize(std::max(1L, rect.right), std::max(1L, rect.bottom));
        valid = true;
    }
    bool frame(const uint32_t *bgra,FrameStamp stamp={}) {
        if (!valid || !bgra) return false;
        RECT rect{}; if (!GetClientRect(window, &rect)) throw std::runtime_error("Cannot read client size");
        if (rect.right <= 0 || rect.bottom <= 0 || IsIconic(window)) return false;
        if (UINT(rect.right) != back_width || UINT(rect.bottom) != back_height) resize(rect.right, rect.bottom);
        begin();
        const size_t pixels=SIZE_T(width)*height,bytes=pixels*sizeof(uint32_t);
        // The compositor can change F1/keycaps while a source ID repeats.
        // Compare every byte rather than treating that ID as pixel identity.
        const bool changed=uploaded_pixels.size()!=pixels||std::memcmp(uploaded_pixels.data(),bgra,bytes)!=0;
        if(changed) {
            uint8_t *mapped{}; const D3D12_RANGE no_read{0, 0};
            check(upload->Map(0, &no_read, reinterpret_cast<void **>(&mapped)), "Map frame upload");
            // Each source row is width * 4 bytes; upload pitch is 256-byte aligned.
            for (UINT y = 0; y < height; ++y)
                std::memcpy(mapped + upload_layout.Offset + SIZE_T(y) * upload_layout.Footprint.RowPitch, bgra + SIZE_T(y) * width, SIZE_T(width) * 4);
            upload->Unmap(0, nullptr);
            transition(input.Get(), input_state, D3D12_RESOURCE_STATE_COPY_DEST);
            input_state=D3D12_RESOURCE_STATE_COPY_DEST;
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = upload_layout;
            D3D12_TEXTURE_COPY_LOCATION dest{}; dest.pResource = input.Get(); dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commands->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
            uploaded_pixels.assign(bgra,bgra+pixels);++stats.uploads;
        }
        ID3D12Resource *sampled=input.Get();
        bool generated=false;
        if(stamp.output_fps==60||stamp.output_fps==120) {
            if(!interpolation)interpolation=std::make_unique<FrameInterpolation>(device.Get(),width,height);
            transition(input.Get(),input_state,D3D12_RESOURCE_STATE_COPY_SOURCE);
            input_state=D3D12_RESOURCE_STATE_COPY_SOURCE;
            if(auto *image=interpolation->record(commands.Get(),input.Get(),stamp))sampled=image;
            generated=interpolation->generated();
        } else if(interpolation)interpolation->reset();
        srv(sampled,0);
        const auto readable = scaled ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        transition(input.Get(), input_state, readable);input_state=readable;
        ID3D12DescriptorHeap *heaps[] = {descriptors.Get()}; commands->SetDescriptorHeaps(1, heaps);
        if (scaled && (!scaled_valid||changed||sampled!=scaled_source||generated||
                       (sampled!=input.Get()&&stamp.sequence!=scaled_sequence))) {
            std::array<uint32_t, 20> constants{};
            FsrEasuCon(constants.data(), constants.data() + 4, constants.data() + 8, constants.data() + 12,
                float(width), float(height), float(width), float(height), float(compute_width), float(compute_height));
            constants[16] = compute_width; constants[17] = compute_height;
            commands->SetComputeRootSignature(root.Get()); commands->SetPipelineState(easu.Get());
            commands->SetComputeRootDescriptorTable(0, gpu(0)); commands->SetComputeRootDescriptorTable(1, gpu(1));
            commands->SetComputeRoot32BitConstants(2, 20, constants.data(), 0);
            transition(intermediate.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands->Dispatch((compute_width + 7) / 8, (compute_height + 7) / 8, 1);
            transition(intermediate.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            FsrRcasCon(constants.data(), 0.2f);
            commands->SetPipelineState(rcas.Get());
            commands->SetComputeRootDescriptorTable(0, gpu(2)); commands->SetComputeRootDescriptorTable(1, gpu(3));
            commands->SetComputeRoot32BitConstants(2, 20, constants.data(), 0);
            transition(scaled.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commands->Dispatch((compute_width + 7) / 8, (compute_height + 7) / 8, 1);
            transition(scaled.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            scaled_valid=true;scaled_source=sampled;scaled_sequence=stamp.sequence;++stats.spatial_passes;
        }
        last_back = swap->GetCurrentBackBufferIndex();
        transition(back[last_back].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += SIZE_T(last_back) * rtv_size;
        const float black[4] = {0, 0, 0, 1}; commands->ClearRenderTargetView(rtv, black, 0, nullptr);
        commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT viewport{float((back_width - output_width) / 2), float((back_height - output_height) / 2),
            float(output_width), float(output_height), 0, 1};
        D3D12_RECT scissor{0, 0, LONG(back_width), LONG(back_height)};
        commands->RSSetViewports(1, &viewport); commands->RSSetScissorRects(1, &scissor);
        commands->SetGraphicsRootSignature(root.Get()); commands->SetPipelineState(draw.Get());
        commands->SetGraphicsRootDescriptorTable(0, gpu(scaled ? 4 : 0));
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commands->DrawInstanced(3, 1, 0, 0);
        if (readback_buffer) {
            // Copy before Present: FLIP_DISCARD does not preserve a presented back buffer.
            transition(back[last_back].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource = back[last_back].Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource = readback_buffer.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = readback_layout;
            commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            transition(back[last_back].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
        } else transition(back[last_back].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        submit();
        const auto result = swap->Present(vsync ? 1 : 0, 0);
        // Present can enqueue additional uses of the swap-chain resources.
        // Resize/shutdown must wait for these too, not just the draw submission.
        check(queue->Signal(fence.Get(), ++serial), "Signal present fence");
        check(result, "Present"); has_frame = result != DXGI_STATUS_OCCLUDED;
        if(has_frame) {
            stats.presents++;stats.generated+=generated;
            if(stamp.sequence&&stamp.sequence!=last_source){stats.source_frames++;last_source=stamp.sequence;}
        }
        return has_frame && vsync;
    }
    void readback(std::vector<uint32_t> &rgba, uint32_t &w, uint32_t &h) {
        if (!has_frame || !readback_buffer) throw std::runtime_error("Readback requires BF_GRAPHICS_READBACK and a rendered frame");
        wait();
        uint8_t *mapped{}; const D3D12_RANGE range{0, SIZE_T(readback_bytes)};
        check(readback_buffer->Map(0, &range, reinterpret_cast<void **>(&mapped)), "Map GPU readback");
        w = back_width; h = back_height; rgba.resize(SIZE_T(w) * h);
        for (UINT y = 0; y < h; ++y) std::memcpy(rgba.data() + SIZE_T(y) * w, mapped + readback_layout.Offset + SIZE_T(y) * readback_layout.Footprint.RowPitch, SIZE_T(w) * 4);
        const D3D12_RANGE no_write{0, 0}; readback_buffer->Unmap(0, &no_write);
        ComPtr<ID3D12InfoQueue> messages;
        if (SUCCEEDED(device.As(&messages))) {
            bool failed = false;
            for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i) {
                SIZE_T bytes = 0; messages->GetMessage(i, nullptr, &bytes);
                std::vector<uint8_t> storage(bytes);
                auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &bytes)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                    std::fprintf(stderr, "[DX12] validation error: %s\n", message->pDescription); failed = true;
                }
            }
            messages->ClearStoredMessages();
            if (failed) throw std::runtime_error("D3D12 debug validation reported GPU errors");
        }
    }
};
std::unique_ptr<Presenter> presenter;
}
bool dx12_init(HWND window, uint32_t width, uint32_t height, bool fsr) {
    dx12_shutdown();
    try {
        auto candidate = std::make_unique<Presenter>(); candidate->initialize(window, width, height, fsr);
        presenter = std::move(candidate); return true;
    } catch (const std::exception &error) { std::fprintf(stderr, "[DX12] %s; falling back\n", error.what()); return false; }
}
bool dx12_active() { return presenter && presenter->valid; }
PresentationStats dx12_stats(){return presenter?presenter->stats:PresentationStats{};}
bool dx12_scale(unsigned factor) {
    if(!dx12_active() || (factor!=0&&factor!=2&&factor!=4&&factor!=8))return false;
    if(presenter->scale_factor==factor)return true;
    try {
        presenter->scale_factor=factor;
        presenter->resize(presenter->back_width,presenter->back_height);return true;
    } catch(const std::exception &error) {
        std::fprintf(stderr,"[DX12] upscale change: %s; disabling presenter\n",error.what());presenter.reset();return false;
    }
}
bool dx12_frame(const uint32_t *bgra) {
    if (!dx12_active()) return false;
    try { return presenter->frame(bgra); }
    catch (const std::exception &error) {
        std::fprintf(stderr, "[DX12] %s; disabling presenter\n", error.what()); presenter.reset(); return false;
    }
}
bool dx12_frame_timed(const uint32_t *bgra,FrameStamp stamp) {
    if(!dx12_active())return false;
    try{return presenter->frame(bgra,stamp);}
    catch(const std::exception &error){std::fprintf(stderr,"[DX12] interpolation: %s\n",error.what());presenter.reset();return false;}
}
void dx12_shutdown() { presenter.reset(); }
bool dx12_readback(std::vector<uint32_t> &rgba, uint32_t &width, uint32_t &height) {
    if (!dx12_active()) return false;
    try { presenter->readback(rgba, width, height); return true; }
    catch (const std::exception &error) { std::fprintf(stderr, "[DX12] readback: %s\n", error.what()); return false; }
}
}
