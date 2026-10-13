#include "platform/events.hpp"

#include <SDL3/SDL.h>

namespace eng::platform {

namespace {

Key translate_key(SDL_Keycode k) {
    if (k >= SDLK_A && k <= SDLK_Z) return Key(uint16_t(Key::A) + (k - SDLK_A));
    if (k >= SDLK_0 && k <= SDLK_9) return Key(uint16_t(Key::Num0) + (k - SDLK_0));
    if (k >= SDLK_F1 && k <= SDLK_F12) return Key(uint16_t(Key::F1) + (k - SDLK_F1));
    switch (k) {
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_SPACE: return Key::Space;
    case SDLK_RETURN: return Key::Enter;
    case SDLK_TAB: return Key::Tab;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_LSHIFT: return Key::LShift;
    case SDLK_RSHIFT: return Key::RShift;
    case SDLK_LCTRL: return Key::LCtrl;
    case SDLK_RCTRL: return Key::RCtrl;
    case SDLK_LALT: return Key::LAlt;
    case SDLK_RALT: return Key::RAlt;
    default: return Key::Unknown;
    }
}

bool translate(const SDL_Event& e, Event& out) {
    out = Event{};
    out.timestamp_ns = e.common.timestamp;
    switch (e.type) {
    case SDL_EVENT_QUIT:
        out.type = EventType::Quit;
        return true;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        out.type = EventType::WindowResized;
        out.window_id = e.window.windowID;
        out.resize = {e.window.data1, e.window.data2};
        return true;
    case SDL_EVENT_WINDOW_MINIMIZED: out.type = EventType::WindowMinimized; out.window_id = e.window.windowID; return true;
    case SDL_EVENT_WINDOW_RESTORED: out.type = EventType::WindowRestored; out.window_id = e.window.windowID; return true;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: out.type = EventType::WindowFocusGained; out.window_id = e.window.windowID; return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST: out.type = EventType::WindowFocusLost; out.window_id = e.window.windowID; return true;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        out.type = EventType::Quit;
        out.window_id = e.window.windowID;
        return true;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        out.type = e.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp;
        out.window_id = e.key.windowID;
        out.key = {translate_key(e.key.key), e.key.repeat, uint16_t(e.key.mod)};
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        out.type = EventType::MouseMove;
        out.window_id = e.motion.windowID;
        out.motion = {e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel};
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        out.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        out.window_id = e.button.windowID;
        out.button = {MouseButton(e.button.button), e.button.clicks, e.button.x, e.button.y};
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        out.type = EventType::MouseWheel;
        out.window_id = e.wheel.windowID;
        out.wheel = {e.wheel.x, e.wheel.y};
        return true;
    default:
        out.type = EventType::Other;
        return true;
    }
}

} // namespace

void EventPump::pump() {
    input_.pressed.fill(false);
    SDL_PumpEvents();
}

bool EventPump::poll(Event& out) {
    SDL_Event e;
    if (!SDL_PollEvent(&e)) return false;
    translate(e, out);
    apply(out);
    return true;
}

uint32_t EventPump::pump_all() {
    pump();
    uint32_t n = 0;
    Event ev;
    while (poll(ev)) ++n;
    return n;
}

void EventPump::apply(const Event& e) {
    switch (e.type) {
    case EventType::Quit: input_.quit_requested = true; break;
    case EventType::KeyDown:
        if (!e.key.repeat) input_.pressed[size_t(e.key.key)] = true;
        input_.down[size_t(e.key.key)] = true;
        break;
    case EventType::KeyUp: input_.down[size_t(e.key.key)] = false; break;
    case EventType::MouseMove: input_.mouse_x = e.motion.x; input_.mouse_y = e.motion.y; break;
    case EventType::MouseButtonDown: input_.mouse_buttons |= uint8_t(1u << (uint8_t(e.button.button) - 1)); break;
    case EventType::MouseButtonUp: input_.mouse_buttons &= uint8_t(~(1u << (uint8_t(e.button.button) - 1))); break;
    default: break;
    }
}

void request_quit() {
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}

} // namespace eng::platform
