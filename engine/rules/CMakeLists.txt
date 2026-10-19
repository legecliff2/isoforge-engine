# rules5e: pure SRD 5.1 rules. Depends only on rng, detmath, std (§2.1).
engine_add_module(rules5e
    SOURCES     src/dice.cpp src/d20.cpp src/initiative.cpp src/attack.cpp src/damage.cpp
                src/conditions.cpp src/saves.cpp src/encounter.cpp
                src/checks.cpp src/healing.cpp src/spells.cpp
    PUBLIC_DEPS rng detmath
    TESTS       tests/dice_tests.cpp tests/attack_tests.cpp tests/initiative_tests.cpp
                tests/damage_tests.cpp tests/golden_tests.cpp
                tests/conditions_tests.cpp tests/saves_tests.cpp tests/encounter_tests.cpp
                tests/healing_tests.cpp tests/actions_tests.cpp tests/reactions_tests.cpp tests/async_tests.cpp
                tests/ranged_features_tests.cpp tests/spells_tests.cpp tests/multi_damage_tests.cpp tests/qualifiers_tests.cpp tests/multiattack_tests.cpp tests/weapon_property_tests.cpp tests/roll_traits_tests.cpp tests/regeneration_tests.cpp tests/spell_effects_tests.cpp tests/foundation_tests.cpp tests/save_action_tests.cpp tests/innate_tests.cpp tests/timed_effects_tests.cpp tests/triggers_tests.cpp tests/trigger_events_tests.cpp tests/trigger_rider_tests.cpp tests/reactions_ext_tests.cpp tests/modifiers_tests.cpp
    DETERMINISTIC)

add_subdirectory(content)
