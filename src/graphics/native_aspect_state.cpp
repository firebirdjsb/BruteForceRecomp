#include "native_aspect_state.h"
#include <atomic>
namespace {
std::atomic<bool> requested{},scene{},presenter{},frame_wide{};
std::atomic<int (*)(uint32_t)> surface_reader{};
std::atomic<void (*)(int)> scope_receiver{};
std::atomic<void (*)(unsigned)> anchor_receiver{};
}
extern "C" void bf_aspect_request(int value){requested.store(value!=0);}
extern "C" int bf_aspect_requested(void){return requested.load();}
extern "C" void bf_aspect_scene_supported(int value){scene.store(value!=0);}
extern "C" void bf_aspect_presenter_supported(int value){presenter.store(value!=0);}
extern "C" int bf_aspect_available(void){return scene.load()&&presenter.load();}
extern "C" int bf_aspect_ready(void){return requested.load()&&bf_aspect_available();}
extern "C" void bf_aspect_present_source(int value){frame_wide.store(value!=0);}
extern "C" int bf_aspect_frame_wide(void){return frame_wide.load();}
extern "C" void bf_aspect_surface_reader(int (*reader)(uint32_t)){surface_reader.store(reader);}
extern "C" int bf_aspect_surface_wide(uint32_t address){
    const auto reader=surface_reader.load();return reader&&reader(address)!=0;
}
extern "C" void bf_aspect_backend_commands(void (*scope)(int),void (*anchor)(unsigned)){
    scope_receiver.store(scope);anchor_receiver.store(anchor);
}
extern "C" void bf_aspect_command_scope(int begin_scope){const auto callback=scope_receiver.load();if(callback)callback(begin_scope);}
extern "C" void bf_aspect_command_anchor(unsigned anchor){const auto callback=anchor_receiver.load();if(callback)callback(anchor);}
