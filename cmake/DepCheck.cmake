# DepCheck (ARCHITECTURE.md §2.2 rule 1, §6): emits the real CMake target graph of every
# engine_add_module() target to ${CMAKE_BINARY_DIR}/depgraph.txt and registers the
# `depcheck` test, which validates it against tools/depcheck/rules.cmake.
# Include this AFTER all add_subdirectory() calls.
get_property(_mods GLOBAL PROPERTY ENGINE_MODULES)
set(_graph "")
foreach(m IN LISTS _mods)
    set(_deps "")
    foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_ll eng_${m} ${prop})
        if(_ll)
            foreach(l IN LISTS _ll)
                string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" l "${l}")
                if(l MATCHES "^\\$<")
                    continue()   # other generator expressions: not module edges
                endif()
                if(l MATCHES "^eng::(.+)$")
                    set(l "${CMAKE_MATCH_1}")
                elseif(l MATCHES "^eng_(.+)$" AND NOT l MATCHES "^eng_thirdparty_" AND TARGET "${l}")
                    set(l "${CMAKE_MATCH_1}")
                else()
                    set(l "ext:${l}")
                endif()
                if(NOT l STREQUAL m)
                    list(APPEND _deps "${l}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _deps)
    list(SORT _deps)
    list(JOIN _deps "," _deps_s)
    string(APPEND _graph "${m}=${_deps_s}\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/depgraph.txt" "${_graph}")

if(ENGINE_BUILD_TESTS)
    add_test(NAME depcheck
             COMMAND ${CMAKE_COMMAND} -DGRAPH=${CMAKE_BINARY_DIR}/depgraph.txt
                     -DRULES=${PROJECT_SOURCE_DIR}/tools/depcheck/rules.cmake
                     -P ${PROJECT_SOURCE_DIR}/tools/depcheck/depcheck.cmake)
    add_test(NAME include_lint
             COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                     -P ${PROJECT_SOURCE_DIR}/tools/include_lint/include_lint.cmake)
endif()

# Self-tests: the checkers must reject known-bad inputs.
if(ENGINE_BUILD_TESTS)
    foreach(bad bad_upward bad_sdl bad_unknown)
        add_test(NAME depcheck_rejects_${bad}
                 COMMAND ${CMAKE_COMMAND} -DGRAPH=${PROJECT_SOURCE_DIR}/tools/depcheck/tests/${bad}.txt
                         -DRULES=${PROJECT_SOURCE_DIR}/tools/depcheck/rules.cmake
                         -P ${PROJECT_SOURCE_DIR}/tools/depcheck/depcheck.cmake)
        set_tests_properties(depcheck_rejects_${bad} PROPERTIES WILL_FAIL TRUE)
    endforeach()
    add_test(NAME include_lint_rejects_sdl
             COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}/tools/include_lint/tests/bad
                     -P ${PROJECT_SOURCE_DIR}/tools/include_lint/include_lint.cmake)
    set_tests_properties(include_lint_rejects_sdl PROPERTIES WILL_FAIL TRUE)
endif()
