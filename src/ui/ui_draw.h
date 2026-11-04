#pragma once
#include <stdint.h>
#include <string_view>
namespace bf::ui {
struct Canvas {
    uint32_t *pixels; unsigned width,height;
    float xscale=1.0f;
    float xoffset=0.0f;
    void box(int x,int y,int w,int h,uint32_t color);
    void text(int x,int y,std::string_view value,uint32_t color,int scale=1);
    void text5x7(int x,int y,std::string_view value,uint32_t color,int scale=1);
};
}
