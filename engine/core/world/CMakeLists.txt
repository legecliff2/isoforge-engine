# world: Chunk model, IChunkProvider, HandcraftedProvider (ARCHITECTURE.md §5.2).
engine_add_module(world
    SOURCES     src/chunk_codec.cpp src/handcrafted_provider.cpp
    PUBLIC_DEPS base ecs assets
    TESTS       tests/chunk_tests.cpp)
if(TARGET world_tests)
    target_compile_definitions(world_tests PRIVATE ENGINE_CORE_FIXTURES="${ENGINE_CORE_FIXTURES}"
        ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
        CHUNK_HASH_VECTORS="${PROJECT_SOURCE_DIR}/tools/chunk_hash/vectors")
endif()
if(TARGET world_tests)
    find_package(Threads REQUIRED)
    target_link_libraries(world_tests PRIVATE Threads::Threads)
endif()
