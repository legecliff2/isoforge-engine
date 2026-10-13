#include <doctest/doctest.h>

#include "platform/clock.hpp"
#include "platform/events.hpp"
#include "platform/platform.hpp"

using namespace eng::platform;

TEST_CASE("event pump delivers a synthetic quit (no video needed)") {
    REQUIRE(init({.video = false}));
    EventPump pump;
    request_quit();
    pump.pump();
    Event ev;
    bool saw_quit = false;
    while (pump.poll(ev)) saw_quit |= ev.type == EventType::Quit;
    CHECK(saw_quit);
    CHECK(pump.input().quit_requested);
    shutdown();
}

TEST_CASE("clock is monotonic") {
    REQUIRE(init({.video = false}));
    Clock c;
    uint64_t a = now_ns();
    uint64_t b = now_ns();
    CHECK(b >= a);
    (void)c.tick();
    shutdown();
}
