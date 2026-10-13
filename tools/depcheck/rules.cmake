# Module dependency table — ARCHITECTURE.md §2.1 "May depend on" (+ modules added since,
# marked). An edge A -> B is legal iff B is reachable from A through this table
# ("downward only", §2.2 rule 1). The table itself must be acyclic (checked).
# Every engine_add_module() target must have an entry here.
set(ALLOW_base          "")
set(ALLOW_rng           "")
set(ALLOW_detmath       "")
set(ALLOW_rules5e       "rng;detmath")
set(ALLOW_platform      "base")
set(ALLOW_render_api    "base")
set(ALLOW_render_sdlgpu "render_api;platform")
set(ALLOW_render_null   "render_api")
set(ALLOW_iso2d         "render_api;base")
set(ALLOW_assets        "base;platform")
set(ALLOW_ecs           "base")
set(ALLOW_world         "assets;ecs;base;rng;detmath")
set(ALLOW_audio         "assets;platform")
set(ALLOW_sim           "ecs;rules5e;world")
set(ALLOW_script_host   "ecs;sim;world;assets;rng;detmath")
set(ALLOW_procgen       "detmath;rng;base")          # not in §2.1 table yet (procgen v0, #16)
set(ALLOW_procgen_world "world;procgen")             # glue: world <- procgen
set(ALLOW_rules5e_content "rules5e;assets")         # glue: rules5e <- assets (spell@1 loader, #23)
set(ALLOW_engine        "base;rng;detmath;rules5e;platform;render_api;render_sdlgpu;render_null;iso2d;assets;ecs;world;audio;sim;script_host;procgen;procgen_world")

# Third-party (non eng::) link targets each module may use directly. ext:* not listed = error.
# §2.2 rule 3: SDL only in platform and render_*; rule 2: rng/detmath/rules5e std only.
set(EXT_base          "eng_thirdparty_xxhash")
set(EXT_detmath       "eng_thirdparty_xxhash")   # StableHash = xxHash3-64 (§4.5), header-only
set(EXT_platform      "SDL3::SDL3-static;SDL3::SDL3")
set(EXT_render_sdlgpu "SDL3::SDL3-static;SDL3::SDL3")
set(EXT_assets        "nlohmann_json::nlohmann_json")
set(EXT_script_host   "Luau.VM;Luau.Compiler;Luau.Analysis")
set(EXT_world         "nlohmann_json::nlohmann_json")
set(EXT_sim           "nlohmann_json::nlohmann_json")
set(EXT_procgen_world "nlohmann_json::nlohmann_json")
# Allowed everywhere: toolchain/system libs.
set(EXT_ANY           "Threads::Threads")
