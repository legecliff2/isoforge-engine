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
    include(Dependencies)
endif()

# Modules are added by their owners per docs/ARCHITECTURE.md.
# Deterministic, engine-independent modules (§2.2 rule 2): no SDL, no I/O.
add_subdirectory(engine/detmath)   # targets: eng::rng, eng::detmath
add_subdirectory(engine/rules)     # target:  eng::rules5e

# Content: validate every pack (content/packs/*) and the frozen compat corpus
# (tests/compat/fl*/*) against schemas/ (§3). Needs Python 3 + jsonschema
# (pip install -r tools/validate/requirements.txt).
if(ENGINE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME content_validate
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/validate_packs.py
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    else()
        message(WARNING "Python3 not found: content_validate test disabled")
    endif()
endif()
