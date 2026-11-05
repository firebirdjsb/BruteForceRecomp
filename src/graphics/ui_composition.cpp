#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ui_composition.h"
#include <wrl/client.h>
#include <memory>
#include <new>
using Microsoft::WRL::ComPtr;

struct BfUiComposition {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
    BfUiCompositionInfo info{};
};
namespace {
BfUiCompositionConsumer consumer{};
void *consumer_user{};
bool requested{};
}
extern "C" {
BfUiBlendMode bf_ui_blend_mode(uint32_t enabled,uint32_t source,uint32_t destination,
    uint32_t equation,uint32_t color_mask) {
    constexpr uint32_t rgb_mask = 0x00010101u; // R byte 2, G byte 1, B byte 0.
    const auto rgb = color_mask & rgb_mask;
    if (!rgb) return BF_UI_BLEND_NO_RGB;
    if (rgb != rgb_mask) return BF_UI_BLEND_UNSUPPORTED;
    if (!enabled) return BF_UI_BLEND_OPAQUE;
    // NV2A's explicit ADD (0x8006) and the executor's zero-initialized ADD.
    if (equation != 0 && equation != 0x8006u) return BF_UI_BLEND_UNSUPPORTED;
    if (source == 1 && destination == 0) return BF_UI_BLEND_OPAQUE;
    if (source == 0x302u && destination == 0x303u) return BF_UI_BLEND_SOURCE_ALPHA;
    return BF_UI_BLEND_UNSUPPORTED;
}
void bf_ui_composition_consumer(BfUiCompositionConsumer callback,void *user) { consumer=callback;consumer_user=user; }
void bf_ui_composition_request(int enabled) { requested=enabled!=0; }
int bf_ui_composition_enabled(void) { return requested || consumer!=nullptr; }
HRESULT bf_ui_composition_begin(BfUiComposition **capture,ID3D11Device *device,ID3D11DeviceContext *context,
    uint64_t generation,uint32_t color_va,uint32_t width,uint32_t height) {
    if (!capture || !device || !context || !generation || !color_va || !width || !height || width>16384 || height>16384) return E_INVALIDARG;
    ComPtr<ID3D11Device> context_device; context->GetDevice(&context_device);
    if (context_device.Get()!=device) return E_INVALIDARG;
    try {
        if (!*capture || (*capture)->device.Get()!=device || (*capture)->info.width!=width || (*capture)->info.height!=height) {
            UINT support{};
            HRESULT result=device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT,&support);
            const UINT required=D3D11_FORMAT_SUPPORT_RENDER_TARGET|D3D11_FORMAT_SUPPORT_BLENDABLE|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
            if (FAILED(result)) return result;
            if ((support&required)!=required) return DXGI_ERROR_UNSUPPORTED;
            auto replacement=std::make_unique<BfUiComposition>();replacement->device=device;
            D3D11_TEXTURE2D_DESC description{};
            description.Width=width;description.Height=height;description.MipLevels=description.ArraySize=1;
            description.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;description.SampleDesc.Count=1;
            description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            result=device->CreateTexture2D(&description,nullptr,&replacement->texture);if(FAILED(result))return result;
            result=device->CreateRenderTargetView(replacement->texture.Get(),nullptr,&replacement->target);if(FAILED(result))return result;
            result=device->CreateShaderResourceView(replacement->texture.Get(),nullptr,&replacement->view);if(FAILED(result))return result;
            delete *capture;*capture=replacement.release();
        }
        (*capture)->info={generation,color_va,width,height,0,BF_UI_CAPTURE_VALID,BF_UI_SOURCE_CHANGE_NONE};
        const float transparent[4]{};context->ClearRenderTargetView((*capture)->target.Get(),transparent);
        return S_OK;
    } catch(const std::bad_alloc &) { return E_OUTOFMEMORY; }
    catch(...) { return E_FAIL; }
}
void bf_ui_composition_destroy(BfUiComposition *capture) { delete capture; }
ID3D11RenderTargetView *bf_ui_composition_target(BfUiComposition *capture,uint64_t generation) {
    return capture && capture->info.generation==generation && capture->info.status==BF_UI_CAPTURE_VALID ? capture->target.Get() : nullptr;
}
void bf_ui_composition_invalidate(BfUiComposition *capture,BfUiCaptureStatus reason) {
    if(capture && capture->info.status==BF_UI_CAPTURE_VALID && reason!=BF_UI_CAPTURE_VALID)capture->info.status=reason;
}
void bf_ui_composition_source_changed(BfUiComposition *capture,BfUiSourceChangeReason reason) {
    if(!capture||capture->info.status!=BF_UI_CAPTURE_VALID)return;
    if(reason>BF_UI_SOURCE_CHANGE_NONE&&reason<=BF_UI_SOURCE_CHANGE_WORLD_SCOPE)
        capture->info.source_change_reason=reason;
    capture->info.status=BF_UI_CAPTURE_SOURCE_CHANGED;
}
void bf_ui_composition_draw(BfUiComposition *capture,uint64_t generation) {
    if(capture && capture->info.generation==generation && capture->info.status==BF_UI_CAPTURE_VALID)++capture->info.draws;
}
int bf_ui_composition_snapshot(BfUiComposition *capture,uint64_t generation,BfUiCompositionSnapshot *out) {
    if(out)*out={};
    if(!out||!capture||capture->info.generation!=generation)return 0;
    *out={capture->info,capture->texture.Get(),capture->view.Get()};return 1;
}
void bf_ui_composition_publish(BfUiComposition *capture,ID3D11DeviceContext *context,uint64_t generation) {
    if(!consumer || !capture || !context || capture->info.generation!=generation)return;
    const BfUiCompositionSnapshot snapshot{capture->info,capture->texture.Get(),capture->view.Get()};
    consumer(context,&snapshot,consumer_user);
}
}
