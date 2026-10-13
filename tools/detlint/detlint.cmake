# Determinism lint (ARCHITECTURE.md §4.7) for source trees that feed sim/gen state.
#   cmake -DDIRS="<dir>;<dir>" -P detlint.cmake
# Rejects: float, double, long double, <cmath>/<math.h>, <random>, std::hash, std::unordered_*,
# <chrono>/std::chrono, rand()/srand(), and float literals (1.0, 2.5f, 1e3).
# Comments are stripped first so prose can mention the banned words.
cmake_minimum_required(VERSION 3.28)
set(bad
    "(^|[^A-Za-z0-9_])(float|double)([^A-Za-z0-9_]|$)"
    "#[ \t]*include[ \t]*<(cmath|math\\.h|random|chrono|cfloat|float\\.h)>"
    "std::hash"
    "unordered_(map|set|multimap|multiset)"
    "std::chrono"
    "(^|[^A-Za-z0-9_:.])s?rand[ \t]*\\("
    "(^|[^A-Za-z0-9_.])[0-9]+\\.[0-9]*([eE][-+]?[0-9]+)?[fFlL]?([^A-Za-z0-9_.]|$)"
    "(^|[^A-Za-z0-9_.x])[0-9]+[eE][-+]?[0-9]+([^A-Za-z0-9_]|$)")
set(n 0)
set(fail "")
foreach(dir IN LISTS DIRS)
    file(GLOB_RECURSE files "${dir}/*.hpp" "${dir}/*.cpp" "${dir}/*.h")
    list(SORT files)
    foreach(f IN LISTS files)
        math(EXPR n "${n} + 1")
        file(READ "${f}" src)
        # Strip /* */ and // comments and string literals (approximate but sufficient here).
        string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" src "${src}")
        string(REGEX REPLACE "//[^\n]*" "" src "${src}")
        string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" src "${src}")
        string(REPLACE ";" "," src "${src}")
        string(REPLACE "\n" ";" lines "${src}")
        set(lineno 0)
        foreach(line IN LISTS lines)
            math(EXPR lineno "${lineno} + 1")
            foreach(re IN LISTS bad)
                if(line MATCHES "${re}")
                    string(APPEND fail "  ${f}:${lineno}: ${line}\n")
                    break()
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()
if(fail)
    message(FATAL_ERROR "detlint: banned non-deterministic constructs (§4.7):\n${fail}")
endif()
message(STATUS "detlint: OK (${n} files)")
