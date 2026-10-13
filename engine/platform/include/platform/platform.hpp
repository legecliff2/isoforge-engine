#pragma once
// Public platform API. No SDL types leak through these headers (§2.2 rule 3).

namespace eng::platform {

struct InitOptions {
    bool video = true;   // false: events/timers only (tests, headless tools)
};

// Initialises SDL subsystems. Returns false and logs on failure.
bool init(const InitOptions& = {});
void shutdown();
const char* last_error();

} // namespace eng::platform
