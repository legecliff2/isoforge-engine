# iso2d: sprite batcher, iso projection, depth sort (§2.1, §5.1). render_api + base only.
engine_add_module(iso2d
    SOURCES     src/sprite_batch.cpp src/iso.cpp src/light_map.cpp
    PUBLIC_DEPS render_api base
    TESTS       tests/iso_tests.cpp tests/sprite_batch_tests.cpp tests/light_map_tests.cpp
    TEST_DEPS   render_null)
