cmake_minimum_required(VERSION 3.28)
# cmake -DROOT=<repo> -P include_lint.cmake
# ARCHITECTURE.md §2.2:
#   rule 3: no `#include <SDL3/...>` (or "SDL...") outside engine/platform and engine/render.
#   rule 5: no include path reaching into another module's src/ ("../src/", "/src/").
#   rule 4: backend headers (render_null/, render_sdlgpu/) only from tests/, engine/engine, or the backend itself.
file(GLOB_RECURSE files RELATIVE "${ROOT}"
     "${ROOT}/engine/*.h" "${ROOT}/engine/*.hpp" "${ROOT}/engine/*.c" "${ROOT}/engine/*.cpp" "${ROOT}/engine/*.inl"
     "${ROOT}/game/*.hpp" "${ROOT}/game/*.cpp" "${ROOT}/tools/*.hpp" "${ROOT}/tools/*.cpp"
     "${ROOT}/tests/*.hpp" "${ROOT}/tests/*.cpp")
list(FILTER files EXCLUDE REGEX "^tools/include_lint/tests/")
set(errors "")
foreach(f IN LISTS files)
    file(STRINGS "${ROOT}/${f}" incs REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")
    foreach(inc IN LISTS incs)
        string(STRIP "${inc}" inc)
        if(inc MATCHES "[<\"]SDL3?(/|_|\\.h)" AND NOT f MATCHES "^engine/(platform|render)/")
            string(APPEND errors "  ${f}: SDL include outside engine/platform, engine/render: ${inc}\n")
        endif()
        if(inc MATCHES "[<\"](\\.\\./)+[^>\"]*src/" OR inc MATCHES "[<\"][^>\"]*/src/")
            string(APPEND errors "  ${f}: include reaches into a module's src/: ${inc}\n")
        endif()
        if(inc MATCHES "[<\"]render_(null|sdlgpu)/"
           AND NOT f MATCHES "/tests/" AND NOT f MATCHES "^tests/"
           AND NOT f MATCHES "^engine/render/" AND NOT f MATCHES "^engine/engine/")
            string(APPEND errors "  ${f}: backend header outside engine/tests: ${inc}\n")
        endif()
    endforeach()
endforeach()
list(LENGTH files n)
if(errors)
    message(FATAL_ERROR "include_lint: violations (ARCHITECTURE.md §2.2 rules 3-5):\n${errors}")
endif()
message(STATUS "include_lint: OK (${n} files)")
