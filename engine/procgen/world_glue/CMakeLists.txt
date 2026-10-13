# procgen_world: thin glue (world + procgen) providing ProceduralProvider : IChunkProvider.
# Lives outside engine/core so world keeps no procgen dependency; Engine Core may adopt it
# (see docs/PROCGEN.md).
engine_add_module(procgen_world
    SOURCES     src/procedural_provider.cpp
    PUBLIC_DEPS world procgen
    TESTS       tests/procgen_world_tests.cpp tests/streaming_tests.cpp)
if(TARGET procgen_world_tests)
    find_package(Threads REQUIRED)
    target_link_libraries(procgen_world_tests PRIVATE Threads::Threads)
endif()
if(TARGET procgen_world_tests)
    target_compile_definitions(procgen_world_tests PRIVATE PROCGEN_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
endif()
