# script_host: Luau VM host, sandbox, budgets, API level 1 bindings, pack-load strict
# typecheck (ARCHITECTURE.md §3.3). No SDL. Luau.CodeGen is never linked.
engine_add_module(script_host
    SOURCES     src/script_host.cpp src/typecheck.cpp
    PUBLIC_DEPS ecs rng assets
    TESTS       tests/script_host_tests.cpp)
target_link_libraries(eng_script_host PRIVATE Luau.VM Luau.Compiler Luau.Analysis)
target_compile_definitions(eng_script_host PRIVATE
    ENGINE_SCRIPT_API_DIR="${CMAKE_CURRENT_SOURCE_DIR}/api")
if(TARGET script_host_tests)
    target_compile_definitions(script_host_tests PRIVATE
        ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
        SCRIPT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts")
endif()
