# sim: turn manager, ECS <-> rules5e adapters, grid movement, reaction windows (§2.1, §5.3
# "Sim responsibilities"). Deterministic (§4): integer math only, stable iteration.
engine_add_module(sim
    SOURCES     src/events.cpp src/grid.cpp src/content.cpp src/features.cpp src/combat.cpp src/driver.cpp src/rest.cpp
    PUBLIC_DEPS ecs rules5e rules5e_content world assets
    TESTS       tests/sim_tests.cpp tests/events_tests.cpp tests/features_tests.cpp tests/tactics_tests.cpp tests/compat_fl1_tests.cpp tests/compat_fl1_goblin_tests.cpp tests/compat_defenses_tests.cpp tests/defenses_tests.cpp tests/casting_tests.cpp tests/footprint_tests.cpp tests/ally_near_tests.cpp tests/save_actions_tests.cpp tests/area_template_tests.cpp tests/innate_tests.cpp tests/trigger_content_tests.cpp tests/trigger_9b_tests.cpp tests/reaction_content_tests.cpp tests/modifier_content_tests.cpp tests/qualifiers_tests.cpp tests/multiattack_content_tests.cpp tests/hash_fold_tests.cpp tests/world_tests.cpp tests/packset_tests.cpp tests/resource_pool_tests.cpp tests/static_feature_tests.cpp tests/grapple_tests.cpp tests/bonus_attack_content_tests.cpp tests/rg7_rerolls_tests.cpp tests/spell_kinds_content_tests.cpp tests/surprise_rest_tests.cpp tests/leftovers_content_tests.cpp tests/pack_tactics_tests.cpp tests/material_costs_tests.cpp tests/prone_rider_tests.cpp tests/ai_reckless_standup_tests.cpp tests/metamagic_content_tests.cpp tests/slot_recovery_content_tests.cpp)
if(TARGET sim_tests)
    target_compile_definitions(sim_tests PRIVATE ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
endif()
