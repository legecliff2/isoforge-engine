// Keen Senses (CONTENT_CONVENTIONS §29): monster@5 loader, the wolf stamp, and no hash fold.
// Static content, like darkvision. perception_senses is not folded into log_hash (same as skill, dc, check_mode).
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <utility>

#include "rules5e/checks.hpp"
#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Skill;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json creature(const std::string& id, std::vector<Json> effects) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    int i = 0;
    for (Json& e : effects)
        m["traits"].push_back({{"id", "ks_" + std::to_string(i++)}, {"name", "Keen Senses"}, {"description", "test"}, {"effect", e}});
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
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
rules5e::Combatant* poke(Combat& c, CombatantId id) {
    return const_cast<rules5e::Combatant*>(rules5e::find(c.state(), id));
}
}  // namespace

TEST_CASE("keen_senses loader: bits, union, refusals, the wolf stamp") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const Json hear = {{"kind", "keen_senses"}, {"senses", {"hearing", "smell"}}};
    REQUIRE_FALSE(add(lib, creature("t.ks", {hear, {{"kind", "keen_senses"}, {"senses", {"smell"}}}})));
    CHECK(lib.find("t.ks")->base.keen_senses == uint8_t(rules5e::kKeenHearing | rules5e::kKeenSmell));
    REQUIRE_FALSE(add(lib, creature("t.ks2", {hear, {{"kind", "keen_senses"}, {"senses", {"sight"}}}})));
    CHECK(lib.find("t.ks2")->base.keen_senses == uint8_t(rules5e::kKeenSight | rules5e::kKeenHearing | rules5e::kKeenSmell));

    auto err = [&](const char* id, Json e, int v = 5) {
        const auto r = add(lib, creature(id, {std::move(e)}), v);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.e1", {{"kind", "keen_senses"}, {"senses", Json::array()}}).find("bad senses") != std::string::npos);
    CHECK(err("t.e2", {{"kind", "keen_senses"}}).find("bad senses") != std::string::npos);
    CHECK(err("t.e3", {{"kind", "keen_senses"}, {"senses", {"hearing", "hearing"}}}).find("bad senses") != std::string::npos);
    CHECK(err("t.e4", {{"kind", "keen_senses"}, {"senses", {"taste"}}}).find("bad senses") != std::string::npos);
    CHECK(err("t.e5", {{"kind", "keen_senses"}, {"senses", {"hearing"}}, {"note", 1}}).find("unknown key 'note'") != std::string::npos);
    CHECK(err("t.e6", hear, 4).find("needs a monster@5 file") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.ks_pc";
    ch["features"].push_back({{"id", "keen"}, {"name", "Keen Senses"}, {"description", "test"}, {"effect", hear}});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc_err = pcs.add(ch, "character");
    REQUIRE(pc_err);
    CHECK(pc_err->find("only valid on monsters") != std::string::npos);

    const auto* wolf = lib.find("srd51.monster.wolf");
    REQUIRE(wolf);
    CHECK(wolf->base.keen_senses == uint8_t(rules5e::kKeenHearing | rules5e::kKeenSmell));
    CHECK(wolf->base.passive_perception_override == -1);
    // skills.perception is not loaded onto the combatant (docs/notes/monster_printed_skills.md). Keen does not invent
    // a +5: the wolf's rules passive stays 10 + Wis (+1).
    CHECK(rules5e::passive_perception(wolf->base) == 11);
    CHECK(wolf->base.abilities[rules5e::Ability::Wis] == 12);
}

TEST_CASE("keen_senses: no hash fold; perception_senses is not in log_hash; a check replays") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    auto ch = arena({{"srd51.monster.wolf", Vec2i{6, 5}}, {"srd51.character.fighter_1", Vec2i{5, 5}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId wolf = ch.spawns[0].stable_id;
    const uint64_t h0 = c->hash();
    const uint64_t l0 = c->log_hash();
    poke(*c, wolf)->keen_senses = 0;
    CHECK(c->hash() == h0);
    CHECK(c->log_hash() == l0);
    poke(*c, wolf)->keen_senses = uint8_t(rules5e::kKeenSight | rules5e::kKeenHearing | rules5e::kKeenSmell);
    CHECK(c->hash() == h0);
    CHECK(c->log_hash() == l0);

    auto run = [&](uint8_t senses) {
        ecs::Registry r;
        auto fight = Combat::create(r, ch, lib, 11);
        REQUIRE(fight);
        poke(*fight, wolf)->keen_senses = 0;   // the request names a sense; the creature has none, so the roll is normal
        to_actor(*fight, wolf);
        Command cmd;
        cmd.request.actor = wolf;
        cmd.request.kind = ActionKind::AbilityCheck;
        cmd.request.skill = Skill::Perception;
        cmd.request.dc = 10;
        cmd.request.perception_senses = senses;
        REQUIRE(fight->submit(cmd).ok);
        return std::make_pair(fight->hash(), fight->log_hash());
    };
    const auto named = run(rules5e::kKeenSmell);
    const auto blank = run(0);
    CHECK(named.first == blank.first);     // same dice: naming a sense does not grant advantage by itself
    CHECK(named.second == blank.second);   // and log_hash does not fold perception_senses

    ecs::Registry live;
    auto fight = Combat::create(live, ch, lib, 11);
    REQUIRE(fight);
    to_actor(*fight, wolf);
    Command cmd;
    cmd.request.actor = wolf;
    cmd.request.kind = ActionKind::AbilityCheck;
    cmd.request.skill = Skill::Perception;
    cmd.request.dc = 10;
    cmd.request.perception_senses = rules5e::kKeenSmell;
    REQUIRE(fight->submit(cmd).ok);
    ecs::Registry again;
    auto rp = replay(again, ch, lib, 11, fight->log());
    REQUIRE(rp);
    CHECK(rp->hash() == fight->hash());
    CHECK(rp->log_hash() == fight->log_hash());
}
