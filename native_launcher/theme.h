#pragma once
#include <SDL.h>
#include <imgui.h>
namespace bf {
struct Theme {
    ImFont *body{}, *small{}, *heading{}, *title{};
    SDL_Texture *grain{};
    SDL_Texture *title_art{};
    bool load(SDL_Renderer *renderer, const char *asset_directory);
    ~Theme();
    void panel(ImDrawList *draw, ImVec2 from, ImVec2 to, bool hero = false) const;
    void hero(ImVec2 from, ImVec2 size) const;
};
constexpr ImVec4 gold{0.78f, 0.70f, 0.45f, 1.0f};
constexpr ImVec4 muted{0.49f, 0.61f, 0.69f, 1.0f};
void label(const char *text, ImFont *font);
}
