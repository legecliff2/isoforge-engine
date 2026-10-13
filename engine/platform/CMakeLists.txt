# platform: SDL3 window, event pump, clock (§2.1). base + SDL3 only.
engine_add_module(platform
    SOURCES     src/platform.cpp src/window.cpp src/events.cpp src/clock.cpp
    PUBLIC_DEPS base
    TESTS       tests/clock_tests.cpp tests/events_tests.cpp)
target_link_libraries(eng_platform PRIVATE SDL3::SDL3-static)
