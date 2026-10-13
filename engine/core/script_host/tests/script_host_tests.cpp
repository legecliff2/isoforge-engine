#include <doctest/doctest.h>

#include "scratch_dir.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "script_host/script_host.hpp"

using namespace eng;
using namespace eng::script;

namespace {
const std::filesystem::path kPack = std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/sample.vertical_slice";
const std::string kAi = "scripts/ai/skirmisher.luau";

ScriptId must_load(ScriptHost& h, const char* src) {
    ScriptError e;
    auto id = h.load_script("test", src, &e);
    REQUIRE_MESSAGE(id, e.message);
    return *id;
}

// Goblin (team 1) vs three foes: B weakest but far, C nearest, D in between.
struct Scene {
    ecs::Registry reg;
    ecs::Entity gob, b, c, d;
    Scene() {
        gob = reg.create(100); reg.emplace(gob, Team{1}); reg.emplace(gob, Health{7, 7}); reg.emplace(gob, TilePos{10, 10});
        b = reg.create(30);    reg.emplace(b, Team{0});   reg.emplace(b, Health{2, 12}); reg.emplace(b, TilePos{20, 20});
        c = reg.create(20);    reg.emplace(c, Team{0});   reg.emplace(c, Health{9, 12}); reg.emplace(c, TilePos{11, 10});
        d = reg.create(10);    reg.emplace(d, Team{0});   reg.emplace(d, Health{9, 12}); reg.emplace(d, TilePos{14, 10});
        ecs::Entity ally = reg.create(5); reg.emplace(ally, Team{1}); reg.emplace(ally, Health{1, 7});
        ecs::Entity dead = reg.create(1); reg.emplace(dead, Team{0}); reg.emplace(dead, Health{0, 7});
    }
};

std::vector<uint64_t> decisions(uint64_t seed, int turns) {
    ScriptHost host(1);
    auto pack = load_pack_scripts(host, kPack);
    REQUIRE_MESSAGE(pack.ok, (pack.errors.empty() ? "" : pack.errors[0]));
    Scene s;
    rng::Pcg32 r(seed, 7);
    std::vector<uint64_t> out;
    for (int i = 0; i < turns; ++i) {
        CallResult cr = host.call_entity_fn(pack.scripts.at(kAi), "choose_target", s.reg, s.gob, r);
        REQUIRE_MESSAGE(cr.ok(), cr.error.message);
        REQUIRE(cr.target);
        out.push_back(*s.reg.stable_id(*cr.target));
    }
    return out;
}
} // namespace

TEST_CASE("api level 1 definitions are available and only level 1 exists") {
    CHECK(api_definitions(1).find("declare extern type SimContext with") != std::string::npos);
    CHECK(api_definitions(0).empty());
    CHECK(api_definitions(2).empty());
}

TEST_CASE("sample pack: strict typecheck passes and the goblin AI picks a target") {
    ScriptHost host(1);
    PackScripts p = load_pack_scripts(host, kPack);
    REQUIRE_MESSAGE(p.ok, (p.errors.empty() ? "" : p.errors[0]));
    CHECK(p.api_level == 1);
    REQUIRE(p.scripts.count(kAi) == 1);
    // goblin.json behavior.ai (migrated from monster@1 presentation.ai) binds the script.
    REQUIRE(p.ai_by_archetype.count("srd51.monster.goblin") == 1);
    CHECK(p.ai_by_archetype.at("srd51.monster.goblin") == p.scripts.at(kAi));
    Scene s;
    rng::Pcg32 r(1, 7);
    CallResult cr = host.call_entity_fn(p.scripts.at(kAi), "choose_target", s.reg, s.gob, r);
    REQUIRE_MESSAGE(cr.ok(), cr.error.message);
    REQUIRE(cr.target);
    const uint64_t id = *s.reg.stable_id(*cr.target);
    CHECK((id == 30 || id == 20));   // weakest (B) or nearest (C); never ally/dead/D
    CHECK(host.log().size() == 1);
    CHECK(cr.safepoints > 0);
}

TEST_CASE("determinism: same seed => same decisions; different seeds exercise both branches") {
    const auto a = decisions(42, 64), b = decisions(42, 64);
    CHECK(a == b);
    std::set<uint64_t> picks(a.begin(), a.end());
    CHECK(picks == std::set<uint64_t>{20, 30});   // both nearest and weakest occur over 64 turns
    CHECK(decisions(43, 64) != a);                // a different seed takes a different path
    // Golden: the first 16 decisions for seed 42 are fixed by Pcg32 + the script.
    std::string g;
    for (int i = 0; i < 16; ++i) g += (a[std::size_t(i)] == 20 ? 'N' : 'W');
    CHECK(g == "WNWWNNWNWNWWWWWN");
}

TEST_CASE("sandbox: removed globals, frozen builtins, isolated script globals") {
    ScriptHost host(1);
    ScriptId probe = must_load(host, R"(
        local r = {}
        r.os = os == nil
        r.loadstring = loadstring == nil
        r.getfenv = getfenv == nil and setfenv == nil
        r.require = require == nil
        r.random = math.random == nil and math.randomseed == nil and math.noise == nil
        r.libm = math.sin == nil and math.sqrt == nil and math.pow == nil
        r.debug = debug.traceback ~= nil and debug.info == nil
        r.gc_count = type(collectgarbage("count")) == "number"
        r.gc_collect = not pcall(collectgarbage, "collect")
        r.frozen = not pcall(function() string.upper = nil end)
        r.globals_frozen = not pcall(function() rawset(_G, "x", 1) end)
        mine = 1   -- private global in this script's sandbox thread
        return { check = function(ctx: any, self: any) for k, v in r do if not v then error(k) end end return nil end }
    )");
    ScriptId other = must_load(host, R"(
        return { check = function(ctx: any, self: any) if mine ~= nil then error("leak") end return nil end }
    )");
    Scene s;
    rng::Pcg32 r(1, 1);
    auto c1 = host.call_entity_fn(probe, "check", s.reg, s.gob, r);
    CHECK_MESSAGE(c1.ok(), c1.error.message);
    auto c2 = host.call_entity_fn(other, "check", s.reg, s.gob, r);
    CHECK_MESSAGE(c2.ok(), c2.error.message);
}

TEST_CASE("cpu budget: infinite loops are terminated, even under pcall; host stays usable") {
    ScriptHost host(1, Limits{10'000, 4u << 20, 32u << 20});
    ScriptId spin = must_load(host, "return { f = function(c: any, s: any) while true do end end }");
    ScriptId sneaky = must_load(host, R"(return { f = function(c: any, s: any)
        while true do pcall(function() while true do end end) end end })");
    ScriptId fine = must_load(host, "return { f = function(c: any, s: any) local n = 0 for i = 1, 100 do n += i end return nil end }");
    Scene s;
    rng::Pcg32 r(1, 1);
    auto a = host.call_entity_fn(spin, "f", s.reg, s.gob, r);
    CHECK(a.error.kind == ErrorKind::CpuBudget);
    CHECK(a.safepoints == 10'001);   // deterministic count
    auto b = host.call_entity_fn(sneaky, "f", s.reg, s.gob, r);
    CHECK(b.error.kind == ErrorKind::CpuBudget);
    auto c = host.call_entity_fn(fine, "f", s.reg, s.gob, r);
    CHECK_MESSAGE(c.ok(), c.error.message);
    // Same budget hit at the same safepoint again.
    CHECK(host.call_entity_fn(spin, "f", s.reg, s.gob, r).safepoints == 10'001);
    // Top-level chunk code is budgeted too.
    ScriptError e;
    CHECK_FALSE(host.load_script("loop", "while true do end return {}", &e));
    CHECK(e.kind == ErrorKind::CpuBudget);
}

TEST_CASE("memory limit: allocation bombs are terminated, even under pcall; memory is reclaimed") {
    ScriptHost host(1, Limits{100'000'000, 1u << 20, 32u << 20});
    ScriptId bomb = must_load(host, R"(return { f = function(c: any, s: any)
        local t = {} local i = 0 while true do i += 1 t[i] = string.rep("x", 1024) .. i end end })");
    ScriptId sneaky = must_load(host, R"(return { f = function(c: any, s: any)
        local keep = {}
        for k = 1, 1000 do pcall(function() local t = {} for i = 1, 1e9 do t[i] = string.rep("y", 1024) .. i end end) keep[k] = k end
        return nil end })");
    ScriptId fine = must_load(host, "return { f = function(c: any, s: any) local t = {} for i = 1, 100 do t[i] = i end return nil end }");
    Scene s;
    rng::Pcg32 r(1, 1);
    const std::size_t before = host.bytes_in_use();
    auto a = host.call_entity_fn(bomb, "f", s.reg, s.gob, r);
    CHECK(a.error.kind == ErrorKind::Memory);
    auto b = host.call_entity_fn(sneaky, "f", s.reg, s.gob, r);
    CHECK(b.error.kind == ErrorKind::Memory);
    auto c = host.call_entity_fn(fine, "f", s.reg, s.gob, r);
    CHECK_MESSAGE(c.ok(), c.error.message);
    // Garbage from the killed calls is collectable: repeated bombs keep failing on the
    // per-call cap (not the 32 MiB context cap) and normal scripts keep running.
    for (int i = 0; i < 8; ++i) CHECK(host.call_entity_fn(bomb, "f", s.reg, s.gob, r).error.kind == ErrorKind::Memory);
    CHECK(host.call_entity_fn(fine, "f", s.reg, s.gob, r).ok());
    CHECK(host.bytes_in_use() < before + (16u << 20));
}

TEST_CASE("handles: invalid outside their call; bad returns rejected") {
    ScriptHost host(1);
    ScriptId keep = must_load(host, R"(
        local saved: any = nil
        return { f = function(ctx: any, self: any)
            if saved ~= nil then return saved:hp() end
            saved = self
            return nil
        end, g = function(ctx: any, self: any) return 5 end })");
    Scene s;
    rng::Pcg32 r(1, 1);
    CHECK(host.call_entity_fn(keep, "f", s.reg, s.gob, r).ok());
    auto second = host.call_entity_fn(keep, "f", s.reg, s.gob, r);
    CHECK(second.error.kind == ErrorKind::Runtime);
    CHECK(second.error.message.find("outside its call") != std::string::npos);
    CHECK(host.call_entity_fn(keep, "g", s.reg, s.gob, r).error.kind == ErrorKind::BadResult);
    CHECK(host.call_entity_fn(keep, "missing", s.reg, s.gob, r).error.kind == ErrorKind::BadResult);
    s.reg.destroy(s.c);
    ScriptId stale = must_load(host, "return { f = function(ctx: any, self: any) return #ctx:entities() end }");
    CHECK(host.call_entity_fn(stale, "f", s.reg, s.gob, r).error.kind == ErrorKind::BadResult);
}

TEST_CASE("typecheck: strict errors with file:line, overrides nonstrict, rejects sandboxed symbols") {
    auto errs = typecheck(1, {{"scripts/a.luau", "--!nonstrict\nlocal x: number = \"no\"\nreturn {}\n"}});
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].rfind("scripts/a.luau:2: ", 0) == 0);
    CHECK_FALSE(typecheck(1, {{"c.luau", "return { t = os.time() }"}}).empty());
    CHECK_FALSE(typecheck(1, {{"d.luau", "return { r = math.random(1, 6) }"}}).empty());
    CHECK_FALSE(typecheck(1, {{"e.luau", "return { s = math.sqrt(4) }"}}).empty());
    CHECK_FALSE(typecheck(1, {{"f.luau", "return { f = function(ctx: SimContext) return ctx.rng:below(\"x\") end }"}}).empty());
    CHECK_FALSE(typecheck(1, {{"g.luau", "return { f = function(ctx: SimContext) return ctx:teleport() end }"}}).empty());
    CHECK(typecheck(1, {{"h.luau", "return { f = function(ctx: SimContext): number return ctx.rng:d(20) + math.abs(-1) end }"}}).empty());
    CHECK_FALSE(typecheck(2, {{"i.luau", "return {}"}}).empty());   // unknown api level
}

TEST_CASE("pack load fails on type errors and api_level mismatch") {
    const ScratchDir scratch("script_pack_test");
    const auto& dir = scratch.path;
    std::filesystem::create_directories(dir / "scripts");
    std::ofstream(dir / "manifest.json") << R"({"$schema":"manifest@1","api_level":1})";
    std::ofstream(dir / "scripts/bad.luau") << "local n: number = 'x'\nreturn {}\n";
    ScriptHost host(1);
    auto p = load_pack_scripts(host, dir);
    CHECK_FALSE(p.ok);
    REQUIRE_FALSE(p.errors.empty());
    CHECK(p.errors[0].rfind("scripts/bad.luau:1: ", 0) == 0);
    std::ofstream(dir / "manifest.json") << R"({"$schema":"manifest@1","api_level":2})";
    auto q = load_pack_scripts(host, dir);
    CHECK_FALSE(q.ok);
    CHECK(q.errors.at(0).find("api_level 2") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("embedded level 1 definitions are byte-identical to the frozen source file") {
    std::ifstream in(std::filesystem::path(SCRIPT_API_DIR) / "engine.level1.d.luau", std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    CHECK(api_definitions(1) == ss.str());
}

TEST_CASE("check_pack_scripts (tools/script_check) on the sample pack") {
    auto r = check_pack_scripts(kPack);
    CHECK(r.errors.empty());
    CHECK(r.scripts >= 1);
    CHECK(r.api_level == 1);
}
