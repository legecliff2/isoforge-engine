# rules5e: pure SRD 5.1 rules. Depends only on rng, detmath, std (§2.1).
engine_add_module(rules5e
    SOURCES     src/dice.cpp src/d20.cpp src/initiative.cpp src/attack.cpp src/damage.cpp
                src/conditions.cpp src/saves.cpp src/encounter.cpp
                src/checks.cpp src/healing.cpp src/spells.cpp src/armor.cpp src/rest.cpp src/font_of_magic.cpp
    PUBLIC_DEPS rng detmath
    TESTS       tests/dice_tests.cpp tests/attack_tests.cpp tests/initiative_tests.cpp
                tests/damage_tests.cpp tests/golden_tests.cpp
                tests/conditions_tests.cpp tests/saves_tests.cpp tests/encounter_tests.cpp
                tests/healing_tests.cpp tests/actions_tests.cpp tests/reactions_tests.cpp tests/async_tests.cpp
                tests/ranged_features_tests.cpp tests/spells_tests.cpp tests/multi_damage_tests.cpp tests/qualifiers_tests.cpp tests/multiattack_tests.cpp tests/weapon_property_tests.cpp tests/roll_traits_tests.cpp tests/regeneration_tests.cpp tests/spell_effects_tests.cpp tests/foundation_tests.cpp tests/save_action_tests.cpp tests/innate_tests.cpp tests/timed_effects_tests.cpp tests/triggers_tests.cpp tests/trigger_events_tests.cpp tests/trigger_rider_tests.cpp tests/reactions_ext_tests.cpp tests/modifiers_tests.cpp tests/pools_tests.cpp tests/armor_tests.cpp tests/modifiers_followup_tests.cpp tests/bonus_attacks_tests.cpp tests/grapple_tests.cpp tests/static_features_tests.cpp tests/roll_windows_tests.cpp tests/spell_kinds_tests.cpp tests/spell_kinds2_tests.cpp tests/per_cast_options_tests.cpp tests/rest_surprise_tests.cpp tests/small_leftovers_tests.cpp tests/forced_movement_tests.cpp tests/zones_tests.cpp tests/zones2_tests.cpp tests/gaze_tests.cpp tests/transforms_tests.cpp tests/summons_tests.cpp tests/spiritual_weapon_tests.cpp tests/font_of_magic_tests.cpp tests/squeeze_tests.cpp tests/cover_tests.cpp tests/contain_tests.cpp tests/move_modes_tests.cpp tests/spell_riders_tests.cpp tests/control_tests.cpp tests/vision_tests.cpp tests/class_hooks_tests.cpp
    DETERMINISTIC)

add_subdirectory(content)
