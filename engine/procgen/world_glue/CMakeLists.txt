# procgen_world: thin glue (world + procgen) providing ProceduralProvider : IChunkProvider.
# Lives outside engine/core so world keeps no procgen dependency; Engine Core may adopt it
# (see docs/PROCGEN.md).
engine_add_module(procgen_world
    SOURCES     src/procedural_provider.cpp
    PUBLIC_DEPS world procgen
    TESTS       tests/procgen_world_tests.cpp)
if(TARGET procgen_world_tests)
    find_package(Threads REQUIRED)
    target_link_libraries(procgen_world_tests PRIVATE Threads::Threads)
    # Scratch files go under this build tree with the pid appended (script_host's shared
    # tests/scratch_dir.hpp from #25), never a fixed path in the shared system temp dir.
    target_include_directories(procgen_world_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/core/script_host/tests")
    target_compile_definitions(procgen_world_tests PRIVATE
        TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_scratch")
endif()
