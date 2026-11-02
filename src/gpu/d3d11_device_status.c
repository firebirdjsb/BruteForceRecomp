#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
void gpu_d3d11_check_device(ID3D11Device *device, HRESULT result, const char *operation)
{
    HRESULT reason = device ? ID3D11Device_GetDeviceRemovedReason(device) : S_OK;
    if (FAILED(reason) || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET
        || result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR) {
        fprintf(stderr, "[GPU-D3D11] device lost during %s: result 0x%08lX,"
                        " removed reason 0x%08lX; recovery is not implemented,"
                        " stopping rather than completing outstanding GPU work\n",
                operation, (unsigned long)result, (unsigned long)reason);
        fflush(stderr);
        abort();
    }
}

HRESULT gpu_d3d11_get_data(ID3D11Device *device, ID3D11DeviceContext *context,
                           ID3D11Asynchronous *query, void *data, UINT size, UINT flags,
                           const char *operation)
{
    HRESULT hr = ID3D11DeviceContext_GetData(context, query, data, size, flags);
    gpu_d3d11_check_device(device, hr, operation);
    return hr;
}
