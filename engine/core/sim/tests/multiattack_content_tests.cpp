// Declared multiattack from content (monster@3 `multiattack` effect) and sim: the loader fills
// Combatant::multiattacks, Combat::multiattack_option finds options by id, Command passes
// ActionRequest::multiattack through, and the AI opens a melee option in reach (else an all-ranged
// one it can shoot) and finishes the sequence.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include "assets/content_migrations.hpp"
#include "sample_set.hpp"
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
    CHECK(r.c->multiattack_option(r.beast_id, "rend").value_or(0) == 1);
    CHECK(r.c->multiattack_option(r.beast_id, "snipe").value_or(0) == 2);
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
    // One AI turn for the beast (in melee reach: 'rend' is opened; the bow option 'snipe' is not).
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

namespace {
world::Chunk arena_teams(const std::vector<std::pair<std::string, Vec2i>>& spawns, const std::vector<Vec2i>& walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls)
        c.layers[2][std::size_t(world::cell_index(w.x, w.y))].flags =
            world::tile_flags::kBlocksMove | world::tile_flags::kBlocksSight;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)),
                            std::string("{\"team\":\"") + (i == 0 ? "a" : "b") + "\"}"});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
uint8_t attack_named(const rules5e::Combatant& k, const char* id) {
    for (uint8_t i = 0; i < k.attacks.size(); ++i)
        if (k.attacks[i].id == id) return i;
    FAIL("no attack " << id);
    return 0;
}
// Drive `who` until it has made `want` attack commands (steps in between are kept).
struct Opened {
    std::vector<Command> cmds;
    std::vector<rules5e::ActionRequest> attacks;
};
Opened play_open(Combat& c, CombatantId who, CombatantId foe, int want) {
    for (int i = 0; i < 4 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        REQUIRE_MESSAGE(r.ok, r.error);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
    Opened o;
    for (int guard = 0; guard < 12 && int(o.attacks.size()) < want && rules5e::current_actor(c.state()) == who; ++guard) {
        Command cmd = next_command(c, foe);
        o.cmds.push_back(cmd);
        if (cmd.kind == CommandKind::Rules && cmd.request.kind == ActionKind::Attack && cmd.request.special == 0)
            o.attacks.push_back(cmd.request);
        auto r = c.submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
    }
    return o;
}
Json opener_beast() {
    Json m = beast("t.beast", {options(Json::array({opt("mix", {"bite", "bow"}), opt("rend", {"bite", "claw", "claw"}),
                                                     opt("snipe", {"bow", "bow"})}))},
                   "monster@5");
    m["actions"].push_back({{"id", "blink"}, {"name", "Blink"}, {"kind", "teleport"}, {"feet", 40}, {"with_attack", "bite"},
                            {"description", "teleport, then bite"}});
    m["hit_points"]["average"] = 80;
    return m;
}
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    std::optional<Combat> c;
    CombatantId self = 0, foe = 0;
    Fight(const ContentLibrary& lib, std::vector<std::pair<std::string, Vec2i>> spawns, std::vector<Vec2i> walls, uint64_t seed) {
        chunk = arena_teams(spawns, walls);
        c = Combat::create(reg, chunk, lib, seed);
        REQUIRE(c);
        self = chunk.spawns[0].stable_id;
        foe = chunk.spawns[1].stable_id;
    }
    Opened open(int want) { return play_open(*c, self, foe, want); }
};
} // namespace

TEST_CASE("multiattack sim: opener prefers melee in reach and ranged when it can shoot") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(opener_beast(), "monster"));
    Json foe = beast("t.foe", {});
    foe["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(foe, "monster"));
    const auto& mas = lib.find("t.beast")->base.multiattacks;
    REQUIRE(mas.size() == 4);
    CHECK(mas[0].id == "mix");
    CHECK(mas[3].after_action != 0);

    auto run = [&](int gap, const std::vector<Vec2i>& walls, int want) {
        Fight f(lib, {{"t.beast", {4, 4}}, {"t.foe", {4 + gap, 4}}}, walls, 3);
        const uint8_t bow = attack_named(*rules5e::find(f.c->state(), f.self), "bow");
        const uint8_t bite = attack_named(*rules5e::find(f.c->state(), f.self), "bite");
        return std::tuple{bow, bite, f.open(want)};
    };

    {
        auto [bow, bite, o] = run(1, {}, 2);
        REQUIRE(o.attacks.size() == 2);
        CHECK(o.cmds[0].kind == CommandKind::Rules);
        CHECK(o.attacks[0].multiattack == 2);   // rend, not mix (1) and not the teleport follow-up (4)
        CHECK(o.attacks[0].attack_index == bite);
        CHECK(o.attacks[1].multiattack == 0);
        CHECK(o.attacks[1].attack_index != bow);
    }
    {
        auto [bow, bite, o] = run(4, {}, 2);   // 20 ft: out of melee, inside the bow's normal range, close enough to walk in
        REQUIRE(o.cmds.size() >= 2);
        CHECK(o.cmds[0].request.kind == ActionKind::Attack);
        REQUIRE(o.attacks.size() == 2);
        CHECK(o.attacks[0].multiattack == 3);   // snipe
        CHECK(o.attacks[0].attack_index == bow);
        CHECK(o.attacks[1].multiattack == 0);
        CHECK(o.attacks[1].attack_index == bow);
        CHECK(o.attacks[0].attack_index != bite);
    }
    {
        auto [bow, bite, o] = run(20, {}, 2);   // 100 ft: beyond normal 80, inside long 320; can_shoot, not the reach test
        REQUIRE_FALSE(o.cmds.empty());
        CHECK(o.cmds[0].kind == CommandKind::Rules);
        CHECK(o.cmds[0].request.kind == ActionKind::Attack);
        CHECK(o.cmds[0].request.multiattack == 3);
        CHECK(o.cmds[0].request.attack_index == bow);
        REQUIRE(o.attacks.size() == 2);
        CHECK(o.attacks[1].attack_index == bow);
        (void)bite;
    }
    {
        auto [bow, bite, o] = run(6, {{7, 4}}, 1);   // wall on the ray: cannot shoot, so the plain move path
        REQUIRE_FALSE(o.cmds.empty());
        CHECK(o.cmds[0].kind == CommandKind::Step);
        (void)bow;
        (void)bite;
    }
}

TEST_CASE("multiattack sim: scout and wight open ranged multiattack out of melee") {
    ContentLibrary lib;
    auto errs = lib.load(sample_set());
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? "" : errs[0]));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["id"] = "t.foe";
    foe["hit_points"]["average"] = 400;
    REQUIRE_FALSE(lib.add(foe, "monster"));

    {
        Fight f(lib, {{"srd51.monster.scout", {0, 4}}, {"t.foe", {1, 4}}}, {}, 5);
        const auto& k = *rules5e::find(f.c->state(), f.self);
        Opened o = f.open(2);
        REQUIRE(o.attacks.size() == 2);
        const auto melee = f.c->multiattack_option(f.self, "melee");
        REQUIRE(melee);
        CHECK(o.attacks[0].multiattack == *melee);
        CHECK(o.attacks[0].attack_index == attack_named(k, "shortsword"));
        CHECK(o.attacks[1].multiattack == 0);
        CHECK(o.attacks[1].attack_index == attack_named(k, "shortsword"));
    }
    {
        Fight f(lib, {{"srd51.monster.scout", {0, 4}}, {"t.foe", {6, 4}}}, {}, 5);   // 30 ft: out of reach, can still walk in
        const auto& k = *rules5e::find(f.c->state(), f.self);
        Opened o = f.open(2);
        REQUIRE_FALSE(o.cmds.empty());
        CHECK(o.cmds[0].request.kind == ActionKind::Attack);
        REQUIRE(o.attacks.size() == 2);
        const auto ranged = f.c->multiattack_option(f.self, "ranged");
        REQUIRE(ranged);
        CHECK(o.attacks[0].multiattack == *ranged);
        CHECK(o.attacks[0].attack_index == attack_named(k, "longbow"));
        CHECK(o.attacks[1].multiattack == 0);
        CHECK(o.attacks[1].attack_index == attack_named(k, "longbow"));
        CHECK(f.c->state().budget.sequence == 0);
    }
    {
        Fight f(lib, {{"srd51.monster.scout", {0, 4}}, {"t.foe", {31, 4}}}, {}, 5);   // 155 ft: long range only
        const auto& k = *rules5e::find(f.c->state(), f.self);
        CHECK(f.c->distance_tiles(f.self, f.foe) > 30);
        CHECK(f.c->can_shoot(f.self, attack_named(k, "longbow"), f.foe));
        Opened o = f.open(1);
        REQUIRE_FALSE(o.cmds.empty());
        CHECK(o.cmds[0].kind == CommandKind::Rules);
        CHECK(o.cmds[0].request.kind == ActionKind::Attack);
        const auto ranged = f.c->multiattack_option(f.self, "ranged");
        REQUIRE(ranged);
        CHECK(o.cmds[0].request.multiattack == *ranged);
        CHECK(o.cmds[0].request.attack_index == attack_named(k, "longbow"));
    }
    {
        Fight f(lib, {{"srd51.monster.wight", {0, 4}}, {"t.foe", {6, 4}}}, {}, 5);
        const auto& k = *rules5e::find(f.c->state(), f.self);
        Opened o = f.open(2);
        REQUIRE_FALSE(o.cmds.empty());
        CHECK(o.cmds[0].request.kind == ActionKind::Attack);
        REQUIRE(o.attacks.size() == 2);
        const auto bow = f.c->multiattack_option(f.self, "longbow_x2");
        REQUIRE(bow);
        CHECK(o.attacks[0].multiattack == *bow);
        CHECK(o.attacks[0].attack_index == attack_named(k, "longbow"));
        CHECK(o.attacks[1].multiattack == 0);
        CHECK(o.attacks[1].attack_index == attack_named(k, "longbow"));
    }
    {
        Fight f(lib, {{"srd51.monster.wight", {0, 4}}, {"t.foe", {1, 4}}}, {}, 5);
        const auto& k = *rules5e::find(f.c->state(), f.self);
        Opened o = f.open(1);
        REQUIRE_FALSE(o.attacks.empty());
        const auto swords = f.c->multiattack_option(f.self, "longsword_x2");
        REQUIRE(swords);
        CHECK(o.attacks[0].multiattack == *swords);
        CHECK(o.attacks[0].attack_index == attack_named(k, "longsword"));
    }
}
