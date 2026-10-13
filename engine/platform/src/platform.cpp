#include "platform/platform.hpp"

#include <SDL3/SDL.h>

namespace eng::platform {

bool init(const InitOptions& o) {
    SDL_InitFlags flags = SDL_INIT_EVENTS;
    if (o.video) flags |= SDL_INIT_VIDEO;
    if (!SDL_Init(flags)) {
        SDL_Log("platform::init failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void shutdown() { SDL_Quit(); }

const char* last_error() { return SDL_GetError(); }

} // namespace eng::platform
