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

# Content: validate every pack (content/packs/*) and the frozen compat corpus
# (tests/compat/fl*/*) against schemas/ (§3). A private venv is created in the
# build dir at configure time and the pinned tools/validate/requirements.txt is
# installed into it, so system Python (often PEP 668 managed) is never touched.
# Needs network on first configure; set -DENGINE_CONTENT_VALIDATE=OFF to skip.
option(ENGINE_CONTENT_VALIDATE "Add the content_validate test (creates a Python venv, needs pip/network once)" ON)
if(ENGINE_BUILD_TESTS AND ENGINE_CONTENT_VALIDATE)
    find_package(Python3 3.9 COMPONENTS Interpreter)
    set(_req "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/requirements.txt")
    set(_venv "${CMAKE_BINARY_DIR}/content-venv")
    if(WIN32)
        set(_venv_py "${_venv}/Scripts/python.exe")
    else()
        set(_venv_py "${_venv}/bin/python")
    endif()
    set(_content_ok OFF)
    if(Python3_Interpreter_FOUND)
        file(SHA256 "${_req}" _req_hash)
        set(_stamp "${_venv}/.requirements.sha256")
        set(_old_hash "")
        if(EXISTS "${_stamp}")
            file(READ "${_stamp}" _old_hash)
        endif()
        if(EXISTS "${_venv_py}" AND _old_hash STREQUAL _req_hash)
            set(_content_ok ON)
        else()
            message(STATUS "content_validate: creating venv ${_venv}")
            execute_process(COMMAND "${Python3_EXECUTABLE}" -m venv "${_venv}"
                            RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_VARIABLE _err)
            if(_rc EQUAL 0)
                execute_process(COMMAND "${_venv_py}" -m pip install --disable-pip-version-check -q -r "${_req}"
                                RESULT_VARIABLE _rc ERROR_VARIABLE _err)
            endif()
            if(_rc EQUAL 0)
                file(WRITE "${_stamp}" "${_req_hash}")
                set(_content_ok ON)
            else()
                message(WARNING "content_validate disabled: venv/pip install failed (offline?). "
                                "Re-run configure with network, or pass -DENGINE_CONTENT_VALIDATE=OFF.\n${_err}")
            endif()
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_req}")
    else()
        message(WARNING "content_validate disabled: Python3 not found")
    endif()
    if(_content_ok)
        add_test(NAME content_validate
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/validate_packs.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
