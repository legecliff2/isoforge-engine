# engine_add_module(<name>
#     SOURCES      src/a.cpp ...
#     PUBLIC_DEPS  <module> ...
#     PRIVATE_DEPS <module> ...
#     TESTS        tests/a_tests.cpp ...
#     TEST_DEPS    <module> ...      (extra modules linked into the test only)
#     [DETERMINISTIC])
#
# Creates static lib `eng_<name>` with alias `eng::<name>`, public headers in
# include/, warnings, and a `<name>_tests` executable registered with CTest.
# DETERMINISTIC adds -ffp-contract=off (GCC/Clang) or /fp:precise (MSVC), see §4.3.
function(engine_add_module name)
    cmake_parse_arguments(M "DETERMINISTIC" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS;TESTS;TEST_DEPS" ${ARGN})
    set(tgt eng_${name})
    add_library(${tgt} STATIC ${M_SOURCES})
    add_library(eng::${name} ALIAS ${tgt})
    target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(${tgt} PUBLIC cxx_std_20)
    set_target_properties(${tgt} PROPERTIES CXX_EXTENSIONS OFF)
    foreach(d IN LISTS M_PUBLIC_DEPS)
        target_link_libraries(${tgt} PUBLIC eng::${d})
    endforeach()
    foreach(d IN LISTS M_PRIVATE_DEPS)
        target_link_libraries(${tgt} PRIVATE eng::${d})
    endforeach()
    _engine_warnings(${tgt})
    if(M_DETERMINISTIC)
        _engine_deterministic(${tgt})
    endif()
    # Record dependency edges for tools/depcheck (§2.2).
    set_property(GLOBAL APPEND PROPERTY ENGINE_MODULE_EDGES "${name}:${M_PUBLIC_DEPS};${M_PRIVATE_DEPS}")

    if(ENGINE_BUILD_TESTS AND M_TESTS)
        set(t ${name}_tests)
        add_executable(${t} ${M_TESTS})
        target_link_libraries(${t} PRIVATE ${tgt} doctest::doctest_with_main)
        foreach(d IN LISTS M_TEST_DEPS)
            target_link_libraries(${t} PRIVATE eng::${d})
        endforeach()
        set_target_properties(${t} PROPERTIES CXX_EXTENSIONS OFF)
        _engine_warnings(${t})
        if(M_DETERMINISTIC)
            _engine_deterministic(${t})
        endif()
        add_test(NAME ${t} COMMAND ${t})
    endif()
endfunction()

function(_engine_warnings tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /permissive-)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
        if(ENGINE_WARNINGS_AS_ERRORS)
            target_compile_options(${tgt} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(_engine_deterministic tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /fp:precise)
    else()
        target_compile_options(${tgt} PRIVATE -ffp-contract=off -fno-fast-math)
    endif()
    target_compile_definitions(${tgt} PRIVATE RULES_STANDALONE=1)
endfunction()
