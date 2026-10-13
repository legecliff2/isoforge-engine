# ecs: entities, typed component storage, stable_id-ordered iteration (§4.5).
engine_add_module(ecs
    SOURCES     src/registry.cpp
    PUBLIC_DEPS base
    TESTS       tests/ecs_tests.cpp)
