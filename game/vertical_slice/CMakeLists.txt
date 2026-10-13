# game/vertical_slice: M1 slice viewer. world + sim (Engine Core) feed a plain-data ViewModel;
# SliceView (iso2d/render_api only) draws it. No game state on the render side.
add_library(vertical_slice_session STATIC src/session.cpp src/event_feed.cpp)
target_include_directories(vertical_slice_session PUBLIC src)
target_link_libraries(vertical_slice_session PUBLIC eng::base eng::sim PRIVATE eng::world eng::ecs eng::rules5e)
_engine_warnings(vertical_slice_session)

add_library(vertical_slice_view STATIC src/slice_view.cpp)
target_include_directories(vertical_slice_view PUBLIC src)
target_link_libraries(vertical_slice_view PUBLIC eng::iso2d eng::render_api)
_engine_warnings(vertical_slice_view)

if(ENGINE_BUILD_TESTS)
    add_executable(vertical_slice_tests tests/vertical_slice_tests.cpp tests/event_feed_tests.cpp)
    target_link_libraries(vertical_slice_tests PRIVATE vertical_slice_session vertical_slice_view eng::render_null
                          eng::sim eng::world eng::ecs
                          doctest::doctest_with_main)
    target_compile_definitions(vertical_slice_tests PRIVATE
        SLICE_PACK="${PROJECT_SOURCE_DIR}/content/packs/sample.vertical_slice")
    _engine_warnings(vertical_slice_tests)
    add_test(NAME vertical_slice_tests COMMAND vertical_slice_tests)
endif()

if(ENGINE_WITH_SDL AND ENGINE_BACKEND_SDLGPU)
    add_executable(vertical_slice src/main.cpp)
    target_link_libraries(vertical_slice PRIVATE vertical_slice_session vertical_slice_view
                          eng::platform eng::render_sdlgpu)
    target_compile_definitions(vertical_slice PRIVATE
        SLICE_DEFAULT_PACK="${PROJECT_SOURCE_DIR}/content/packs/sample.vertical_slice")
    _engine_warnings(vertical_slice)
endif()
