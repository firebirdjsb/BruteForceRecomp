#include "scene_device.h"
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <cstdint>
#include <vector>
#include <exception>

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
void access_trace(const char *stage,HRESULT result=S_OK) {
    static const bool enabled=[](){const char *value=std::getenv("BF_SCENE_D3D12_TRACE");return value&&!std::strcmp(value,"1");}();
    if(enabled){std::fprintf(stderr,"[SCENE-ACCESS] %s (%08lX)\n",stage,(unsigned long)result);std::fflush(stderr);}
}
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

extern "C" HRESULT bf_scene_d3d12_access(ID3D11DeviceContext *context,
        ID3D11Resource *const *resources,UINT count,BfSceneD3D12Work work,void *user) {
    if(!scene)return DXGI_ERROR_UNSUPPORTED;
    if(!context||!resources||!count||count>16||!work)return E_INVALIDARG;
    if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return E_INVALIDARG;
    if(scene->serial>=UINT64_MAX-1)return E_UNEXPECTED;
    ComPtr<ID3D11Device> device11;context->GetDevice(&device11);
    ComPtr<ID3D11On12Device2> bridge;HRESULT result=device11.As(&bridge);
    if(FAILED(result))return result;
    for(UINT i=0;i<count;++i) {
        if(!resources[i])return E_POINTER;
        ComPtr<ID3D11Device> owner;resources[i]->GetDevice(&owner);
        if(owner.Get()!=device11.Get())return E_INVALIDARG;
        D3D11_RESOURCE_DIMENSION dimension;resources[i]->GetType(&dimension);
        if(dimension!=D3D11_RESOURCE_DIMENSION_TEXTURE2D)return E_INVALIDARG;
        for(UINT j=0;j<i;++j)if(resources[i]==resources[j])return E_INVALIDARG;
    }
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    result=scene->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator));
    if(FAILED(result))return result;
    result=scene->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands));
    if(FAILED(result))return result;
    ComPtr<ID3D12Resource> native[16];ID3D12Resource *borrowed[16]{};UINT acquired=0;
    for(;acquired<count;++acquired) {
        result=bridge->UnwrapUnderlyingResource(resources[acquired],scene->queue.Get(),IID_PPV_ARGS(&native[acquired]));
        if(FAILED(result))break;
        borrowed[acquired]=native[acquired].Get();
    }
    // Unwrap can schedule translation work but does not flush it. Submit that
    // work before executing native commands or waiting on their queue fence.
    context->Flush();
    if(SUCCEEDED(result)) {
        try {result=work(scene->device.Get(),scene->queue.Get(),commands.Get(),borrowed,count,user);}
        catch(const std::exception &e){std::fprintf(stderr,"[GPU-D3D12] external scene work rejected: %s\n",e.what());result=E_FAIL;}
        catch(...){std::fprintf(stderr,"[GPU-D3D12] external scene work rejected by callback exception\n");result=E_FAIL;}
        if(SUCCEEDED(result)){access_trace("close begin");result=commands->Close();access_trace("close end",result);}
        if(SUCCEEDED(result)) {ID3D12CommandList *lists[]={commands.Get()};access_trace("execute begin");scene->queue->ExecuteCommandLists(1,lists);access_trace("execute end");}
        // A rejected callback's command list is discarded; its recorded
        // transitions were never executed, so checked-out resources stay COMMON.
    }
    const UINT64 value=++scene->serial;
    access_trace("signal begin");
    HRESULT signal=scene->queue->Signal(scene->fence.Get(),value);
    access_trace("signal end",signal);
    HRESULT returned=S_OK;
    for(UINT i=0;i<acquired;++i) {
        ID3D12Fence *fence=scene->fence.Get();UINT64 completion=value;
        access_trace(i?"return resource 1+ begin":"return resource 0 begin");
        const HRESULT r=SUCCEEDED(signal)?bridge->ReturnUnderlyingResource(resources[i],1,&completion,&fence):signal;
        access_trace(i?"return resource 1+ end":"return resource 0 end",r);
        if(FAILED(r)&&SUCCEEDED(returned))returned=r;
    }
    if(FAILED(signal))return signal;
    access_trace("event begin");
    HRESULT completion=scene->fence->SetEventOnCompletion(value,scene->event);
    access_trace("event end",completion);
    if(FAILED(completion))return completion;
    access_trace("wait begin");
    const DWORD waited=WaitForSingleObject(scene->event,5000);
    access_trace("wait end",HRESULT(waited));
    if(waited!=WAIT_OBJECT_0)return HRESULT_FROM_WIN32(waited==WAIT_FAILED?GetLastError():WAIT_TIMEOUT);
    access_trace("device status begin");completion=scene->device->GetDeviceRemovedReason();access_trace("device status end",completion);
    if(FAILED(completion))return completion;
    if(FAILED(returned))return returned;
    return result;
}

extern "C" void bf_scene_d3d12_shutdown(void) {delete scene;scene=nullptr;}
