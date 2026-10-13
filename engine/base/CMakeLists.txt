# base: std-only foundation types (ARCHITECTURE.md §2.1).
engine_add_module(base
    SOURCES src/canonical_hash.cpp src/base.cpp
    TESTS   tests/base_tests.cpp)
target_link_libraries(eng_base PRIVATE eng_thirdparty_xxhash)
