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
    # Scratch files go under this build tree with the pid appended (scratch_dir.hpp, same
    # pattern as script_host in #25), never a fixed path in the shared system temp dir.
    target_compile_definitions(procgen_world_tests PRIVATE
        TEST_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/test_scratch")
endif()
