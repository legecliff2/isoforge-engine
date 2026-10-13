# script_host: Luau VM host, sandbox, budgets, API level 1 bindings, pack-load strict
# typecheck (ARCHITECTURE.md §3.3). No SDL. Luau.CodeGen is never linked.

# Frozen API definitions are embedded byte-exact at build time (api_freeze guards the source).
set(_api_src ${CMAKE_CURRENT_SOURCE_DIR}/api/engine.level1.d.luau)
set(_api_cpp ${CMAKE_CURRENT_BINARY_DIR}/generated/engine_level1_d_luau.cpp)
add_custom_command(
    OUTPUT  ${_api_cpp}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${_api_src} -DOUTPUT=${_api_cpp}
            -DSYMBOL=kEngineLevel1Defs -DNAMESPACE=eng::script::embedded
            -P ${PROJECT_SOURCE_DIR}/tools/embed/embed_file.cmake
    DEPENDS ${_api_src} ${PROJECT_SOURCE_DIR}/tools/embed/embed_file.cmake
    COMMENT "Embedding engine.level1.d.luau"
    VERBATIM)

engine_add_module(script_host
    SOURCES     src/script_host.cpp src/typecheck.cpp ${_api_cpp}
    PUBLIC_DEPS ecs rng assets
    TESTS       tests/script_host_tests.cpp)
target_link_libraries(eng_script_host PRIVATE Luau.VM Luau.Compiler Luau.Analysis)
if(TARGET script_host_tests)
    target_compile_definitions(script_host_tests PRIVATE
        ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
        SCRIPT_API_DIR="${CMAKE_CURRENT_SOURCE_DIR}/api"
        SCRIPT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts")
endif()

# script_check: offline strict typecheck of a pack's scripts with the engine's exact
# environment (sandbox-removed globals + engine.level<N>.d.luau). Stock luau-analyze has no
# way to load engine definitions, so content CI should prefer this.
add_executable(script_check tools/script_check.cpp)
target_link_libraries(script_check PRIVATE eng::script_host)
_engine_warnings(script_check)
set(ENGINE_SCRIPT_CHECK_EXECUTABLE "$<TARGET_FILE:script_check>" CACHE INTERNAL
    "Generator expression for script_check (usage: script_check <pack_dir>...)")
if(ENGINE_BUILD_TESTS)
    add_test(NAME script_check_sample_pack
             COMMAND script_check ${PROJECT_SOURCE_DIR}/content/packs/sample.vertical_slice)
endif()
