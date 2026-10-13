cmake_minimum_required(VERSION 3.28)
project(isoforge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENGINE_BUILD_TESTS "Build unit tests" ON)
option(ENGINE_WARNINGS_AS_ERRORS "Treat warnings as errors (CI)" OFF)
option(ENGINE_HEADLESS "Null renderer only: no SDL, no window (servers/CI)" OFF)
option(ENGINE_BACKEND_SDLGPU "Build the SDL_GPU render backend" ON)
option(ENGINE_BUILD_SANDBOX "Build the game/sandbox iso grid demo" ON)

if(ENGINE_HEADLESS)
    set(ENGINE_WITH_SDL OFF)
else()
    set(ENGINE_WITH_SDL ON)
endif()

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

# Presentation stack (§2.1).
add_subdirectory(engine/base)          # eng::base
add_subdirectory(engine/render/api)    # eng::render_api
add_subdirectory(engine/render/null)   # eng::render_null
add_subdirectory(engine/iso2d)         # eng::iso2d
if(ENGINE_WITH_SDL)
    add_subdirectory(engine/platform)  # eng::platform (SDL3)
    if(ENGINE_BACKEND_SDLGPU)
        add_subdirectory(engine/render/sdlgpu)  # eng::render_sdlgpu
        if(ENGINE_BUILD_SANDBOX)
            add_subdirectory(game/sandbox)
        endif()
    endif()
endif()
