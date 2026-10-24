# game/vertical_slice: M1 slice viewer. world + sim (Engine Core) feed a plain-data ViewModel;
# SliceView (iso2d/render_api only) draws it. No game state on the render side.
add_library(vertical_slice_session STATIC src/session.cpp src/event_feed.cpp src/debug_equip.cpp src/debug_cover.cpp)
target_include_directories(vertical_slice_session PUBLIC src)
target_link_libraries(vertical_slice_session PUBLIC eng::base eng::sim eng::iso2d PRIVATE eng::world eng::ecs eng::rules5e eng::assets)
_engine_warnings(vertical_slice_session)

add_library(vertical_slice_view STATIC src/slice_view.cpp src/equip_visuals.cpp)
target_include_directories(vertical_slice_view PUBLIC src)
target_link_libraries(vertical_slice_view PUBLIC eng::iso2d eng::render_api)
_engine_warnings(vertical_slice_view)

if(ENGINE_BUILD_TESTS)
    add_executable(vertical_slice_tests tests/vertical_slice_tests.cpp tests/event_feed_tests.cpp tests/light_view_tests.cpp tests/equipped_light_tests.cpp tests/overlay_determinism_tests.cpp tests/equip_visuals_tests.cpp tests/debug_equip_tests.cpp tests/faerie_fire_outline_tests.cpp tests/unseen_feedback_tests.cpp tests/cover_debug_tests.cpp)
    target_link_libraries(vertical_slice_tests PRIVATE vertical_slice_session vertical_slice_view eng::render_null
                          eng::sim eng::world eng::ecs eng::assets
                          doctest::doctest_with_main)
    target_compile_definitions(vertical_slice_tests PRIVATE
        SLICE_PACK_ROOT="${PROJECT_SOURCE_DIR}/content/packs" SLICE_PACK="sample.vertical_slice")
    _engine_warnings(vertical_slice_tests)
    add_test(NAME vertical_slice_tests COMMAND vertical_slice_tests)

    # Overlay perf benchmark (fog + many glows). Built with the tests, never run by ctest (wall-clock, machine dependent).
    add_executable(vertical_slice_overlay_bench tests/overlay_bench.cpp)
    target_link_libraries(vertical_slice_overlay_bench PRIVATE vertical_slice_view eng::render_null)
    _engine_warnings(vertical_slice_overlay_bench)

    # Offscreen snapshot of the equipment scene (CPU rasteriser behind IRenderDevice). Built with the tests, never run by ctest.
    add_executable(vertical_slice_equip_snapshot tests/equip_snapshot.cpp)
    target_link_libraries(vertical_slice_equip_snapshot PRIVATE vertical_slice_view)
    _engine_warnings(vertical_slice_equip_snapshot)
endif()

if(ENGINE_WITH_SDL AND ENGINE_BACKEND_SDLGPU)
    add_executable(vertical_slice src/main.cpp)
    target_link_libraries(vertical_slice PRIVATE vertical_slice_session vertical_slice_view
                          eng::platform eng::render_sdlgpu)
    target_compile_definitions(vertical_slice PRIVATE
        SLICE_DEFAULT_PACK_ROOT="${PROJECT_SOURCE_DIR}/content/packs" SLICE_DEFAULT_PACK="sample.vertical_slice")
    _engine_warnings(vertical_slice)
endif()
