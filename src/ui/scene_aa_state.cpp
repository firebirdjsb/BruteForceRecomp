#include "scene_boundary.h"
#include <atomic>
namespace {std::atomic<bool> enabled{true};}
extern "C" void bf_scene_aa_set_enabled(int value){enabled.store(value!=0,std::memory_order_relaxed);}
extern "C" int bf_scene_aa_enabled(){return enabled.load(std::memory_order_relaxed);}
