# DepCheck (ARCHITECTURE.md §2.2 rule 1, §6): emits the real CMake target graph of every
# engine_add_module() target to ${CMAKE_BINARY_DIR}/depgraph.txt and registers the
# `depcheck` test, which validates it against tools/depcheck/rules.cmake.
# Include this AFTER all add_subdirectory() calls.
get_property(_mods GLOBAL PROPERTY ENGINE_MODULES)

# Non-module targets in the source tree: game/* and tools (any .../tools/ dir) executables and
# their local helper libs become nodes "app:<kind>:<target>". Tests and third-party are skipped.
set(_apps "")
function(_collect_apps dir)
    get_property(_ts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS _ts)
        get_target_property(_type ${t} TYPE)
        if(NOT _type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY)$"
           OR t MATCHES "^eng_" OR t MATCHES "_tests$")
            continue()
        endif()
        get_target_property(_src ${t} SOURCES)
        set(_kind "")
        file(RELATIVE_PATH _rel "${PROJECT_SOURCE_DIR}" "${dir}")
        if(_rel MATCHES "^game(/|$)")
            set(_kind game)
        elseif(_rel MATCHES "(^|/)tools(/|$)" OR _src MATCHES "(^|;)tools/")
            set(_kind tool)
        endif()
        if(_kind)
            set_property(GLOBAL APPEND PROPERTY ENGINE_APPS "${_kind}:${t}")
        endif()
    endforeach()
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(sd IN LISTS _subs)
        string(FIND "${sd}" "${PROJECT_SOURCE_DIR}/" _p)
        string(FIND "${sd}" "${CMAKE_BINARY_DIR}" _b)
        if(_p EQUAL 0 AND NOT _b EQUAL 0)
            _collect_apps("${sd}")
        endif()
    endforeach()
endfunction()
_collect_apps("${PROJECT_SOURCE_DIR}")
get_property(_apps GLOBAL PROPERTY ENGINE_APPS)
set(_app_names "")
foreach(a IN LISTS _apps)
    string(REGEX REPLACE "^[^:]+:" "" _n "${a}")
    list(APPEND _app_names "${_n}")
endforeach()

set(_nodes "")
foreach(m IN LISTS _mods)
    list(APPEND _nodes "${m}|eng_${m}")
endforeach()
foreach(a IN LISTS _apps)
    string(REGEX REPLACE "^[^:]+:" "" _n "${a}")
    list(APPEND _nodes "app:${a}|${_n}")
endforeach()

set(_graph "")
foreach(node IN LISTS _nodes)
    string(REPLACE "|" ";" node "${node}")
    list(GET node 0 m)
    list(GET node 1 _tgt)
    set(_deps "")
    foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(_ll ${_tgt} ${prop})
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
                elseif(l IN_LIST _app_names)
                    foreach(a IN LISTS _apps)
                        if(a MATCHES ":${l}$")
                            set(l "app:${a}")
                            break()
                        endif()
                    endforeach()
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
    foreach(bad bad_upward bad_sdl bad_unknown bad_tool_renderer bad_game_sdl)
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
