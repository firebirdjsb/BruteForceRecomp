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
void Canvas::text5x7(int x,int y,std::string_view value,uint32_t color,int scale) {
    // Authored five-column, seven-row capitals for the Basic Controls guide.
    // Each row uses bits 4..0 from left to right; M and W have distinct
    // diagonals which the existing three-column menu font cannot express.
    static constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:/+[]<>%?";
    static constexpr uint8_t rows[][7]={
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, // A B
        {15,16,16,16,16,16,15}, {30,17,17,17,17,17,30}, // C D
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, // E F
        {15,16,16,23,17,17,15}, {17,17,17,31,17,17,17}, // G H
        {31,4,4,4,4,4,31}, {7,2,2,2,18,18,12},        // I J
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},// K L
        {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17},// M N
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},// O P
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},// Q R
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},       // S T
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},// U V
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},// W X
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},      // Y Z
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},    // 0 1
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},       // 2 3
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},     // 4 5
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},     // 6 7
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},  // 8 9
        {0,0,0,31,0,0,0}, {0,0,0,0,0,6,6},           // - .
        {0,6,6,0,6,6,0}, {1,2,2,4,8,8,16},           // : /
        {0,4,4,31,4,4,0}, {14,8,8,8,8,8,14},         // + [
        {14,2,2,2,2,2,14}, {2,4,8,16,8,4,2},          // ] <
        {8,4,2,1,2,4,8}, {25,26,2,4,8,11,19},         // > %
        {14,17,1,2,4,0,4}                             // ?
    };
    static_assert(sizeof rows/sizeof rows[0]==alphabet.size());
    if(scale<1)return;
    const int origin=x;
    for(unsigned char ch:value) {
        if(ch=='\n'){x=origin;y+=9*scale;continue;}
        ch=static_cast<unsigned char>(std::toupper(ch));
        const auto index=alphabet.find(char(ch));
        if(ch!=' ') {
            const auto &glyph=rows[index==std::string_view::npos?alphabet.size()-1:index];
            for(int row=0;row<7;row++)for(int col=0;col<5;col++)
                if(glyph[row]&(1u<<(4-col)))box(x+col*scale,y+row*scale,scale,scale,color);
        }
        x+=6*scale;
    }
}
}
