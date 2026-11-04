#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace bf::ui {
std::u16string desktop_prompt_text(std::u16string_view text,bool desktop);
}
extern "C" uint32_t bf_prompt_text_frame(uint32_t frame);
