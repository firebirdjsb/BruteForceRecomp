#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu/render_scale_policy.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "Render scale check failed at line %d: %s\n", __LINE__, #e); return 1; } } while (0)

int main(void) {
    uint32_t w = 0, h = 0;
    double scale = 0;
    BfGpuViewport r;
    CHECK(bf_gpu_scaled_surface(640, 480, 1, 1, 1.0, &w, &h));
    CHECK(w == 640 && h == 480);
    CHECK(bf_gpu_scaled_surface(640, 480, 1, 1, 4.0, &w, &h));
    CHECK(w == 2560 && h == 1920);
    CHECK(bf_gpu_scaled_surface(1280, 960, 2, 2, 2.0, &w, &h));
    CHECK(w == 1280 && h == 960);
    CHECK(!bf_gpu_scaled_surface(640, 480, 1, 1, NAN, &w, &h));
    CHECK(!bf_gpu_scaled_surface(640, 480, 1, 1, INFINITY, &w, &h));
    CHECK(!bf_gpu_scaled_surface(16384, 16384, 1, 1, 2.0, &w, &h));
    CHECK(!bf_gpu_scaled_surface(640, 480, 1, 1, 10.1, &w, &h));
    CHECK(!bf_gpu_scaled_surface(640, 480, 1, 1, 0.1, &w, &h));
    CHECK(bf_gpu_native_scale(480, 2160, &scale));
    CHECK(scale == 4.5);
    CHECK(bf_gpu_scaled_surface(640, 480, 1, 1, scale, &w, &h));
    CHECK(w == 2880 && h == 2160);
    CHECK(bf_gpu_fit_viewport(640, 480, 3840, 2160, &r));
    CHECK(r.x == 480 && r.y == 0 && r.width == 2880 && r.height == 2160);
    CHECK(bf_gpu_fit_viewport(640, 480, 2560, 1440, &r));
    CHECK(r.x == 320 && r.y == 0 && r.width == 1920 && r.height == 1440);
    CHECK(bf_gpu_fit_viewport(1920, 1080, 3840, 2160, &r));
    CHECK(r.x == 0 && r.y == 0 && r.width == 3840 && r.height == 2160);
    CHECK(!bf_gpu_fit_viewport(0, 1080, 3840, 2160, &r));
    puts("DX11/DX12-neutral render-scale and aspect policy passed");
    return 0;
}
