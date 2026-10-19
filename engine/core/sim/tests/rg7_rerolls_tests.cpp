// R-G7, sim side: loader kinds reroll_ones (Halfling Lucky), great_weapon_fighting, evasion; Cutting Words as a roll_attack
// subtract_roll reaction spending a resources[] pool; in_range answers AttackRolled (60 ft, footprint-aware, sight); the
// default decider takes it; the AI sets ActionRequest::two_handed_grip for a versatile weapon held in two hands.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e/weapon.hpp"
#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::ReactionTrigger;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json fighter(const std::string& id, std::vector<Json> effects, int declared_schema = 4) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");   // longsword (versatile), shield
    c["$schema"] = "character@" + std::to_string(declared_schema);
    c["id"] = id;
    for (std::size_t i = 0; i < effects.size(); ++i)
        c["features"].push_back({{"id", "s" + std::to_string(i)}, {"name", "S"}, {"description", "x"}, {"effect", effects[i]}});
    return c;
}
Json no_shield(Json c) {
    c["equipment"] = Json::array({"chain_mail", "longsword"});
    c["armor_class"]["note"] = "chain mail";
    return c;
}
Json cutting_words(Json usage = Json({{"kind", "pool"}, {"pool", "bardic_inspiration"}}), const char* window = "roll_attack",
                   Json effect = Json({{"kind", "subtract_roll"}, {"dice", "1d6"}})) {
    Json r = {{"id", "cutting_words"}, {"name", "Cutting Words"}, {"window", {{"kind", window}}}, {"effect", effect}};
    if (!usage.is_null()) r["usage"] = usage;
    return r;
}
Json bard(const std::string& id, std::vector<Json> reactions, int pool_max = 3, const char* size = "small") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["size"] = size;
    m["hit_points"]["average"] = 400;
    m["resources"] = Json::array({{{"id", "bardic_inspiration"}, {"name", "Bardic Inspiration"}, {"max", pool_max}, {"recharge", "long_rest"}}});
    m["reactions"] = Json::array();
    for (const Json& r : reactions) m["reactions"].push_back(r);
    return m;
}
Json plain_goblin(const std::string& id) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    return m;
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns, std::vector<Vec2i> sight_walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : sight_walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight;
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
ContentLibrary lib_with(const std::vector<Json>& monsters, const std::vector<Json>& characters = {}) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : monsters) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    for (const Json& d : characters) { const auto e = lib.add(d, "character"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
class NoTarget final : public IActorPolicy {
public:
    std::optional<CombatantId> choose_target(const Combat&, CombatantId) override { return std::nullopt; }
};
// The fighter attacks `tgt` once per round for up to `rounds` rounds; returns how many AttackRolled windows it met (each is
// answered with the default decider) and records the reactor of the first.
int attack_rounds(Combat& c, CombatantId f, CombatantId tgt, int rounds, CombatantId* reactor = nullptr) {
    NoTarget policy;
    int windows = 0;
    for (int t = 0; t < rounds; ++t) {
        to_actor(c, f);
        Command a;
        a.request = {f, ActionKind::Attack, tgt};
        a.request.attack_index = 0;
        auto r = c.submit(a);
        REQUIRE_MESSAGE(r.ok, r.error);
        while (r.needs) {
            if (r.needs->trigger == ReactionTrigger::AttackRolled) {
                if (windows == 0 && reactor) *reactor = r.needs->reactor;
                ++windows;
                const ReactionDecision d = policy.decide_reaction(c, *r.needs);
                CHECK(d.react);           // the default decider treats AttackRolled like the other windows: option 0
                CHECK(d.option == 0);
            }
            r = c.decide(policy.decide_reaction(c, *r.needs));
            REQUIRE_MESSAGE(r.ok, r.error);
        }
        Command e;
        e.request = {f, ActionKind::EndTurn};
        if (rules5e::current_actor(c.state()) == f) REQUIRE(c.submit(e).ok);
    }
    return windows;
}
}  // namespace

TEST_CASE("R-G7 loader: reroll_ones (Lucky), great_weapon_fighting, evasion; defaults unchanged") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(fighter("t.plain", {}), "character"));
    const auto& p = lib.find("t.plain")->base;
    CHECK(p.reroll_ones == 0);
    CHECK_FALSE(p.great_weapon_fighting);
    CHECK_FALSE(p.evasion);
    REQUIRE_FALSE(lib.add(fighter("t.lucky", {{{"kind", "reroll_ones"}}, {{"kind", "great_weapon_fighting"}}, {{"kind", "evasion"}}}),
                          "character"));
    const auto& l = lib.find("t.lucky")->base;
    CHECK(l.reroll_ones == 7);   // Halfling Lucky: attack rolls, ability checks, saving throws
    CHECK(l.great_weapon_fighting);
    CHECK(l.evasion);
    CHECK(lib.find("t.lucky")->unsupported_features.empty());
    REQUIRE_FALSE(lib.add(fighter("t.saves", {{{"kind", "reroll_ones"}, {"rolls", Json::array({"save"})}},
                                              {{"kind", "reroll_ones"}, {"rolls", Json::array({"attack"})}}}), "character"));
    CHECK(lib.find("t.saves")->base.reroll_ones == 5);   // several declarations OR together
    // open kinds: live at any declared version (like R-G5's); bad shapes are load errors
    REQUIRE_FALSE(lib.add(fighter("t.old", {{{"kind", "evasion"}}}, 3), "character", "", 3));
    CHECK(lib.find("t.old")->base.evasion);
    CHECK(lib.add(fighter("t.bad1", {{{"kind", "reroll_ones"}, {"rolls", Json::array({"damage"})}}}), "character"));
    CHECK(lib.add(fighter("t.bad2", {{{"kind", "reroll_ones"}, {"rolls", Json::array()}}}), "character"));
    // the monster side takes them too (Rogue / Monk Evasion on an NPC)
    Json m = plain_goblin("t.evasive");
    m["traits"] = Json::array({{{"id", "evasion"}, {"name", "Evasion"}, {"description", "x"}, {"effect", {{"kind", "evasion"}}}}});
    REQUIRE_FALSE(lib.add(m, "monster"));
    CHECK(lib.find("t.evasive")->base.evasion);
}

TEST_CASE("R-G7 loader: weapon grip bits and the v0 wield model") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const auto* f = lib.find(kFighter);
    REQUIRE(f);
    REQUIRE(f->base.attacks.size() == 2);
    CHECK(f->base.attacks[0].properties == rules5e::kWeaponVersatile);   // longsword
    CHECK(f->base.attacks[1].properties == rules5e::kWeaponTwoHanded);   // light crossbow: ammunition / loading stay off
    CHECK(f->two_handed_grip == 0);                                      // carries a shield
    REQUIRE_FALSE(lib.add(no_shield(fighter("t.twohands", {})), "character"));
    CHECK(lib.find("t.twohands")->two_handed_grip == 1u);                // the longsword only (melee, versatile)
    Json note_only = no_shield(fighter("t.note", {}));
    note_only["armor_class"]["note"] = "Chain Mail, Shield";
    REQUIRE_FALSE(lib.add(note_only, "character"));
    CHECK(lib.find("t.note")->two_handed_grip == 0);
    // character@4 equipment entries: {"item": "<namespaced item id>"}
    Json v4 = no_shield(fighter("t.v4shield", {}));
    v4["equipment"] = Json::array({{{"item", "srd51.item.chain_mail"}}, {{"item", "srd51.item.shield"}}, {{"item", "srd51.item.longsword"}}});
    REQUIRE_FALSE(lib.add(v4, "character"));
    CHECK(lib.find("t.v4shield")->two_handed_grip == 0);
    Json v4bare = no_shield(fighter("t.v4bare", {}));
    v4bare["equipment"] = Json::array({{{"item", "srd51.item.chain_mail"}}, {{"item", "srd51.item.longsword"}}});
    REQUIRE_FALSE(lib.add(v4bare, "character"));
    CHECK(lib.find("t.v4bare")->two_handed_grip == 1u);
}

TEST_CASE("R-G7 loader: Cutting Words compiles to Reaction{AttackRolled, SubtractRoll, dice, pool}") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(bard("t.bard", {cutting_words()}), "monster"));
    const auto* a = lib.find("t.bard");
    REQUIRE(a->base.reactions.size() == 1);
    const auto& x = a->base.reactions[0];
    CHECK(x.window == ReactionTrigger::AttackRolled);
    CHECK(x.kind == rules5e::ReactionEffectKind::SubtractRoll);
    CHECK(x.dice.count == 1);
    CHECK(x.dice.sides == 6);
    CHECK(x.pool == rules5e::spell_id_of("bardic_inspiration"));
    CHECK(x.pool_cost == 1);
    CHECK(x.id == rules5e::spell_id_of("t.bard#cutting_words"));
    REQUIRE(a->resources.size() == 1);   // registered by Combat::create (register_pool)
    // no usage: an ungated reaction; a usage that is not a pool is listed, not compiled
    REQUIRE_FALSE(lib.add(bard("t.free", {cutting_words(Json())}), "monster"));
    CHECK(lib.find("t.free")->base.reactions.at(0).pool == 0);
    REQUIRE_FALSE(lib.add(bard("t.perrest", {cutting_words({{"kind", "per_rest"}, {"uses", 1}})}), "monster"));
    CHECK(lib.find("t.perrest")->base.reactions.empty());
    CHECK(lib.find("t.perrest")->unsupported_features.size() == 1);
    // load errors
    for (const Json& bad : std::vector<Json>{cutting_words({{"kind", "pool"}, {"pool", "nope"}}),
                                             cutting_words(Json(), "damaged"),
                                             cutting_words(Json(), "roll_attack", {{"kind", "impose_disadvantage"}}),
                                             cutting_words(Json(), "roll_attack", {{"kind", "subtract_roll"}})}) {
        const auto e = lib.add(bard("t.bad", {bad}), "monster");
        REQUIRE(e);
        CHECK(e->find("t.bad") != std::string::npos);
    }
}

TEST_CASE("R-G7 sim: AttackRolled opens for an enemy within 60 ft that sees the attacker; the pool gates and pays") {
    static_assert(rules5e::kReactionTriggerCount == 8);
    CHECK(rules5e::Combatant{}.reaction_policy.size() == 8);   // AttackRolled (7) has its own policy entry
    auto lib = lib_with({bard("t.bard", {cutting_words()}, 1), bard("t.bigbard", {cutting_words()}, 3, "large"), plain_goblin("t.tgt")});
    const Vec2i fpos{6, 20}, tpos{5, 20};
    {   // 12 tiles (60 ft): the window opens for the bard, which pays its one use; then the pool is empty
        auto ch = arena({{"t.bard", Vec2i{6, 8}}, {"t.tgt", tpos}, {kFighter, fpos}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId b = ch.spawns[0].stable_id, t = ch.spawns[1].stable_id, f = ch.spawns[2].stable_id;
        const uint32_t pid = rules5e::spell_id_of("bardic_inspiration");
        REQUIRE(rules5e::find_pool(c->state(), b, pid));
        CHECK(rules5e::find_pool(c->state(), b, pid)->cur == 1);
        CombatantId who = 0;
        CHECK(attack_rounds(*c, f, t, 6, &who) == 1);   // once: the pool (max 1) is spent
        CHECK(who == b);
        CHECK(rules5e::find_pool(c->state(), b, pid)->cur == 0);
        // the log replays to the same state
        ecs::Registry reg2;
        auto rp = replay(reg2, ch, lib, 4, c->log());
        REQUIRE(rp);
        CHECK(rp->hash() == c->hash());
        CHECK(rp->log_hash() == c->log_hash());
    }
    {   // 13 tiles (65 ft): out of range
        auto ch = arena({{"t.bard", Vec2i{6, 7}}, {"t.tgt", tpos}, {kFighter, fpos}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        CHECK(attack_rounds(*c, ch.spawns[2].stable_id, ch.spawns[1].stable_id, 4) == 0);
    }
    {   // footprint-aware: a Large bard anchored 13 tiles away has its nearest tile 12 away
        auto ch = arena({{"t.bigbard", Vec2i{6, 7}}, {"t.tgt", tpos}, {kFighter, fpos}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        CombatantId who = 0;
        CHECK(attack_rounds(*c, ch.spawns[2].stable_id, ch.spawns[1].stable_id, 2, &who) >= 1);
        CHECK(who == ch.spawns[0].stable_id);
    }
    {   // in range but a sight-blocking wall stands between the bard and the attacker
        auto ch = arena({{"t.bard", Vec2i{6, 15}}, {"t.tgt", tpos}, {kFighter, fpos}}, {Vec2i{6, 17}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        CHECK_FALSE(c->line_of_sight(ch.spawns[0].stable_id, ch.spawns[2].stable_id));
        CHECK(attack_rounds(*c, ch.spawns[2].stable_id, ch.spawns[1].stable_id, 4) == 0);
    }
}

TEST_CASE("R-G7 AI: two_handed_grip for a versatile weapon held in two hands; Great Weapon Fighting attacks resolve") {
    Json twf = no_shield(fighter("t.twf", {}));   // two light daggers: a two-weapon bonus option, so no two-handed grip
    for (const char* id : {"dagger_a", "dagger_b"})
        twf["actions"].push_back({{"id", id}, {"name", "Dagger"}, {"kind", "melee_weapon_attack"}, {"to_hit", 5}, {"reach", 5},
                                  {"targets", 1}, {"properties", Json::array({"light", "finesse"})},
                                  {"damage", Json::array({{{"dice", "1d4+3"}, {"type", "piercing"}}})}});
    auto lib = lib_with({plain_goblin("t.tgt")}, {no_shield(fighter("t.gwf", {{{"kind", "great_weapon_fighting"}}})), twf});
    REQUIRE_FALSE(lib.find("t.twf")->base.bonus_attacks.empty());
    CHECK(lib.find("t.twf")->two_handed_grip == 1u);   // the loader bit; the AI drops it for a two-weapon fighter
    Json v4s = no_shield(fighter("t.v4s", {{{"kind", "great_weapon_fighting"}}}));
    v4s["equipment"] = Json::array({{{"item", "srd51.item.chain_mail"}}, {{"item", "srd51.item.shield"}}, {{"item", "srd51.item.longsword"}}});
    REQUIRE_FALSE(lib.add(v4s, "character"));
    for (const char* who : {kFighter, "t.gwf", "t.twf", "t.v4s"}) {
        auto ch = arena({{"t.tgt", Vec2i{5, 5}}, {who, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId t = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        to_actor(*c, f);
        const Command cmd = next_command(*c, t);
        REQUIRE(cmd.kind == CommandKind::Rules);
        REQUIRE(cmd.request.kind == ActionKind::Attack);
        CHECK(cmd.request.attack_index == 0);
        CHECK(cmd.request.two_handed_grip == (std::string(who) == "t.gwf"));   // the shield-bearer holds it in one hand
        auto r = c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(c->log().back().step.request.two_handed_grip == cmd.request.two_handed_grip);
    }
}

TEST_CASE("R-G7 log_hash: two_handed_grip folds (0xB6) only when set") {
    auto lib = lib_with({plain_goblin("t.tgt")}, {no_shield(fighter("t.gwf", {{{"kind", "great_weapon_fighting"}}}))});
    auto ch = arena({{"t.tgt", Vec2i{5, 5}}, {"t.gwf", Vec2i{6, 5}}});
    uint64_t h[2] = {0, 0};
    for (int grip = 0; grip < 2; ++grip) {
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId t = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        to_actor(*c, f);
        const uint64_t before = c->log_hash();
        Command a;
        a.request = {f, ActionKind::Attack, t};
        a.request.attack_index = 0;
        a.request.two_handed_grip = grip == 1;
        REQUIRE(c->submit(a).ok);
        CHECK(c->log_hash() != before);
        h[grip] = c->log_hash();
        ecs::Registry reg2;
        auto rp = replay(reg2, ch, lib, 7, c->log());
        REQUIRE(rp);
        CHECK(rp->log_hash() == c->log_hash());
    }
    CHECK(h[0] != h[1]);
}
