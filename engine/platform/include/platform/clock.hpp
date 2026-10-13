#pragma once
#include <cstdint>

namespace eng::platform {

// Monotonic time since platform init.
uint64_t now_ns();

// Real-time frame clock: tick() returns ns since the previous tick, clamped.
class Clock {
public:
    explicit Clock(uint64_t max_frame_ns = 250'000'000);
    uint64_t tick();
    uint64_t total_ns() const { return total_; }
private:
    uint64_t last_, total_ = 0, max_;
};

// Fixed-timestep accumulator ("fix your timestep"). Pure integer logic, no SDL,
// so the number of simulation steps for a given elapsed-time sequence is exact.
class FixedTimestep {
public:
    explicit FixedTimestep(uint64_t step_ns = 1'000'000'000ull / 60, uint32_t max_steps_per_frame = 8);

    // Adds frame time; returns how many fixed steps to run now. Excess beyond
    // max_steps_per_frame is dropped (spiral-of-death guard).
    uint32_t advance(uint64_t elapsed_ns);

    uint64_t step_ns() const { return step_; }
    uint64_t steps_total() const { return steps_; }
    uint64_t dropped_ns() const { return dropped_; }
    // Interpolation factor [0,1) for rendering between the last two sim states.
    float alpha() const { return float(double(acc_) / double(step_)); }

private:
    uint64_t step_, acc_ = 0, steps_ = 0, dropped_ = 0;
    uint32_t max_steps_;
};

} // namespace eng::platform
