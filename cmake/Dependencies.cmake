# Third-party dependencies, pinned (ARCHITECTURE.md §6).
include(FetchContent)

# doctest: unit test framework. Pinned to a release tag.
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)
