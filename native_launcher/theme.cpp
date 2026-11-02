#include "theme.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>

namespace bf {
bool Theme::load(SDL_Renderer *renderer, const char *assets) {
    auto &io = ImGui::GetIO();
    const auto folder = std::filesystem::u8path(assets) / "fonts";
    const auto regular = (folder / "Rajdhani-Medium.ttf").string();
    const auto bold = (folder / "Rajdhani-Bold.ttf").string();
    if (!std::filesystem::exists(regular) || !std::filesystem::exists(bold)) return false;
    body = io.Fonts->AddFontFromFileTTF(regular.c_str(), 21);
    small = io.Fonts->AddFontFromFileTTF(regular.c_str(), 16);
    heading = io.Fonts->AddFontFromFileTTF(bold.c_str(), 30);
    title = io.Fonts->AddFontFromFileTTF(bold.c_str(), 104);
    if (!body || !small || !heading || !title) return false;
    if(auto *image=SDL_LoadBMP((std::filesystem::u8path(assets)/"title.bmp").string().c_str())){
        title_art=SDL_CreateTextureFromSurface(renderer,image);SDL_FreeSurface(image);
    }
    io.FontDefault = body;
    auto &style = ImGui::GetStyle();
    style.WindowPadding = {24, 20}; style.FramePadding = {12, 9}; style.ItemSpacing = {10, 8};
    style.FrameRounding = 2; style.FrameBorderSize = 1; style.ChildRounding = 0; style.WindowRounding = 0;
    style.Colors[ImGuiCol_Text] = {0.84f,0.9f,0.94f,1};
    style.Colors[ImGuiCol_TextDisabled] = muted;
    style.Colors[ImGuiCol_WindowBg] = {0.035f,0.055f,0.08f,1};
    style.Colors[ImGuiCol_ChildBg] = {0,0,0,0};
    style.Colors[ImGuiCol_PopupBg] = {0.06f,0.09f,0.13f,1};
    style.Colors[ImGuiCol_Border] = {0.20f,0.29f,0.36f,1};
    style.Colors[ImGuiCol_FrameBg] = {0.04f,0.07f,0.10f,1};
    style.Colors[ImGuiCol_Button] = {0.12f,0.19f,0.25f,1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.20f,0.31f,0.40f,1};
    style.Colors[ImGuiCol_ButtonActive] = {0.26f,0.39f,0.49f,1};
    style.Colors[ImGuiCol_Header] = {0.13f,0.24f,0.31f,1};
    style.Colors[ImGuiCol_HeaderHovered] = {0.20f,0.34f,0.43f,1};
    style.Colors[ImGuiCol_CheckMark] = gold;
    style.Colors[ImGuiCol_PlotHistogram] = gold;
    style.Colors[ImGuiCol_NavCursor] = gold;
    std::array<uint32_t,256*256> pixels{};
    uint32_t random = 0x42524643;
    for (unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) {
        random ^= random<<13; random ^= random>>17; random ^= random<<5;
        const uint8_t noise = uint8_t(random & 31);
        const uint8_t alpha = uint8_t(y%4 == 0 ? 18 : 2 + noise/3);
        pixels[y*256+x] = uint32_t(alpha)<<24 | 0x00BAD9E8u;
    }
    grain = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 256, 256);
    if (!grain || SDL_UpdateTexture(grain, nullptr, pixels.data(), 256*4) != 0) return false;
    SDL_SetTextureBlendMode(grain, SDL_BLENDMODE_BLEND);
    return true;
}
Theme::~Theme() { if(grain) SDL_DestroyTexture(grain);if(title_art)SDL_DestroyTexture(title_art); }
void Theme::panel(ImDrawList *draw, ImVec2 a, ImVec2 b, bool hero) const {
    draw->AddRectFilledMultiColor(a,b,IM_COL32(20,31,42,255),IM_COL32(15,23,32,255),IM_COL32(9,17,26,255),IM_COL32(16,26,37,255));
    // SDL_RenderGeometryRaw validates every supplied vertex UV, including
    // vertices outside a command's index range. UVs above 1 reject the whole
    // parent draw list, making its artwork and text disappear or flicker.
    if(grain) draw->AddImage((ImTextureID)(intptr_t)grain,a,b,{0,0},{1,1},IM_COL32_WHITE);
    draw->AddRect(a,b,IM_COL32(55,77,93,255));
    draw->AddLine(a,{b.x,a.y},hero ? IM_COL32(79,112,137,255):IM_COL32(109,128,142,255),2);
}
void label(const char *text, ImFont *font) { ImGui::PushFont(font); ImGui::TextColored(muted,"%s",text); ImGui::PopFont(); }
static void title_line(ImDrawList *draw, ImFont *font, ImVec2 p, const char *text) {
    draw->AddText(font,104,{p.x+1,p.y+3},IM_COL32(1,8,15,255),text);
    const auto start = draw->VtxBuffer.Size;
    draw->AddText(font,104,p,IM_COL32_WHITE,text);
    for(int i=start;i<draw->VtxBuffer.Size;++i) {
        auto &vertex=draw->VtxBuffer[i];
        const float shade=std::clamp((vertex.pos.y-p.y)/104,0.f,1.f);
        const uint8_t r=uint8_t(235-shade*90),g=uint8_t(247-shade*63),b=uint8_t(255-shade*42);
        vertex.col=(vertex.col&0xFF000000u)|IM_COL32(r,g,b,0);
    }
}
void Theme::hero(ImVec2 a, ImVec2 size) const {
    auto *draw=ImGui::GetWindowDrawList(); const ImVec2 b{a.x+size.x,a.y+size.y};
    panel(draw,a,b,true);
    draw->PushClipRect(a,b,true);
    const ImVec2 p{a.x+30,a.y+38};
    draw->AddText(small,16,p,IM_COL32(137,171,193,255),"FOUR OPERATIVES. ONE FORCE.");
    title_line(draw,title,{p.x-3,p.y+35},"BRUTE"); title_line(draw,title,{p.x-3,p.y+120},"FORCE");
    draw->AddText(body,21,{p.x,p.y+237},IM_COL32(184,205,220,255),"The squad is back.");
    draw->AddText(body,21,{p.x,p.y+263},IM_COL32(184,205,220,255),"Your disc. A new deployment.");
    const ImVec2 center{a.x+size.x*.5f,a.y+size.y*.72f};
    const float radius=std::min(112.f,size.x*.25f);
    draw->AddCircle(center,radius,IM_COL32(88,127,154,48),80);
    draw->AddCircle(center,radius*.8f,IM_COL32(88,127,154,65),80);
    draw->AddLine({center.x-radius,center.y},{center.x+radius,center.y},IM_COL32(88,127,154,35));
    draw->AddLine({center.x,center.y-radius},{center.x,center.y+radius},IM_COL32(88,127,154,35));
    const float w=radius*.47f;
    if(title_art){
        draw->AddImage((ImTextureID)(intptr_t)title_art,{center.x-radius+12,center.y-radius+12},{center.x+radius-12,center.y+radius-12});
    }else{
    ImVec2 shield[]={{center.x,center.y-w*.92f},{center.x+w,center.y-w*.68f},{center.x+w,center.y+w*.15f},{center.x,center.y+w},{center.x-w,center.y+w*.15f},{center.x-w,center.y-w*.68f}};
    draw->AddPolyline(shield,6,IM_COL32(134,174,204,220),5.0f,ImDrawFlags_Closed);
    ImVec2 star[10];
    for(unsigned i=0;i<10;++i) { const float angle=-1.5707963f+float(i)*.6283185f,r=i%2?w*.25f:w*.57f; star[i]={center.x+std::cos(angle)*r,center.y+std::sin(angle)*r}; }
    draw->AddPolyline(star,10,IM_COL32(144,181,209,230),3.0f,ImDrawFlags_Closed);
    }
    draw->AddText(small,16,{center.x-81,center.y+radius+10},IM_COL32(117,152,178,255),"SQUAD SIGNAL // ACQUIRED");
    const float row=b.y-49,cell=size.x/4;
    draw->AddRectFilled({a.x,row},b,IM_COL32(10,18,27,220));
    const char *names[]={"01  TEX","02  BRUTUS","03  HAWK","04  FLINT"};
    for(unsigned i=0;i<4;++i) {
        draw->AddLine({a.x+cell*i,row},{a.x+cell*i,b.y},IM_COL32(54,75,90,150));
        draw->AddText(small,16,{a.x+cell*i+13,row+15},IM_COL32(164,189,208,255),names[i]);
    }
    draw->PopClipRect();
}
}
