#include "text_events.h"
#include <chrono>
#include <deque>
#include <mutex>

namespace {
std::mutex lock;
std::deque<BfTextEvent> pending;
uint32_t owner{};
std::chrono::steady_clock::time_point last_seen{};
bool active() {
    // A render heartbeat prevents a closed/destroyed keyboard from retaining
    // text ownership. Confirm/cancel/focus loss also release it immediately.
    return owner && std::chrono::steady_clock::now() - last_seen < std::chrono::milliseconds(500);
}
}
extern "C" int bf_text_active() {
    std::lock_guard guard(lock);
    return active();
}
extern "C" void bf_text_seen(uint32_t keyboard) {
    std::lock_guard guard(lock);
    if (owner != keyboard || !active()) pending.clear();
    owner = keyboard;
    last_seen = std::chrono::steady_clock::now();
}
extern "C" void bf_text_reset() {
    std::lock_guard guard(lock);
    owner = 0;
    pending.clear();
}
extern "C" int bf_text_character(uint32_t codepoint) {
    std::lock_guard guard(lock);
    if (!active()) return 0;
    // Windows supplies Backspace and Return as WM_CHAR. Escape/Delete are
    // handled on keydown. Reject invalid Unicode, controls and lone surrogates.
    if (codepoint != 8 && codepoint != 13 && codepoint != 27 &&
        (codepoint < 32 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))) return 1;
    if (pending.size() < 256) pending.push_back({codepoint,0,0,0});
    return 1;
}
extern "C" int bf_text_click(uint32_t keyboard,uint32_t column,uint32_t row) {
    std::lock_guard guard(lock);
    if(!active()||keyboard!=owner||column>=32||row>=16||pending.size()>=256)return 0;
    pending.push_back({0,column,row,1});return 1;
}
extern "C" int bf_text_key(uint32_t key) {
    if (!bf_text_active()) return 0;
    // Virtual-key numbers: Escape=27, Delete=46. Arrow keys remain available
    // for the original on-screen grid and physical controllers remain enabled.
    if (key == 27) return bf_text_character(27);
    if (key == 46) return bf_text_character(8);
    return key != 37 && key != 38 && key != 39 && key != 40;
}
extern "C" int bf_text_take(uint32_t keyboard, BfTextEvent *event) {
    std::lock_guard guard(lock);
    if (!event || !active() || owner != keyboard || pending.empty()) return 0;
    *event = pending.front();
    pending.pop_front();
    return 1;
}
extern "C" int bf_text_utf8(const char *text) {
    if (!text || !bf_text_active()) return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(text);
    for (unsigned consumed = 0; *p && consumed < 4096;) {
        uint32_t codepoint = *p++;
        ++consumed;
        unsigned extra = codepoint < 0x80 ? 0 : codepoint >= 0xc2 && codepoint < 0xe0 ? 1 :
                         codepoint >= 0xe0 && codepoint < 0xf0 ? 2 : codepoint >= 0xf0 && codepoint <= 0xf4 ? 3 : 4;
        if (extra == 4) return 1;
        const uint32_t minimum = extra == 1 ? 0x80 : extra == 2 ? 0x800 : extra == 3 ? 0x10000 : 0;
        if (extra) codepoint &= (1u << (6 - extra)) - 1;
        for (unsigned i = 0; i < extra; ++i) {
            if ((*p & 0xc0) != 0x80) return 1;
            codepoint = (codepoint << 6) | (*p++ & 0x3f);
            ++consumed;
        }
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return 1;
        bf_text_character(codepoint);
    }
    return 1;
}
