#include "platform/window.hpp"

#include <SDL3/SDL.h>

namespace eng::platform {

static SDL_Window* sdl(const render::NativeWindow& n) { return static_cast<SDL_Window*>(n.sdl_window); }

std::unique_ptr<Window> Window::create(const WindowDesc& d) {
    SDL_WindowFlags flags = 0;
    if (d.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (d.high_dpi) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* w = SDL_CreateWindow(d.title.c_str(), int(d.width), int(d.height), flags);
    if (!w) {
        SDL_Log("Window::create failed: %s", SDL_GetError());
        return nullptr;
    }
    std::unique_ptr<Window> win(new Window());
    win->native_.sdl_window = w;
    return win;
}

Window::~Window() {
    if (native_.sdl_window) SDL_DestroyWindow(sdl(native_));
}

Vec2i Window::size() const {
    int w = 0, h = 0;
    SDL_GetWindowSize(sdl(native_), &w, &h);
    return {w, h};
}

Vec2i Window::size_in_pixels() const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(sdl(native_), &w, &h);
    return {w, h};
}

uint32_t Window::id() const { return SDL_GetWindowID(sdl(native_)); }

void Window::set_title(const std::string& t) { SDL_SetWindowTitle(sdl(native_), t.c_str()); }

} // namespace eng::platform
