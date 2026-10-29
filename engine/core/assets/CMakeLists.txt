# assets: versioned schema loader + migration registry (ARCHITECTURE.md §3.2).
engine_add_module(assets
    SOURCES     src/schema_registry.cpp src/content_migrations.cpp src/pack_set.cpp src/clip_registry.cpp
    PUBLIC_DEPS base
    TESTS       tests/schema_tests.cpp tests/content_migration_tests.cpp tests/pack_set_tests.cpp tests/clip_registry_tests.cpp)
target_link_libraries(eng_assets PUBLIC nlohmann_json::nlohmann_json)
if(TARGET assets_tests)
    target_compile_definitions(assets_tests PRIVATE ENGINE_CORE_FIXTURES="${ENGINE_CORE_FIXTURES}"
        ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
        TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_scratch")
    target_include_directories(assets_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/core/script_host/tests")
endif()
