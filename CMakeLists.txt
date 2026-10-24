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

# Engine core (ARCHITECTURE.md §2.1): base -> ecs, assets -> world.
add_subdirectory(engine/base)      # target:  eng::base
add_subdirectory(engine/core)      # targets: eng::ecs, eng::assets, eng::world
add_subdirectory(engine/procgen)   # targets: eng::procgen (base+detmath only), eng::procgen_world (glue)

# Presentation stack (§2.1).
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
        # Pack scripts are strict-typechecked inside this test with Engine Core's script_check
        # (exact pack-load environment, §3.3; stock luau-analyze can't load engine defs).
        set(_script_check_args "")
        if(ENGINE_SCRIPT_CHECK_EXECUTABLE)
            set(_script_check_args --script-check "${ENGINE_SCRIPT_CHECK_EXECUTABLE}")
        else()
            message(WARNING "content_validate: ENGINE_SCRIPT_CHECK_EXECUTABLE not set, "
                            "pack scripts will not be type-checked")
        endif()
        add_test(NAME content_validate
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/validate_packs.py"
                         ${_script_check_args}
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # Negative tests: each tests/validate/cases/*.json must fail with its expected message.
        add_test(NAME content_validate_negative
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/run_negative.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # Validator unit tests (e.g. unknown effect kinds warn, known kinds' fields are checked).
        # Pack-set resolution: depends, semver ranges, cycles, duplicate pack/content ids.
        add_test(NAME content_validate_packset
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/run_packset_cases.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME content_validate_tests
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/test_validate_packs.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # @4 / item@1 (docs/proposals/VERSION_BUMP_BATCH.md row 2): common@2 stray-ref lint, per-arm allowed ==
        # required + optional, 3->4 migration reference + @4 schema shapes, and the @4 validator cross-reference cases.
        add_test(NAME content_validate_lint
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_common2_lint.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # item@1 optional `visual` block (LIGHTING.md 10.1): accept / reject cases, content_hash unchanged.
        add_test(NAME content_validate_item_visual
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_item_visual.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME content_validate_arms
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_union_arms.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME content_validate_migrations
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_migrations_v4.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # item@2 / monster@5 / character@5 (docs/proposals/EQUIPMENT.md 7): identity re-stamps, new shapes, structural checks.
        add_test(NAME content_validate_equip_v5
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_equip_schemas_v5.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # srd51.core monster@4 batch: key stats vs the 5e-database table (tests/validate/data/).
        add_test(NAME content_validate_srd51_monsters
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_srd51_monsters.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # srd51.core character@4 class levels (Fighter / Cleric / Wizard 1-5) vs the 5e-database class tables.
        add_test(NAME content_validate_srd51_classes
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_srd51_classes.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # srd51.core spell@1 batch vs the 5e-database spell table.
        add_test(NAME content_validate_srd51_spells
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_srd51_spells.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME content_validate_v4
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_v4_validator.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # srd51.core weapons/armor/shields (item@1) against an independent data table.
        add_test(NAME content_validate_equipment
                 COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/test_srd51_equipment.py"
                 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        # Pure-Python validator tests: no compiled code involved, so the result cannot differ by
        # compiler or build type. CI runs them in the Debug jobs and skips them (ctest -LE python_only)
        # in the Release jobs to save minutes. content_validate itself is NOT labelled: it runs the
        # compiled script_check, so every job keeps it.
        set_tests_properties(content_validate_negative content_validate_packset content_validate_tests
                             content_validate_lint content_validate_arms content_validate_migrations
                             content_validate_srd51_monsters content_validate_srd51_classes
                             content_validate_srd51_spells content_validate_v4 content_validate_equipment
                             PROPERTIES LABELS python_only)
        # Procedurally generated chunks must pass the same validator (schema + content_hash).
        if(TARGET procgen_dump)
            set(_pg_out "${CMAKE_BINARY_DIR}/procgen_preview_pack")
            add_test(NAME procgen_pack_dump
                     COMMAND procgen_dump g1-K3QZ7M2XAB4PE "${_pg_out}" 0,0,0 -1,-1,0 5,-3,-2 -1000000,123456,7)
            set_tests_properties(procgen_pack_dump PROPERTIES FIXTURES_SETUP procgen_pack)
            add_test(NAME procgen_pack_validate
                     COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate/validate_packs.py" "${_pg_out}"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(procgen_pack_validate PROPERTIES FIXTURES_REQUIRED procgen_pack)
            # Every archetype procgen can emit must exist in the default packs (else the sim skips it).
            add_test(NAME procgen_archetypes_defined
                     COMMAND "${_venv_py}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate/check_procgen_archetypes.py"
                             $<TARGET_FILE:procgen_dump> "${_pg_out}"
                     WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(procgen_archetypes_defined PROPERTIES FIXTURES_REQUIRED procgen_pack)
        endif()
    endif()
endif()

# Golden corpus freeze: tests/compat/ must match tests/compat/MANIFEST.sha256 (no Python needed).
if(ENGINE_BUILD_TESTS)
    add_test(NAME compat_freeze
             COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/compat_freeze/compat_freeze.cmake)
endif()
# Frozen script API definitions (§3.3: a released engine.level<N>.d.luau is byte-frozen).
if(ENGINE_BUILD_TESTS)
    add_test(NAME api_freeze
             COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR}
                     -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/engine/core/script_host/api
                     -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/engine/core/script_host/api/FROZEN.sha256
                     "-DINCLUDE=^engine\\.level1\\.d\\.luau$"   # level 2 stays unfrozen until row 16b
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/compat_freeze/compat_freeze.cmake)
endif()

# Determinism lint over rules5e / detmath / sim / world/gen (§4.7). Registry: cmake/DetLint.cmake.
include(DetLint)

# Vertical slice viewer (session/view libs + tests always; executable needs SDL).
add_subdirectory(game/vertical_slice)

# R-G13 light rebuild benchmark (tools/light_bench).
if(ENGINE_BUILD_TESTS)
    add_subdirectory(tools/light_bench)
endif()

# Headless encounter scenarios (tools/encounter_runner). Tests compare each scenarios/* golden.
add_subdirectory(tools/encounter_runner)

# Module dependency graph + include lint (§2.2 rules 1, 3-5). Must come after all modules.
include(DepCheck)
