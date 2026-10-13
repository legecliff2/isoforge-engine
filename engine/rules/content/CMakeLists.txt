# rules5e_content: spell@1 JSON -> rules5e::SpellDef (keeps rules5e itself free of JSON/engine deps).
engine_add_module(rules5e_content
    SOURCES     src/spell_loader.cpp
    PUBLIC_DEPS rules5e assets
    TESTS       tests/spell_loader_tests.cpp)
if(TARGET rules5e_content_tests)
    target_compile_definitions(rules5e_content_tests PRIVATE ENGINE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
endif()
