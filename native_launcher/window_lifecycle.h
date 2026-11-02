#pragma once
#include <SDL.h>
namespace bf {
// SDL calls stay on the launcher main thread. Library::running is set only
// after process creation succeeds and clears on normal exit or failure.
inline void sync_game_window(SDL_Window *window,bool game_running,bool &hidden) {
    if(game_running==hidden)return;
    if(game_running)SDL_HideWindow(window);
    else {SDL_ShowWindow(window);SDL_RestoreWindow(window);SDL_RaiseWindow(window);}
    hidden=game_running;
}
}
