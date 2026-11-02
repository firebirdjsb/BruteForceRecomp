#include "ui_draw.h"
#include <algorithm>
#include <cctype>
namespace bf::ui {
void Canvas::box(int x,int y,int w,int h,uint32_t color) {
    const int x0=std::max(0,x),y0=std::max(0,y);
    const int x1=std::min(int(width),x+w),y1=std::min(int(height),y+h);
    if(x1<=x0||y1<=y0)return;
    for(int row=y0;row<y1;++row)std::fill(pixels+size_t(row)*width+x0,pixels+size_t(row)*width+x1,color);
}
void Canvas::text(int x,int y,std::string_view value,uint32_t color,int scale) {
    // Authored 3x5 bitmap capitals; no game font or external asset is bundled.
    static constexpr const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:/+[]<>%?";
    static constexpr const char *glyphs[]={
        "010101111101101","110101110101110","011100100100011","110101101101110",
        "111100110100111","111100110100100","011100101101011","101101111101101",
        "111010010010111","001001001101010","101101110101101","100100100100111",
        "101111111101101","101111111111101","010101101101010","110101110100100",
        "010101101111011","110101110101101","011100010001110","111010010010010",
        "101101101101111","101101101101010","101101111111101","101101010101101",
        "101101010010010","111001010100111",
        "111101101101111","010110010010111","110001010100111","110001010001110",
        "101101111001001","111100110001110","011100111101111","111001010010010",
        "111101111101111","111101111001110",
        "000000111000000","000000000000010","000010000010000","001001010100100",
        "000010111010000","110100100100110","011001001001011","001010100010001",
        "100010001010100","101001010100101","110001010000010"};
    for(unsigned char ch:value) {
        ch=static_cast<unsigned char>(std::toupper(ch));
        if(ch=='\n'){y+=7*scale;continue;}
        const char *bits=nullptr;
        for(size_t i=0;alphabet[i];++i)if(ch==alphabet[i]){bits=glyphs[i];break;}
        for(int row=0;row<5;++row)for(int col=0;col<3;++col)
            if(bits&&bits[row*3+col]=='1')box(x+col*scale,y+row*scale,scale,scale,color);
        x+=4*scale;
    }
}
}
