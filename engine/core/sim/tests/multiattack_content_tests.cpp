// Declared multiattack from content (monster@3 `multiattack` effect) and sim: the loader fills
// Combatant::multiattacks, Combat::multiattack_option finds options by id, Command passes
// ActionRequest::multiattack through, and the AI opens a melee option and finishes the sequence.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json melee(const char* id, const char* dice) {
    return {{"id", id}, {"name", id}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5},
            {"damage", Json::array({{{"dice", dice}, {"type", "slashing"}}})}};
}
// A beast with actions bite, claw, claw2 (melee) and a bow, plus the given trait effects.
Json beast(const std::string& id, const std::vector<Json>& effects, const char* schema = "monster@3") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = id;
    m["hit_points"]["average"] = 80;
    m["traits"] = Json::array();
    int i = 0;
    for (const Json& e : effects)
        m["traits"].push_back({{"id", "t" + std::to_string(i++)}, {"name", "T"}, {"description", "x"}, {"effect", e}});
    m["actions"] = Json::array({melee("bite", "1d8+2"), melee("claw", "1d4+2"),
                                {{"id", "bow"}, {"name", "Bow"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 4},
                                 {"range", Json::array({80, 320})},
                                 {"damage", Json::array({{{"dice", "1d6"}, {"type", "piercing"}}})}}});
    return m;
}
Json options(Json opts) { return {{"kind", "multiattack"}, {"options", std::move(opts)}}; }
Json opt(const char* id, std::vector<std::string> attacks) { return {{"id", id}, {"attacks", Json(attacks)}}; }

world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns, const std::vector<const char*>& teams) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)),
                            std::string("{\"team\":\"") + teams[i] + "\"}"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
std::string load_error(const Json& effect) {
    ContentLibrary lib;
    auto e = lib.add(beast("t.bad", {effect}), "monster");
    REQUIRE(e);
    return *e;
}
} // namespace

TEST_CASE("multiattack content: options load as attack indices, ids and order preserved") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(beast("t.dragon", {options(Json::array({opt("rend", {"bite", "claw", "claw"}), opt("snipe", {"bow"})}))}), "monster"));
    const auto& mas = lib.find("t.dragon")->base.multiattacks;
    REQUIRE(mas.size() == 2);
    CHECK(mas[0].id == "rend");
    CHECK(mas[0].attacks == std::vector<uint8_t>{0, 1, 1});
    CHECK(mas[1].id == "snipe");
    CHECK(mas[1].attacks == std::vector<uint8_t>{2});
    // Shorthand: one option named "multiattack".
    REQUIRE_FALSE(lib.add(beast("t.owl", {Json{{"kind", "multiattack"}, {"attacks", Json::array({"claw", "bite"})}}}), "monster"));
    const auto& o = lib.find("t.owl")->base.multiattacks;
    REQUIRE(o.size() == 1);
    CHECK(o[0].id == "multiattack");
    CHECK(o[0].attacks == std::vector<uint8_t>{1, 0});
    // A copy of the Combatant (what Combat spawns) still names its options.
    rules5e::Combatant copy = lib.find("t.dragon")->base;
    CHECK(copy.multiattacks[0].id == "rend");
    // No effect: nothing declared, as before.
    REQUIRE_FALSE(lib.add(beast("t.plain", {}), "monster"));
    CHECK(lib.find("t.plain")->base.multiattacks.empty());
}

TEST_CASE("multiattack content: malformed effects are rejected") {
    CHECK(load_error(Json{{"kind", "multiattack"}}).find("needs options") != std::string::npos);
    CHECK(load_error(options(Json::array())).find("non-empty") != std::string::npos);
    CHECK(load_error(options(Json::array({opt("x", {"bite", "tail"})}))).find("unknown attack action 'tail'") != std::string::npos);
    CHECK(load_error(options(Json::array({opt("x", {"bite"}), opt("x", {"claw"})}))).find("duplicate") != std::string::npos);
    CHECK(load_error(options(Json::array({opt("x", {})}))).find("1..255") != std::string::npos);
    CHECK(load_error(options(Json::array({Json{{"attacks", Json::array({"bite"})}}}))).find("needs an id") != std::string::npos);
}

TEST_CASE("multiattack content: pre-@3 multiattack effect is inert") {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    auto r = reg.load(beast("t.old", {options(Json::array({opt("rend", {"bite", "claw"})}))}, "monster@2"));
    REQUIRE_MESSAGE(r.ok, r.error);
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(r.data, "monster"));
    CHECK(lib.find("t.old")->base.multiattacks.empty());
    CHECK(lib.find("t.old")->inert_features.size() == 1);
}

namespace {
struct Rig {
    ecs::Registry reg;
    world::Chunk chunk;
    std::optional<Combat> c;
    CombatantId beast_id = 0, foe_id = 0;
    Rig(const ContentLibrary& lib, uint64_t seed = 3, int gap = 1) {
        chunk = arena({{"t.beast", {4, 4}}, {"t.foe", {4 + gap, 4}}}, {"a", "b"});
        c = Combat::create(reg, chunk, lib, seed);
        REQUIRE(c);
        beast_id = chunk.spawns[0].stable_id;
        foe_id = chunk.spawns[1].stable_id;
        for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != beast_id; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
    }
    int attacks_by(CombatantId who) const {
        int n = 0;
        for (const auto& e : c->events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == who;
        return n;
    }
    SubmitResult attack(uint8_t index, uint8_t multi = 0) {
        Command a;
        a.request = {beast_id, ActionKind::Attack, foe_id};
        a.request.attack_index = index;
        a.request.multiattack = multi;
        auto r = c->submit(a);
        while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
};
ContentLibrary rig_lib() {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(beast("t.beast", {options(Json::array({opt("rend", {"bite", "claw", "claw"}), opt("snipe", {"bow"})}))}), "monster"));
    Json foe = beast("t.foe", {});
    foe["hit_points"]["average"] = 200;
    REQUIRE_FALSE(lib.add(foe, "monster"));
    return lib;
}
} // namespace

TEST_CASE("multiattack sim: option lookup by id, one action for the whole sequence, order enforced") {
    auto lib = rig_lib();
    Rig r(lib);
    CHECK(r.c->multiattack_option(r.beast_id, "rend") == std::optional<uint8_t>(1));
    CHECK(r.c->multiattack_option(r.beast_id, "snipe") == std::optional<uint8_t>(2));
    CHECK_FALSE(r.c->multiattack_option(r.beast_id, "nope"));
    CHECK_FALSE(r.c->multiattack_option(r.foe_id, "rend"));
    const uint8_t rend = *r.c->multiattack_option(r.beast_id, "rend");
    CHECK_FALSE(r.attack(1, rend).ok);   // the sequence starts with attack 0 (bite)
    auto a1 = r.attack(0, rend);
    REQUIRE_MESSAGE(a1.ok, a1.error);
    CHECK(r.c->state().budget.sequence == rend);
    CHECK_FALSE(r.c->state().budget.action);                 // the Attack action is spent once
    CHECK_FALSE(r.attack(0).ok);                              // next must be claw (index 1)
    auto a2 = r.attack(1);                                    // multiattack may stay 0 mid-sequence
    REQUIRE_MESSAGE(a2.ok, a2.error);
    auto a3 = r.attack(1, rend);
    REQUIRE_MESSAGE(a3.ok, a3.error);
    CHECK(r.attacks_by(r.beast_id) == 3);
    CHECK(r.c->state().budget.sequence == 0);                 // finished
    CHECK_FALSE(r.attack(0).ok);                              // no action left
    // The command log carries the option: replay is exact.
    ecs::Registry reg2;
    auto rep = replay(reg2, r.chunk, lib, 3, r.c->log());
    REQUIRE(rep);
    CHECK(rep->hash() == r.c->hash());
    CHECK(rep->log_hash() == r.c->log_hash());
}

TEST_CASE("multiattack sim: the AI opens the first melee option, finishes it, and plain creatures are unchanged") {
    auto lib = rig_lib();
    Rig r(lib, 3, 1);
    NearestEnemyPolicy pol;
    // One AI turn for the beast (a bow-only option 'snipe' is never picked by the AI, 'rend' is).
    const std::size_t before = r.c->events().size();
    while (rules5e::current_actor(r.c->state()) == r.beast_id) {
        const Command cmd = next_command(*r.c, r.foe_id);
        if (cmd.request.kind == ActionKind::Attack) CHECK(cmd.request.multiattack == (r.c->state().budget.attacks_remaining == 0 ? 1 : 0));
        auto res = r.c->submit(cmd);
        REQUIRE_MESSAGE(res.ok, res.error);
        while (res.needs) res = r.c->decide({res.needs->window_seq, false, 0});
    }
    int n = 0;
    for (std::size_t i = before; i < r.c->events().size(); ++i) {
        const auto& e = r.c->events()[i];
        n += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == r.beast_id;
    }
    CHECK(n == 3);   // bite, claw, claw
}

// Behaviour golden, pinned once: a multiattack beast against a tougher foe, AI-driven, with replay.
TEST_CASE("multiattack golden: a three-attack beast fights to the end, AI-driven") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(beast("t.beast", {options(Json::array({opt("rend", {"bite", "claw", "claw"})}))}), "monster"));
    Json foe = beast("t.foe", {});
    foe["hit_points"]["average"] = 60;
    REQUIRE_FALSE(lib.add(foe, "monster"));
    ecs::Registry reg;
    auto chunk = arena({{"t.beast", {4, 4}}, {"t.foe", {12, 6}}}, {"a", "b"});
    auto c = Combat::create(reg, chunk, lib, 91);
    REQUIRE(c);
    NearestEnemyPolicy pol;
    auto rr = run_to_end(*c, [&](CombatantId) { return &pol; }, 2000);
    REQUIRE_MESSAGE(rr.error.empty(), rr.error);
    ecs::Registry reg2;
    auto rep = replay(reg2, chunk, lib, 91, c->log());
    REQUIRE(rep);
    CHECK(rep->hash() == c->hash());
    const uint64_t eh = event_stream_hash(c->events()), sh = c->hash(), lh = c->log_hash();
    MESSAGE("multiattack golden: events=" << eh << " state=" << sh << " log=" << lh << " n=" << c->events().size());
    CHECK(eh == 9036601848900934098ull);
    CHECK(sh == 1792088800189987304ull);
    CHECK(lh == 6305423801162545634ull);
    CHECK(c->events().size() == 100u);
}
