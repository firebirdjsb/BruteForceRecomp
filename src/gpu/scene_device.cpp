#include "scene_device.h"
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <memory>
#include <new>
#include <cstdint>

namespace {
using Microsoft::WRL::ComPtr;
struct SceneDevice {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    HANDLE event{};
    UINT64 serial{};
    ~SceneDevice(){if(event)CloseHandle(event);}
};
// Explicit teardown after the runtime stops the NV2A consumer; no C++ static
// destructor can race that consumer during atexit.
SceneDevice *scene{};
}

extern "C" HRESULT bf_scene_d3d12_create(int warp,ID3D11Device **device,
        ID3D11DeviceContext **context,D3D_FEATURE_LEVEL *level) {
    if(!device||!context||!level)return E_POINTER;
    *device=nullptr;*context=nullptr;
    if(scene)return E_UNEXPECTED;
    std::unique_ptr<SceneDevice> candidate(new(std::nothrow) SceneDevice);
    if(!candidate)return E_OUTOFMEMORY;
    ComPtr<IDXGIFactory4> factory;
    HRESULT result=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(result))return result;
    DXGI_ADAPTER_DESC1 description{};
    if(warp) {
        ComPtr<IDXGIAdapter1> adapter;
        result=factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if(SUCCEEDED(result))result=D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&candidate->device));
        if(SUCCEEDED(result))adapter->GetDesc1(&description);
    } else {
        for(UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> adapter;
            result=factory->EnumAdapters1(i,&adapter);
            if(FAILED(result))break;
            adapter->GetDesc1(&description);
            if(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)continue;
            result=D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&candidate->device));
            if(SUCCEEDED(result))break;
        }
    }
    if(FAILED(result)||!candidate->device)return FAILED(result)?result:DXGI_ERROR_UNSUPPORTED;
    D3D12_COMMAND_QUEUE_DESC queue{};queue.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    result=candidate->device->CreateCommandQueue(&queue,IID_PPV_ARGS(&candidate->queue));
    if(FAILED(result))return result;
    result=candidate->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&candidate->fence));
    if(FAILED(result))return result;
    candidate->event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!candidate->event)return HRESULT_FROM_WIN32(GetLastError());
    IUnknown *queues[]={candidate->queue.Get()};
    const D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D11Device> api;
    ComPtr<ID3D11DeviceContext> immediate;
    result=D3D11On12CreateDevice(candidate->device.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        &requested,1,queues,1,0,&api,&immediate,level);
    if(FAILED(result))return result;
    char name[512]{};
    WideCharToMultiByte(CP_UTF8,0,description.Description,-1,name,sizeof name,nullptr,nullptr);
    std::fprintf(stderr,"[GPU-D3D12] %s: %s; scene draws use D3D11On12 and a D3D12 direct queue\n",warp?"WARP":"hardware",name);
    *device=api.Detach();*context=immediate.Detach();scene=candidate.release();return S_OK;
}

extern "C" HRESULT bf_scene_d3d12_wait(ID3D11DeviceContext *context) {
    if(!scene||!context)return E_UNEXPECTED;
    // Flush the 11On12 translator before signaling its actual D3D12 queue.
    context->Flush();
    if(scene->serial==UINT64_MAX-1)return E_UNEXPECTED;
    const UINT64 value=++scene->serial;
    HRESULT result=scene->queue->Signal(scene->fence.Get(),value);
    if(FAILED(result))return result;
    UINT64 completed=scene->fence->GetCompletedValue();
    if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
    if(completed<value) {
        result=scene->fence->SetEventOnCompletion(value,scene->event);
        if(FAILED(result))return result;
        const DWORD waited=WaitForSingleObject(scene->event,5000);
        if(waited==WAIT_FAILED)return HRESULT_FROM_WIN32(GetLastError());
        if(waited!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        completed=scene->fence->GetCompletedValue();
    }
    if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
    return completed>=value?S_OK:E_FAIL;
}

extern "C" void bf_scene_d3d12_shutdown(void) {delete scene;scene=nullptr;}
