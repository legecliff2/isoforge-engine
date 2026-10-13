# Engine core v0 (ARCHITECTURE.md §2.1). Three modules, strictly downward:
#   ecs    -> base
#   assets -> base, nlohmann_json   (platform VFS not yet available; std::filesystem for now)
#   world  -> base, ecs, assets
#   sim    -> ecs, rules5e, world, assets
#   script_host -> sim, ecs, assets, rng, Luau (VM, Compiler, Analysis)
set(ENGINE_CORE_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_subdirectory(ecs)
add_subdirectory(assets)
add_subdirectory(world)
add_subdirectory(sim)
add_subdirectory(script_host)
