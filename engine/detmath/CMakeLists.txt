# rng + detmath: std-only, deterministic (ARCHITECTURE.md §2.1, §4).
engine_add_module(rng
    SOURCES src/rng.cpp
    TESTS   tests/rng_tests.cpp
    DETERMINISTIC)

engine_add_module(detmath
    SOURCES src/fx.cpp src/noise.cpp src/stable_hash.cpp
    TESTS   tests/fx_tests.cpp tests/noise_tests.cpp
    TEST_DEPS rng   # seeded workload for the Fx golden hash
    DETERMINISTIC)
# xxHash is a header-only third-party dependency (StableHash, §4.5); not an engine module.
target_link_libraries(eng_detmath PRIVATE eng_thirdparty_xxhash)
