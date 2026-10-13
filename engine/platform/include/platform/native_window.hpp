#pragma once
// Definition of the opaque eng::render::NativeWindow handed to render backends (§5.1).
// Only platform and render_* should need to look inside it.

namespace eng::render {
struct NativeWindow {
    void* sdl_window = nullptr;  // SDL_Window*
};
} // namespace eng::render
