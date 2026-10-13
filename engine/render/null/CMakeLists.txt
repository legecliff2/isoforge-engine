# render_null: headless backend for tests and servers (§2.1). Depends on render_api only.
engine_add_module(render_null
    SOURCES     src/null_device.cpp
    PUBLIC_DEPS render_api
    TESTS       tests/null_device_tests.cpp)
