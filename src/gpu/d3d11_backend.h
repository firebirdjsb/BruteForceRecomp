/* Direct3D 11 render back end (RECOMP_GPU=d3d11); see d3d11_backend.c. */
#ifndef BRUTEFORCE_D3D11_BACKEND_H
#define BRUTEFORCE_D3D11_BACKEND_H
#include <stdint.h>

int bruteforce_d3d11_install(void);
/* Executor-thread-only, ordered by private NO_OPERATION camera markers. */
void bruteforce_d3d11_wide_scope(int begin_scope);
void bruteforce_d3d11_ui_anchor(unsigned anchor);
/* Producer-thread frame activation; default diagnostics remain unjittered.
 * A failed jittered source must be dropped by the completed-frame publisher. */
void bruteforce_d3d11_temporal_jitter(int enabled);
struct BfMotionSnapshot;
int bruteforce_d3d11_motion_snapshot(uint32_t color_va,uint64_t frame,struct BfMotionSnapshot *);

#endif
