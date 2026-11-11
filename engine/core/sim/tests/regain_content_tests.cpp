// Regain content (CONTENT_CONVENTIONS §55): monster@5 trait `regain` fills Combatant::regain.
// At the start of its turn, while it has at least 1 hit point, it regains that many, capped at maximum.
// At 0 it is dead. Not troll regen. The static amount is not hashed.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <utility>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const Json kRegain = {{"kind", "regain"}, {"amount", 10}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects, int hp) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "regain_" + std::to_string(i++)}, {"name", "Regeneration"}, {"description", "test"},
                               {"effect", std::move(e)}});
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) { return lib.add(m, "monster", "", version); }
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
Json swing_action(const char* id, int flat) {
    Json dmg = Json::object();
    dmg["flat"] = flat;
    dmg["type"] = "slashing";
    Json act = Json::object();
    act["id"] = id;
    act["name"] = id;
    act["kind"] = "melee_weapon_attack";
    act["to_hit"] = 30;
    act["reach"] = 5;
    act["targets"] = 1;
    act["damage"] = Json::array({dmg});
    return act;
}
Json striker_monster() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.striker";
    m["hit_points"]["average"] = 40;
    m["actions"] = Json::array({swing_action("nick", 39), swing_action("slay", 20)});
    return m;
}
int attack_index(const rules5e::Combatant& c, const char* id) {
    for (int i = 0; i < int(c.attacks.size()); ++i)
        if (c.attacks[std::size_t(i)].id == id) return i;
    return -1;
}
bool last_attack_hit(const Combat& c) {
    int flags = -1;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::AttackResolved) flags = e.rules.c;
    return flags >= 0 && (flags & 1) != 0;
}
void swing(Combat& c, CombatantId who, CombatantId target, int index) {
    to_actor(c, who);
    Command cmd;
    cmd.request = {who, ActionKind::Attack, target, uint8_t(index)};
    REQUIRE(c.submit(cmd).ok);
    REQUIRE(last_attack_hit(c));
}
}  // namespace

TEST_CASE("regain loader: amount 10, identical second declaration, refusals") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json same = {{"kind", "regain"}, {"amount", 10}};
    REQUIRE_FALSE(add(lib, creature("t.guard", {kRegain, same}, 80)));
    const auto* t = lib.find("t.guard");
    REQUIRE(t);
    CHECK(t->base.regain == 10);
    CHECK(t->base.regeneration.amount == 0);

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}, 80), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e_amt", {{"kind", "regain"}, {"amount", 0}}).find("amount must be an integer 1..32767") != std::string::npos);
    CHECK(err("t.e_key", {{"kind", "regain"}, {"amount", 10}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e_ver", kRegain, 4).find("needs a monster@5 file") != std::string::npos);
    const auto diff = add(lib, creature("t.e_diff", {kRegain, {{"kind", "regain"}, {"amount", 8}}}, 80));
    REQUIRE(diff);
    CHECK(diff->find("a creature has one regain") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.regain_pc";
    ch["features"].push_back({{"id", "regeneration"}, {"name", "Regeneration"}, {"description", "test"}, {"effect", kRegain}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc = pcs.add(ch, "character");
    REQUIRE(pc);
    CHECK(pc->find("only valid on monsters") != std::string::npos);
}

TEST_CASE("regain: a loaded guardian heals on its turn only, and the amount is not hashed") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kRegain}, 40)));
    REQUIRE_FALSE(add(lib, creature("t.plain", {}, 40)));
    REQUIRE_FALSE(add(lib, creature("t.mid", {}, 40)));
    auto ch = arena({{"t.guard", Vec2i{5, 5}}, {"t.plain", Vec2i{6, 5}}, {"t.mid", Vec2i{7, 5}}});
    ch.spawns[1].overrides_json = R"({"team":"party"})";
    const CombatantId guard = ch.spawns[0].stable_id;
    const CombatantId other = ch.spawns[1].stable_id;
    const CombatantId mid = ch.spawns[2].stable_id;
    // All three are Dex 14. Pin other, then mid, then the guardian, so the non-heal check always runs.
    uint64_t seed = 0;
    for (uint64_t s = 1; s < 200 && seed == 0; ++s) {
        ecs::Registry trial_reg;
        auto trial = Combat::create(trial_reg, ch, lib, s);
        REQUIRE(trial);
        const auto& order = trial->state().order;
        if (order.size() == 3 && order[0].id == other && order[1].id == mid && order[2].id == guard) seed = s;
    }
    REQUIRE(seed != 0);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, seed);
    REQUIRE(c);
    auto* g = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), guard));
    REQUIRE(g->regain == 10);
    g->hp = 40;
    const uint64_t full = c->hash();
    g->regain = 0;
    CHECK(c->hash() == full);
    g->regain = 10;
    CHECK(c->hash() == full);

    REQUIRE(rules5e::current_actor(c->state()) == other);
    g->hp = 12;
    end_turn(*c);
    REQUIRE(rules5e::current_actor(c->state()) == mid);
    REQUIRE(rules5e::current_actor(c->state()) != guard);
    CHECK(rules5e::find(c->state(), guard)->hp == 12);
    end_turn(*c);
    REQUIRE(rules5e::current_actor(c->state()) == guard);
    CHECK(rules5e::find(c->state(), guard)->hp == 22);
    CHECK_FALSE(rules5e::find(c->state(), guard)->dead);
    CHECK_FALSE(rules5e::find(c->state(), guard)->regen_down);
}

TEST_CASE("regain: log replay reproduces a heal from 1 hit point and no revive at 0") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", {kRegain}, 40)));
    REQUIRE_FALSE(add(lib, creature("t.witness", {}, 40)));
    REQUIRE_FALSE(add(lib, striker_monster()));
    constexpr uint64_t kSeed = 11;
    auto ch = arena({{"t.striker", Vec2i{5, 5}}, {"t.guard", Vec2i{6, 5}}, {"t.witness", Vec2i{7, 5}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, kSeed);
    REQUIRE(c);
    const CombatantId striker = ch.spawns[0].stable_id;
    const CombatantId guard = ch.spawns[1].stable_id;
    const int nick = attack_index(*rules5e::find(c->state(), striker), "nick");
    const int slay = attack_index(*rules5e::find(c->state(), striker), "slay");
    REQUIRE(nick >= 0);
    REQUIRE(slay >= 0);

    // 40 hit points, flat 39 leaves 1. That turn start heals. A later drop to 0 does not.
    swing(*c, striker, guard, nick);
    CHECK(rules5e::find(c->state(), guard)->hp == 1);
    CHECK_FALSE(rules5e::find(c->state(), guard)->dead);
    to_actor(*c, guard);
    CHECK(rules5e::find(c->state(), guard)->hp == 11);
    CHECK_FALSE(rules5e::find(c->state(), guard)->dead);

    const auto healed_log = c->log();
    const uint64_t healed_hash = c->hash();
    const uint64_t healed_log_hash = c->log_hash();
    {
        ecs::Registry again;
        auto rp = replay(again, ch, lib, kSeed, healed_log);
        REQUIRE(rp);
        CHECK(rp->hash() == healed_hash);
        CHECK(rp->log_hash() == healed_log_hash);
        CHECK(rules5e::find(rp->state(), guard)->hp == 11);
        CHECK_FALSE(rules5e::find(rp->state(), guard)->dead);
    }

    swing(*c, striker, guard, slay);
    CHECK(rules5e::find(c->state(), guard)->hp == 0);
    CHECK(rules5e::find(c->state(), guard)->dead);
    CHECK_FALSE(rules5e::find(c->state(), guard)->regen_down);
    CHECK_FALSE(c->ended());
    for (int i = 0; i < 4; ++i) {
        REQUIRE(rules5e::current_actor(c->state()) != guard);
        end_turn(*c);
        CHECK(rules5e::find(c->state(), guard)->hp == 0);
        CHECK(rules5e::find(c->state(), guard)->dead);
    }
    REQUIRE(rules5e::current_actor(c->state()) != guard);

    ecs::Registry again;
    auto rp = replay(again, ch, lib, kSeed, c->log());
    REQUIRE(rp);
    CHECK(rp->hash() == c->hash());
    CHECK(rp->log_hash() == c->log_hash());
    CHECK(rules5e::find(rp->state(), guard)->hp == 0);
    CHECK(rules5e::find(rp->state(), guard)->dead);
    CHECK_FALSE(rules5e::find(rp->state(), guard)->regen_down);
}
