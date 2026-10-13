#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace eng::platform {

enum class Key : uint16_t {
    Unknown = 0,
    Escape, Space, Enter, Tab, Backspace,
    Left, Right, Up, Down,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LShift, RShift, LCtrl, RCtrl, LAlt, RAlt,
    Count
};

enum class MouseButton : uint8_t { Left = 1, Middle = 2, Right = 3, X1 = 4, X2 = 5 };

enum class EventType : uint8_t {
    Quit, WindowResized, WindowMinimized, WindowRestored, WindowFocusGained, WindowFocusLost,
    KeyDown, KeyUp, MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel, TextInput, Other
};

struct Event {
    EventType type = EventType::Other;
    uint64_t  timestamp_ns = 0;
    uint32_t  window_id = 0;
    struct Resize { int32_t w = 0, h = 0; };                 // pixels
    struct KeyEv  { Key key = Key::Unknown; bool repeat = false; uint16_t mods = 0; };
    struct Motion { float x = 0, y = 0, dx = 0, dy = 0; };
    struct Button { MouseButton button = MouseButton::Left; uint8_t clicks = 0; float x = 0, y = 0; };
    struct Wheel  { float dx = 0, dy = 0; };
    // Only the member matching `type` is meaningful.
    Resize resize;
    KeyEv  key;
    Motion motion;
    Button button;
    Wheel  wheel;
};

// Current-state snapshot maintained by EventPump (input polling convenience).
struct InputState {
    std::array<bool, std::size_t(Key::Count)> down{};
    std::array<bool, std::size_t(Key::Count)> pressed{};   // went down this pump
    float mouse_x = 0.f, mouse_y = 0.f;
    uint8_t mouse_buttons = 0;                         // bit (button-1)
    bool quit_requested = false;

    bool is_down(Key k) const { return down[std::size_t(k)]; }
    bool was_pressed(Key k) const { return pressed[std::size_t(k)]; }
};

class EventPump {
public:
    // Pumps the OS queue; call once per frame before poll(). Clears per-frame `pressed`.
    void pump();
    // Pops the next translated event; false when the queue is empty.
    bool poll(Event& out);
    // pump() + drain everything into the InputState; returns events seen.
    uint32_t pump_all();

    const InputState& input() const { return input_; }

private:
    void apply(const Event&);
    InputState input_;
};

// Pushes a synthetic quit request (tests, scripted shutdown).
void request_quit();

} // namespace eng::platform
