# game/presentation: SDL-free PNG decode and monster stand-in kits (R-M0).
# iso2d stays a sprite batcher; it does not open files or decode PNG.
# third_party/puff is Mark Adler's puff 2.3 (zlib contrib), unaltered. The copyright
# and redistribution terms are in puff.h.
add_library(presentation STATIC src/png.cpp src/standin.cpp third_party/puff/puff.c)
target_include_directories(presentation PUBLIC include PRIVATE third_party/puff)
target_link_libraries(presentation PUBLIC eng::base)
_engine_warnings(presentation)
if(MSVC)
    set_source_files_properties(third_party/puff/puff.c PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(third_party/puff/puff.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(ENGINE_BUILD_TESTS)
    add_executable(presentation_tests tests/presentation_tests.cpp)
    target_link_libraries(presentation_tests PRIVATE presentation doctest::doctest_with_main)
    target_compile_definitions(presentation_tests PRIVATE
        STANDIN_ROOT="${PROJECT_SOURCE_DIR}/content/art/monster_standins")
    _engine_warnings(presentation_tests)
    add_test(NAME presentation_tests COMMAND presentation_tests)
endif()
