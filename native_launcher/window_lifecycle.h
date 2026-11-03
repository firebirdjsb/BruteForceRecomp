#pragma once
#include <SDL.h>
namespace bf {
// SDL calls stay on the launcher main thread. The session includes launch
// validation and process startup, and ends on normal exit or any failure.
inline void sync_game_window(SDL_Window *window,bool game_running,bool &hidden) {
    if(game_running==hidden)return;
    if(game_running)SDL_HideWindow(window);
    else {SDL_ShowWindow(window);SDL_RestoreWindow(window);SDL_RaiseWindow(window);}
    hidden=game_running;
}
}
