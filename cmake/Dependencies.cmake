# Third-party dependencies, pinned (ARCHITECTURE.md §6).
include(FetchContent)

# nlohmann/json: JSON load for assets/world (§6 "nlohmann/json or simdjson").
# Pinned by release tarball + SHA256.
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# xxHash: StableHash = xxHash3-64 (§4.5). Header-only use (XXH_INLINE_ALL).
FetchContent_Declare(xxhash
    URL      https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.2.tar.gz
    URL_HASH SHA256=baee0c6afd4f03165de7a4e67988d16f0f2b257b51d0e3cb91909302a26a79c4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  _no_cmake_)   # don't build its CLI; we only need xxhash.h
FetchContent_MakeAvailable(xxhash)
add_library(eng_thirdparty_xxhash INTERFACE)
target_include_directories(eng_thirdparty_xxhash SYSTEM INTERFACE ${xxhash_SOURCE_DIR})

# Luau: scripting (D-005, §3.3). Pinned release tag + SHA256. The engine links VM, Compiler
# and Analysis only; Luau.CodeGen is never linked (native codegen off in sim/gen, §3.3).
# Luau's CLI targets are declared so content tooling can use `luau-analyze`, but the whole
# Luau subtree is EXCLUDE_FROM_ALL: only targets something depends on get built (the REPL and
# CodeGen are never built by default).
set(LUAU_BUILD_CLI   ON  CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(luau
    URL      https://github.com/luau-lang/luau/archive/refs/tags/0.742.tar.gz
    URL_HASH SHA256=c065ac4e0a7f147a5db0429163d8d4d61cd18281a83195185823d9810f0e8284
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(luau)

# luau-analyze (strict offline typecheck for content, e.g. a ctest over scripts/**/*.luau).
# Built as part of `all` via the luau_analyze target. Use in add_test as
#   COMMAND ${LUAU_ANALYZE_EXECUTABLE} ...      (expands to $<TARGET_FILE:Luau.Analyze.CLI>)
add_custom_target(luau_analyze ALL)
add_dependencies(luau_analyze Luau.Analyze.CLI)
set(LUAU_ANALYZE_EXECUTABLE "$<TARGET_FILE:Luau.Analyze.CLI>" CACHE INTERNAL
    "Generator expression for the pinned luau-analyze binary (usable in add_test/add_custom_command)")

if(ENGINE_BUILD_TESTS)
    # doctest: unit test framework. Pinned to a release tag.
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()
