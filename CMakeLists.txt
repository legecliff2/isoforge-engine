cmake_minimum_required(VERSION 3.28)
project(isoforge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENGINE_BUILD_TESTS "Build unit tests" ON)
option(ENGINE_WARNINGS_AS_ERRORS "Treat warnings as errors (CI)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EngineModule)

if(ENGINE_BUILD_TESTS)
    enable_testing()
endif()
include(Dependencies)

# Modules are added by their owners per docs/ARCHITECTURE.md.
# Deterministic, engine-independent modules (§2.2 rule 2): no SDL, no I/O.
add_subdirectory(engine/detmath)   # targets: eng::rng, eng::detmath
add_subdirectory(engine/rules)     # target:  eng::rules5e

# Engine core (ARCHITECTURE.md §2.1): base -> ecs, assets -> world.
add_subdirectory(engine/base)      # target:  eng::base
add_subdirectory(engine/core)      # targets: eng::ecs, eng::assets, eng::world
