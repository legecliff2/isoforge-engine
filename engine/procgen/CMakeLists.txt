# procgen: deterministic procedural generation (ARCHITECTURE.md §4.4, §4.6).
# Depends ONLY on base + detmath/rng (std-only, deterministic). It does not know about
# world::Chunk; it emits a mirror (GenChunk) and chunk@1 JSON. world_glue/ adapts it to
# IChunkProvider (ProceduralProvider), see docs/PROCGEN.md.
engine_add_module(procgen
    SOURCES     src/seed.cpp src/dungeon.cpp
    PUBLIC_DEPS detmath rng base
    TESTS       tests/procgen_tests.cpp
    DETERMINISTIC)
if(TARGET procgen_tests)
    find_package(Threads REQUIRED)
    target_link_libraries(procgen_tests PRIVATE Threads::Threads)
endif()

# Determinism lint (§4.7): no float/double/<cmath>/<random>/std::hash/unordered/chrono/rand
# anywhere in procgen (glue included).
if(ENGINE_BUILD_TESTS)
    add_test(NAME procgen_detlint
             COMMAND ${CMAKE_COMMAND} -DDIRS=${CMAKE_CURRENT_SOURCE_DIR}
                     -P ${PROJECT_SOURCE_DIR}/tools/detlint/detlint.cmake)
endif()

# procgen_dump <seed-string> <out_pack_dir> <x,y,level>... : writes a chunk@1 pack (used by
# the procgen_pack_validate test, and handy for previewing seeds).
add_executable(procgen_dump tools/procgen_dump.cpp)
target_link_libraries(procgen_dump PRIVATE eng::procgen)
_engine_warnings(procgen_dump)

add_subdirectory(world_glue)
