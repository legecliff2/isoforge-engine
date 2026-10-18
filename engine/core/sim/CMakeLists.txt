# sim: turn manager, ECS <-> rules5e adapters, grid movement, reaction windows (§2.1, §5.3
# "Sim responsibilities"). Deterministic (§4): integer math only, stable iteration.
engine_add_module(sim
    SOURCES     src/events.cpp src/grid.cpp src/content.cpp src/features.cpp src/combat.cpp src/driver.cpp
    PUBLIC_DEPS ecs rules5e rules5e_content world assets
    TESTS       tests/sim_tests.cpp tests/events_tests.cpp tests/features_tests.cpp tests/tactics_tests.cpp tests/compat_fl1_tests.cpp tests/compat_fl1_goblin_tests.cpp tests/compat_defenses_tests.cpp tests/defenses_tests.cpp tests/casting_tests.cpp tests/footprint_tests.cpp tests/ally_near_tests.cpp tests/qualifiers_tests.cpp tests/multiattack_content_tests.cpp tests/world_tests.cpp tests/packset_tests.cpp)
if(TARGET sim_tests)
    target_compile_definitions(sim_tests PRIVATE ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
endif()
