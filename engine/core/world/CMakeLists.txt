# world: Chunk model, IChunkProvider, FileChunkProvider, LayeredProvider, ChunkStreamer (ARCHITECTURE.md §5.2).
engine_add_module(world
    SOURCES     src/chunk_codec.cpp src/file_chunk_provider.cpp src/chunk_streamer.cpp
    PUBLIC_DEPS base ecs assets rng
    TESTS       tests/chunk_tests.cpp tests/streamer_tests.cpp)
if(TARGET world_tests)
    target_compile_definitions(world_tests PRIVATE ENGINE_CORE_FIXTURES="${ENGINE_CORE_FIXTURES}"
        ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
        CHUNK_HASH_VECTORS="${PROJECT_SOURCE_DIR}/tools/chunk_hash/vectors")
endif()
if(TARGET world_tests)
    find_package(Threads REQUIRED)
    target_link_libraries(world_tests PRIVATE Threads::Threads)
endif()
# ChunkStreamer worker threads.
find_package(Threads REQUIRED)
target_link_libraries(eng_world PUBLIC Threads::Threads)
