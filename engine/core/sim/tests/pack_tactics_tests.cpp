// Pack Tactics (SRD 5.1: jackal, kobold, wolf, dire wolf; T&C-G7), sim side: the open loader kind `pack_tactics` fills the
// rules5e roll trait {Attack, Advantage, kWhenAllyNearTarget}; Combat supplies ActionRequest::ally_near_target (a creature
// of the attacker's team, not incapacitated, within 5 ft of the target). Static data: no hash tag, goldens unchanged.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e/traits.hpp"
#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json pack_trait(Json effect = Json({{"kind", "pack_tactics"}})) {
    return {{"id", "pack_tactics"},
            {"name", "Pack Tactics"},
            {"description", "Advantage on an attack roll against a creature if an ally that isn't incapacitated is within 5 ft of it."},
            {"effect", effect}};
}
// The sample goblin (scimitar first) as a monster@4 test creature, with or without Pack Tactics.
Json beast(const std::string& id, bool pack, const char* schema = "monster@4") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = schema;
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    if (pack) m["traits"].push_back(pack_trait());
    return m;
}
ContentLibrary lib_with(const std::vector<Json>& monsters) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");   // survives a long fight
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
rules5e::Combatant* poke(Combat& c, CombatantId id) {   // test-only state poke (to-hit / AC / HP), not the trait
    return const_cast<rules5e::Combatant*>(rules5e::find(c.state(), id));
}
bool has_pack_trait(const rules5e::Combatant& k) {
    for (const auto& t : k.roll_traits)
        if (t.roll == rules5e::TraitRoll::Attack && t.mode == rules5e::RollMode::Advantage && t.when == rules5e::kWhenAllyNearTarget &&
            t.target_conditions == 0 && t.target_types == 0)
            return true;
    return false;
}
}  // namespace

TEST_CASE("pack_tactics loader: fills the rules5e roll trait; absent by default; open kind at @3 and @4") {
    auto lib = lib_with({beast("t.plain", false), beast("t.pack", true), beast("t.pack3", true, "monster@3")});
    const auto* plain = lib.find("t.plain");
    REQUIRE(plain);
    CHECK(plain->base.roll_traits.empty());
    const auto* pk = lib.find("t.pack");
    REQUIRE(pk);
    REQUIRE(pk->base.roll_traits.size() == 1);
    const rules5e::RollTrait& t = pk->base.roll_traits[0];
    CHECK(t.roll == rules5e::TraitRoll::Attack);
    CHECK(t.mode == rules5e::RollMode::Advantage);
    CHECK(t.when == rules5e::kWhenAllyNearTarget);
    CHECK(t.target_conditions == 0);
    CHECK(t.target_types == 0);
    CHECK(pk->unsupported_features.empty());     // a registered kind, not "unsupported"
    CHECK(pk->base.bonus_action_grants == plain->base.bonus_action_grants);   // the goblin's other trait still loads
    CHECK(has_pack_trait(lib.find("t.pack3")->base));   // open kind: live at monster@3 too

    // a second declaration adds nothing; extra fields are ignored (open effect object)
    Json twice = beast("t.twice", true);
    twice["traits"].push_back(pack_trait(Json({{"kind", "pack_tactics"}, {"note", "ignored"}})));
    twice["traits"].back()["id"] = "pack_tactics_2";
    REQUIRE_FALSE(lib.add(twice, "monster"));
    CHECK(lib.find("t.twice")->base.roll_traits.size() == 1);

    // the Combatant built by Combat::create carries it
    auto ch = arena({{"t.pack", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    REQUIRE(c);
    CHECK(has_pack_trait(*rules5e::find(c->state(), ch.spawns[0].stable_id)));
    CHECK_FALSE(has_pack_trait(*rules5e::find(c->state(), ch.spawns[1].stable_id)));
}

TEST_CASE("pack_tactics combat: advantage only with a non-incapacitated ally within 5 ft of the target") {
    auto lib = lib_with({beast("t.pack", true), beast("t.plain", false)});
    // attacker at (6,5), fighter at (5,5); the ally at (4,5) is adjacent to the fighter, at (5,6) diagonal-adjacent, at (20,5) far
    auto modes = [&](const char* attacker, Vec2i ally) {
        auto ch = arena({{attacker, Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}, {"t.plain", ally}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 2);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        const auto* ak = rules5e::find(c->state(), a);
        const auto* fk = rules5e::find(c->state(), f);
        return rules5e::attack_trait_modes(*ak, *fk, c->ally_near_target(a, f), false);
    };
    CHECK(modes("t.pack", Vec2i{4, 5}).advantage);
    CHECK(modes("t.pack", Vec2i{5, 6}).advantage);
    CHECK_FALSE(modes("t.pack", Vec2i{20, 5}).advantage);
    CHECK_FALSE(modes("t.plain", Vec2i{4, 5}).advantage);   // no trait: the ally alone grants nothing
    CHECK_FALSE(modes("t.pack", Vec2i{4, 5}).disadvantage);

    {   // an incapacitated ally does not count
        auto ch = arena({{"t.pack", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}, {"t.plain", Vec2i{4, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 2);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        rules5e::add_condition(*poke(*c, ch.spawns[2].stable_id), rules5e::Condition::Incapacitated);
        CHECK_FALSE(rules5e::attack_trait_modes(*rules5e::find(c->state(), a), *rules5e::find(c->state(), f),
                                                c->ally_near_target(a, f), false).advantage);
    }

    // Through Combat::submit: the attacker needs a natural 20 (to-hit 0 vs AC 21). With an adjacent ally the roll has
    // advantage (~9.75% hits), alone it does not (5%), and a trait-less attacker with an ally does not either.
    auto hits = [&](const char* attacker, Vec2i ally) {
        int n = 0;
        for (uint64_t seed = 1; seed <= 400; ++seed) {
            auto ch = arena({{attacker, Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}, {"t.plain", ally}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, lib, seed);
            REQUIRE(c);
            const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
            poke(*c, a)->attacks[0].to_hit = 0;
            auto* fk = poke(*c, f);
            fk->ac = 21;
            fk->hp = fk->hp_max = 5000;
            to_actor(*c, a);
            Command cmd;
            cmd.request = {a, ActionKind::Attack, f, 0};
            REQUIRE(c->submit(cmd).ok);
            n += rules5e::find(c->state(), f)->hp < 5000;
        }
        return n;
    };
    const int flanked = hits("t.pack", Vec2i{4, 5}), alone = hits("t.pack", Vec2i{20, 5}), plain = hits("t.plain", Vec2i{4, 5});
    CHECK(flanked > alone + 8);   // 41 vs 20 at these seeds
    CHECK(flanked > plain + 8);
    CHECK(alone == plain);        // both roll one d20 per attack on the same seeds: identical outcomes
}

TEST_CASE("pack_tactics: a pack fight replays to the same hashes") {
    auto lib = lib_with({beast("t.pack", true), beast("t.plain", false)});
    for (const Vec2i ally : {Vec2i{4, 5}, Vec2i{20, 5}}) {
        auto ch = arena({{"t.pack", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}, {"t.pack", ally}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 11);
        REQUIRE(c);
        const CombatantId a = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        for (int round = 0; round < 4; ++round) {
            to_actor(*c, a);
            Command cmd;
            cmd.request = {a, ActionKind::Attack, f, 0};
            REQUIRE(c->submit(cmd).ok);
            Command e;
            e.request = {a, ActionKind::EndTurn};
            if (rules5e::current_actor(c->state()) == a) REQUIRE(c->submit(e).ok);
        }
        ecs::Registry reg2;
        auto rp = replay(reg2, ch, lib, 11, c->log());
        REQUIRE(rp);
        CHECK(rp->hash() == c->hash());
        CHECK(rp->log_hash() == c->log_hash());
    }
}
