#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "base/math.hpp"
#include "platform/native_window.hpp"

namespace eng::platform {

struct WindowDesc {
    std::string title = "isoforge";
    uint32_t    width = 1280, height = 720;
    bool        resizable = true;
    bool        high_dpi = true;
};

class Window {
public:
    static std::unique_ptr<Window> create(const WindowDesc&);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    render::NativeWindow& native() { return native_; }
    Vec2i size() const;            // logical points
    Vec2i size_in_pixels() const;  // drawable pixels
    uint32_t id() const;
    void set_title(const std::string&);

private:
    Window() = default;
    render::NativeWindow native_;
};

} // namespace eng::platform
