/* Persistent DXBC cache smoke: source/key changes and corrupt entries are
 * rejected. No game code or shaders and no actual guest renderer involved. */
#ifdef _WIN32
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu/shader_cache.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr,"Shader cache check failed line %d: %s\n",__LINE__,#e); return 1; } } while (0)
int main(void) {
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compiler) {
        puts("SKIP: D3DCompiler_47 not installed");
        return 77;
    }
    wchar_t temp[MAX_PATH], folder[MAX_PATH], match[MAX_PATH], cacheFile[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temp) > 0);
    CHECK(GetTempFileNameW(temp, L"BFC", 0, folder));
    CHECK(DeleteFileW(folder));
    CHECK(CreateDirectoryW(folder, NULL));
    CHECK(_wputenv_s(L"RECOMP_GPU_PSCACHE", L"1") == 0);
    CHECK(_wputenv_s(L"RECOMP_GPU_PSCACHE_DIR", folder) == 0);
    CHECK(gpu_pscache_init());
    if (!gpu_pscache_enabled()) {
        puts("SKIP: persistent cache unavailable on this runner");
        RemoveDirectoryW(folder);
        return 77;
    }
    const char source[] = "float4 ps_main():SV_Target{return 1;}";
    const char changed[] = "float4 ps_main():SV_Target{return 0;}";
    const uint8_t bytes[32] = {1,2,3,4,5,6,7,8,9,10,11};
    void *out = NULL;
    size_t size = 0;
    CHECK(!gpu_pscache_load(source,sizeof(source)-1,"synthetic",
                            "ps_main","ps_5_0",0,&out,&size));
    CHECK(out == NULL && size == 0);
    gpu_pscache_store(source,sizeof(source)-1,"synthetic",
                      "ps_main","ps_5_0",0,bytes,sizeof bytes);
    CHECK(gpu_pscache_load(source,sizeof(source)-1,"synthetic",
                           "ps_main","ps_5_0",0,&out,&size));
    CHECK(size == sizeof bytes && memcmp(out, bytes, sizeof bytes) == 0);
    free(out);
    CHECK(!gpu_pscache_load(changed,sizeof(changed)-1,"synthetic",
                            "ps_main","ps_5_0",0,&out,&size));
    CHECK(!gpu_pscache_load(source,sizeof(source)-1,"synthetic",
                            "ps_main","vs_5_0",0,&out,&size));
    CHECK(!gpu_pscache_load(source,sizeof(source)-1,"synthetic",
                            "ps_main","ps_5_0",1,&out,&size));
    CHECK(swprintf_s(match, MAX_PATH, L"%ls\\ps_*.bin", folder) > 0);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(match, &found);
    CHECK(search != INVALID_HANDLE_VALUE);
    CHECK(swprintf_s(cacheFile, MAX_PATH, L"%ls\\%ls", folder, found.cFileName) > 0);
    FindClose(search);
    HANDLE handle = CreateFileW(cacheFile, GENERIC_READ|GENERIC_WRITE,
                                FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(handle != INVALID_HANDLE_VALUE);
    LARGE_INTEGER offset;
    offset.QuadPart = 80;
    CHECK(SetFilePointerEx(handle, offset, NULL, FILE_BEGIN));
    uint8_t poison = 0xEE;
    DWORD wrote = 0;
    CHECK(WriteFile(handle, &poison, 1, &wrote, NULL) && wrote == 1);
    CloseHandle(handle);
    CHECK(!gpu_pscache_load(source,sizeof(source)-1,"synthetic",
                            "ps_main","ps_5_0",0,&out,&size));
    CHECK(out == NULL && size == 0);
    CHECK(DeleteFileW(cacheFile));
    CHECK(RemoveDirectoryW(folder));
    puts("Persistent D3DCompiler DXBC cache roundtrip, key isolation and corruption checks passed");
    return 0;
}
#endif
