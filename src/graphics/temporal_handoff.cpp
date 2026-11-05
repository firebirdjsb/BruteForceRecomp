#include "temporal_handoff.h"
#include <new>

namespace {
using Ticket=std::shared_ptr<const bf::graphics::GameTemporalFrame>;
thread_local uint64_t producer_sequence{};
thread_local uint32_t producer_color{};
thread_local bool producer_publishable=true;
thread_local Ticket selected;
}
extern "C" void bf_temporal_handoff_source(uint64_t sequence,uint32_t color_va) {
    if(producer_sequence!=sequence||producer_color!=color_va)producer_publishable=true;
    producer_sequence=sequence;producer_color=color_va;
}
extern "C" void bf_temporal_handoff_reject(uint64_t sequence,uint32_t color_va) {
    if(sequence&&sequence==producer_sequence&&color_va==producer_color)producer_publishable=false;
}
extern "C" int bf_temporal_handoff_publishable(uint32_t color_va) {
    return color_va!=producer_color||producer_publishable;
}
extern "C" void *bf_temporal_ticket_acquire(uint32_t color_va) try {
    if(!producer_sequence||!color_va||color_va!=producer_color)return nullptr;
    auto frame=bf::graphics::game_temporal_take_frame(producer_sequence,color_va);
    return frame?new(std::nothrow) Ticket(std::move(frame)):nullptr;
} catch(...) {return nullptr;}
extern "C" void bf_temporal_ticket_release(void *ticket) {delete static_cast<Ticket *>(ticket);}
extern "C" void bf_temporal_ticket_select(const void *ticket) {
    selected=ticket?*static_cast<const Ticket *>(ticket):Ticket{};
}
extern "C" void bf_temporal_ticket_clear(void) {selected.reset();}
namespace bf::graphics {
std::shared_ptr<const GameTemporalFrame> selected_temporal_frame() {return selected;}
}
