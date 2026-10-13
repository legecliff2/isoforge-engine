# Third-party dependencies, pinned (ARCHITECTURE.md §6).
include(FetchContent)

if(ENGINE_BUILD_TESTS)
    # doctest: unit test framework. Pinned to a release tag.
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

if(ENGINE_WITH_SDL)
    # SDL3: platform layer + SDL_GPU backend (D-003). Pinned to a release tag.
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.8
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()
