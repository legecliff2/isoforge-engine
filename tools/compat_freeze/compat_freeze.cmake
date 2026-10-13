cmake_minimum_required(VERSION 3.28)
# Freeze check for tests/compat/ (ARCHITECTURE.md §3.2 golden corpus).
#   Verify: cmake -DROOT=<repo> -P tools/compat_freeze/compat_freeze.cmake
#   Update: cmake -DROOT=<repo> -DUPDATE=ON -P tools/compat_freeze/compat_freeze.cmake
# The manifest (tests/compat/MANIFEST.sha256) lists "<sha256>  <path>" for every file under
# tests/compat/fl<N>/, sorted by path. Verification fails on any added, removed or
# modified file. UPDATE=ON is only for a sanctioned refreeze (new feature level, or a
# Director-approved one-time regeneration); such a PR shows the manifest diff for review.
if(NOT ROOT)
    get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
# Optional overrides (reused for frozen API definition files):
#   -DDIR=<abs dir>  -DMANIFEST=<abs file>  -DINCLUDE=<regex on dir-relative paths>
if(NOT DIR)
    set(DIR "${ROOT}/tests/compat")
endif()
if(NOT MANIFEST)
    set(MANIFEST "${DIR}/MANIFEST.sha256")
endif()
if(NOT INCLUDE)
    set(INCLUDE "^fl[0-9]+/")   # frozen corpora only; README etc. stay editable
endif()
set(dir "${DIR}")
set(manifest "${MANIFEST}")

file(GLOB_RECURSE files LIST_DIRECTORIES false RELATIVE "${dir}" "${dir}/*")
list(REMOVE_ITEM files "MANIFEST.sha256")
list(FILTER files INCLUDE REGEX "${INCLUDE}")
list(SORT files)
set(actual "")
foreach(f IN LISTS files)
    file(SHA256 "${dir}/${f}" h)
    string(APPEND actual "${h}  ${f}\n")
endforeach()

if(UPDATE)
    file(WRITE "${manifest}" "${actual}")
    list(LENGTH files n)
    message(STATUS "compat_freeze: wrote ${manifest} (${n} files)")
    return()
endif()

if(NOT EXISTS "${manifest}")
    message(FATAL_ERROR "compat_freeze: ${manifest} missing")
endif()
file(READ "${manifest}" expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT expected STREQUAL actual)
    string(REPLACE "\n" ";" exp_l "${expected}")
    string(REPLACE "\n" ";" act_l "${actual}")
    set(diff "")
    foreach(l IN LISTS exp_l)
        if(l AND NOT l IN_LIST act_l)
            string(APPEND diff "  - ${l}\n")
        endif()
    endforeach()
    foreach(l IN LISTS act_l)
        if(l AND NOT l IN_LIST exp_l)
            string(APPEND diff "  + ${l}\n")
        endif()
    endforeach()
    message(FATAL_ERROR "compat_freeze: ${dir} differs from ${manifest} "
                        "(- expected, + actual):\n${diff}"
                        "Frozen files must never change (tests/compat/README.md, ARCHITECTURE.md §3.2/§3.3).")
endif()
list(LENGTH files n)
message(STATUS "compat_freeze: OK (${n} files)")
