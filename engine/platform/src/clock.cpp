#include "platform/clock.hpp"

#include <SDL3/SDL.h>

namespace eng::platform {

uint64_t now_ns() { return SDL_GetTicksNS(); }

Clock::Clock(uint64_t max_frame_ns) : last_(now_ns()), max_(max_frame_ns) {}

uint64_t Clock::tick() {
    const uint64_t t = now_ns();
    uint64_t dt = t - last_;
    last_ = t;
    if (dt > max_) dt = max_;
    total_ += dt;
    return dt;
}

FixedTimestep::FixedTimestep(uint64_t step_ns, uint32_t max_steps)
    : step_(step_ns ? step_ns : 1), max_steps_(max_steps ? max_steps : 1) {}

uint32_t FixedTimestep::advance(uint64_t elapsed_ns) {
    acc_ += elapsed_ns;
    uint64_t n = acc_ / step_;
    acc_ -= n * step_;
    if (n > max_steps_) {
        dropped_ += (n - max_steps_) * step_;
        n = max_steps_;
    }
    steps_ += n;
    return uint32_t(n);
}

} // namespace eng::platform
