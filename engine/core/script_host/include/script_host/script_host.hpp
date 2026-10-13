#pragma once
// Luau scripting host v0 (ARCHITECTURE.md §3.3, D-005).
//
//  - One lua_State per ScriptHost (= one pack context, here "sim"). Safe libs only; os,
//    loadstring, getfenv/setfenv, require, libm-backed math.*, math.random/randomseed/noise
//    are removed, debug is reduced to traceback, collectgarbage only accepts "count".
//    Engine globals are installed, then luaL_sandbox() freezes the global environment.
//  - Each script gets its own luaL_sandboxthread() thread (private globals that fall back
//    to the frozen ones).
//  - CPU budget: deterministic count of VM safepoints (call/return/loop back-edge interrupts;
//    GC interrupts are not counted). No wall clock.
//  - Memory: custom lua_Alloc with a hard per-context cap plus a per-call net-allocation cap.
//  - Over-budget scripts are terminated: the error is re-raised at every following safepoint
//    and allocation, so pcall inside the script cannot swallow it.
//  - Native codegen (Luau.CodeGen) is not linked.
//  - Engine objects are tagged userdata handles: entities carry (index, generation, call
//    epoch) and are checked on every use; nothing is valid across calls.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ecs/registry.hpp"
#include "rng/rng.hpp"

namespace eng::script {

// Script-visible components for API level 1. (They belong in `sim` once it exists;
// kept here so v0 has no upward dependency.)
struct Health  { int32_t hp = 0, hp_max = 0; };
struct Team    { uint8_t team = 0; };
struct TilePos { int32_t x = 0, y = 0; };

inline constexpr uint32_t kMaxApiLevel = 1;

struct Limits {
    uint64_t max_safepoints_per_call = 200'000;     // deterministic CPU budget
    std::size_t max_bytes_per_call = 4u << 20;      // net allocation growth per call/load
    std::size_t max_bytes_total = 32u << 20;        // hard cap for the whole context
};

enum class ErrorKind : uint8_t { None, Type, Compile, Runtime, CpuBudget, Memory, BadResult };

struct ScriptError {
    ErrorKind kind = ErrorKind::None;
    std::string message;
};

struct ScriptId { uint32_t value = 0xFFFF'FFFFu; auto operator<=>(const ScriptId&) const = default; };

// Strict-mode type check against engine.level<api_level>.d.luau. `--!nonstrict` /
// `--!nocheck` headers are overridden. Returns "name:line: message" per error (empty = ok).
std::vector<std::string> typecheck(uint32_t api_level, const std::map<std::string, std::string>& sources);

// The frozen definitions source for an API level (empty if unknown).
std::string api_definitions(uint32_t api_level);

struct CallResult {
    ScriptError error;                    // kind None on success
    std::optional<ecs::Entity> target;    // returned EntityHandle, or nullopt for nil
    uint64_t safepoints = 0;
    bool ok() const noexcept { return error.kind == ErrorKind::None; }
};

class ScriptHost {
public:
    explicit ScriptHost(uint32_t api_level = 1, Limits limits = {});
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // Compiles and runs a module chunk in its own sandbox thread. The chunk must return a
    // table; its functions can then be called with call_entity_fn. Does NOT typecheck (use
    // load_pack or typecheck()).
    std::optional<ScriptId> load_script(std::string_view name, std::string_view source, ScriptError* err = nullptr);

    // Calls module[fn](ctx, self) where ctx is a SimContext over `reg` with ctx.rng = `rng`.
    // The function must return an EntityHandle or nil.
    CallResult call_entity_fn(ScriptId id, std::string_view fn, ecs::Registry& reg, ecs::Entity self, rng::Pcg32& rng);

    const std::vector<std::string>& log() const noexcept { return log_; }
    std::size_t bytes_in_use() const noexcept;
    uint32_t api_level() const noexcept { return api_level_; }

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
    uint32_t api_level_;
    std::vector<std::string> log_;
};

// Pack load (§3.3): reads manifest.json (api_level), strict-typechecks every
// scripts/**/*.luau in sorted path order (any type error fails the load), then loads each.
struct PackScripts {
    bool ok = false;
    std::vector<std::string> errors;
    std::map<std::string, ScriptId> scripts;   // pack-relative path -> id
    // Monster archetype id -> its behavior.ai script (monster@2; monster@1 presentation.ai is
    // migrated). Monsters are the manifest "contents" .json files whose $schema is monster@N.
    std::map<std::string, ScriptId> ai_by_archetype;
    uint32_t api_level = 0;
};
PackScripts load_pack_scripts(ScriptHost& host, const std::filesystem::path& pack_dir);
// Typecheck only (no VM), as used by tools/script_check.
struct PackCheck {
    std::vector<std::string> errors;
    std::size_t scripts = 0;
    uint32_t api_level = 0;
};
PackCheck check_pack_scripts(const std::filesystem::path& pack_dir);
// Reads only the manifest api_level (nullopt + message on failure).
std::optional<uint32_t> read_pack_api_level(const std::filesystem::path& pack_dir, std::string* err = nullptr);

} // namespace eng::script
