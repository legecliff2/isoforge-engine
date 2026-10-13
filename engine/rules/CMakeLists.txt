# rules5e: pure SRD 5.1 rules. Depends only on rng, detmath, std (§2.1).
engine_add_module(rules5e
    SOURCES     src/dice.cpp src/d20.cpp src/initiative.cpp src/attack.cpp src/damage.cpp
    PUBLIC_DEPS rng detmath
    TESTS       tests/dice_tests.cpp tests/attack_tests.cpp tests/initiative_tests.cpp
                tests/damage_tests.cpp tests/golden_tests.cpp
    DETERMINISTIC)
