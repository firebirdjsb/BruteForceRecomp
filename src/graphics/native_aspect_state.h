#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Request alone never changes rendering: both authenticated scene and presenter
 * support are required. The selected completed frame has independent metadata. */
void bf_aspect_request(int enabled);
int bf_aspect_requested(void);
void bf_aspect_scene_supported(int supported);
void bf_aspect_presenter_supported(int supported);
int bf_aspect_available(void);
int bf_aspect_ready(void);
void bf_aspect_present_source(int wide);
int bf_aspect_frame_wide(void);
/* Register a backend-owned reader. Query only on the scene producer thread;
 * the callback's surface ownership/metadata is not made atomic by this API. */
void bf_aspect_surface_reader(int (*reader)(uint32_t));
int bf_aspect_surface_wide(uint32_t address);
/* Optional receivers keep runtime-only hosts independent of the GPU backend. */
void bf_aspect_backend_commands(void (*scope)(int),void (*anchor)(unsigned));
void bf_aspect_command_scope(int begin_scope);
void bf_aspect_command_anchor(unsigned anchor);
#ifdef __cplusplus
}
#endif
