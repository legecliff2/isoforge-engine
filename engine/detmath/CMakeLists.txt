# rng + detmath: std-only, deterministic (ARCHITECTURE.md §2.1, §4).
engine_add_module(rng
    SOURCES src/rng.cpp
    TESTS   tests/rng_tests.cpp
    DETERMINISTIC)

engine_add_module(detmath
    SOURCES src/fx.cpp
    TESTS   tests/fx_tests.cpp
    TEST_DEPS rng   # seeded workload for the Fx golden hash
    DETERMINISTIC)
