# Determinism lint registry (ARCHITECTURE.md §2.2 rule 8, §4.7). One ctest per entry:
#   detlint_<name>  ->  tools/detlint/detlint.cmake over <dir>
# To cover a new deterministic tree, add a line to ENGINE_DETLINT_TREES. Entries whose
# directory doesn't exist yet are skipped silently, so future paths (sim, world/gen) can be
# listed now and start being checked the moment they land. Put a name in
# ENGINE_DETLINT_REPORT_ONLY (with a TODO) to downgrade known violations to warnings.
# procgen keeps its own procgen_detlint test (engine/procgen/CMakeLists.txt).
set(ENGINE_DETLINT_TREES
    "rules5e=engine/rules"
    "detmath=engine/detmath"        # rng + detmath
    "sim=engine/core/sim"           # sim v0 (PR #14); checked once merged
    "world_gen=engine/core/world/gen"   # world procgen runtime (§2.2); not created yet
)
set(ENGINE_DETLINT_REPORT_ONLY "")   # e.g. "sim" -- TODO(owner): remove once fixed

if(ENGINE_BUILD_TESTS)
    foreach(entry IN LISTS ENGINE_DETLINT_TREES)
        string(REPLACE "=" ";" kv "${entry}")
        list(GET kv 0 name)
        list(GET kv 1 rel)
        set(dir "${CMAKE_CURRENT_SOURCE_DIR}/${rel}")
        if(NOT IS_DIRECTORY "${dir}")
            continue()
        endif()
        set(report OFF)
        if(name IN_LIST ENGINE_DETLINT_REPORT_ONLY)
            set(report ON)
        endif()
        add_test(NAME detlint_${name}
                 COMMAND ${CMAKE_COMMAND} -DDIRS=${dir} -DREPORT_ONLY=${report}
                         -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/detlint/detlint.cmake)
    endforeach()
endif()
