#include "host_audio.h"
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
static bool audio_timer_resolution;
#endif
extern "C" {
#include "apu.h"
#include "xbox_memory_layout.h"
extern MCPXAPUState *g_apu_state;
}
bool start_host_audio() {
    if(g_apu_state) return true;
#ifdef _WIN32
    // APU condition-variable waits request 1 ms. A coarse Windows timer
    // tick can oversleep beyond the device's queued audio during loading.
    // Pair the multimedia timer request with stop_host_audio, including a
    // failed APU start. See Microsoft's timeBeginPeriod/timeEndPeriod contract.
    audio_timer_resolution=timeBeginPeriod(1)==TIMERR_NOERROR;
    std::fprintf(stderr,"[AUDIO] 1-ms timer resolution: %s\n",audio_timer_resolution?"active":"unavailable");
#endif
    // The APU uses the runtime's actual RAM/contiguous-memory resolver.
    // No synthetic tone or successful guest call replaces game audio.
    g_apu_state=mcpx_apu_init_standalone(static_cast<uint8_t *>(xbox_GetMemoryBase()));
    if(!g_apu_state){std::fprintf(stderr,"[AUDIO] APU initialization failed\n");stop_host_audio();return false;}
    std::fprintf(stderr,"[AUDIO] Native APU and host output initialized\n");
    return true;
}
void stop_host_audio() {
    auto *state=g_apu_state;
    g_apu_state=nullptr;
    if(state)mcpx_apu_shutdown(state);
#ifdef _WIN32
    if(audio_timer_resolution){timeEndPeriod(1);audio_timer_resolution=false;}
#endif
}
