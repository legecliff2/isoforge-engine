// R-G7, sim side: Lucky / Great Weapon Fighting / Evasion flags by feature id, two_handed / versatile bits, Cutting Words
// (roll_attack + subtract_roll + bardic inspiration pool) and the AttackRolled window in sim (in_range, default decider).
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

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json pc(const std::string& id, std::vector<const char*> feats) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = id;
    c["hit_points"]["average"] = 2000;
    for (const char* f : feats) c["features"].push_back({{"id", f}, {"name", f}, {"description", "x"}});
    return c;
}
Json bard(const std::string& id, Json usage_pool = "bardic_inspiration", const char* window = "roll_attack") {
    Json c = pc(id, {});
    c["resources"] = Json::array({{{"id", "bardic_inspiration"}, {"max", 3}, {"recharge", "long_rest"}}});
    c["reactions"] = Json::array({{{"id", "cutting_words"}, {"name", "Cutting Words"}, {"window", {{"kind", window}}},
                                   {"effect", {{"kind", "subtract_roll"}, {"dice", "1d8"}}},
                                   {"usage", {{"kind", "pool"}, {"pool", usage_pool}}}}});
    return c;
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
}  // namespace

TEST_CASE("roll windows: feature flags, weapon bits, Cutting Words compile; errors") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(pc("t.flags", {"lucky", "great_weapon_fighting", "evasion"}), "character"));
    const auto& f = lib.find("t.flags")->base;
    CHECK(f.reroll_ones == 7);
    CHECK(f.great_weapon_fighting);
    CHECK(f.evasion);
    CHECK((f.attacks[0].properties & rules5e::kWeaponVersatile) != 0);   // longsword
    CHECK(lib.find("t.flags")->has_shield);                              // chain mail, shield
    REQUIRE_FALSE(lib.add(pc("t.old", {"lucky", "evasion"}), "character", "", 3));
    CHECK(lib.find("t.old")->base.reroll_ones == 0);
    CHECK_FALSE(lib.find("t.old")->base.evasion);
    REQUIRE_FALSE(lib.add(bard("t.bard"), "character"));
    const auto& b = lib.find("t.bard")->base;
    REQUIRE(b.reactions.size() == 1);
    CHECK(b.reactions[0].window == rules5e::ReactionTrigger::AttackRolled);
    CHECK(b.reactions[0].kind == rules5e::ReactionEffectKind::SubtractRoll);
    CHECK(b.reactions[0].dice.sides == 8);
    CHECK(b.reactions[0].pool == rules5e::spell_id_of("bardic_inspiration"));
    CHECK(lib.add(bard("t.bad1", "nope"), "character"));
    CHECK(lib.add(bard("t.bad2", "bardic_inspiration", "damaged"), "character"));
}

TEST_CASE("roll windows: the AttackRolled window opens in sim for a bard in range; replay matches") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(bard("t.bard"), "character"));
    REQUIRE_FALSE(lib.add(pc("t.tank", {}), "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(gob, "monster"));
    auto ch = arena({{"t.tank", Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{6, 5}}, {"t.bard", Vec2i{5, 9}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId tank = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    NearestEnemyPolicy pol;
    const ReactionDeciderFn decider = [&](const Combat& cc, const NeedsDecision& n) { return pol.decide_reaction(cc, n); };
    for (int i = 0; i < 40; ++i) {
        const CombatantId cur = rules5e::current_actor(c->state());
        Command cmd;
        if (cur == g) {
            if (c->state().budget.action) {
                cmd.request = {g, ActionKind::Attack, tank};
                cmd.request.attack_index = 0;
            } else cmd.request = {g, ActionKind::EndTurn};
        } else cmd.request = {cur, ActionKind::EndTurn};
        REQUIRE(c->submit(cmd).ok);
        c->settle(decider);
    }
    int opened = 0;
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::ReactionWindowOpened &&
            e.rules.a == int32_t(rules5e::ReactionTrigger::AttackRolled))
            ++opened;
    CHECK(opened >= 1);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 11, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
}
