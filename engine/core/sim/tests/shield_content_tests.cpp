// Shield content (CONTENT_CONVENTIONS §54): monster@5 reactions[] ally_attacked + ac_bonus 2, on a creature
// with the bound trait, compiles to WearerAc. The wearer is spawn overrides.bound_wearer. Within 5 feet
// the guardian may spend its reaction; the bonus is that attack only.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <utility>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::ReactionEffectKind;
using rules5e::ReactionTrigger;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const Json kBound = {{"kind", "bound"}, {"radius_ft", 60}};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json shield_reaction(const char* id = "shield", int bonus = 2) {
    return {{"id", id}, {"name", "Shield"}, {"window", {{"kind", "ally_attacked"}}},
            {"effect", {{"kind", "ac_bonus"}, {"bonus", bonus}}}};
}
Json creature(const std::string& id, bool bound, std::vector<Json> reactions, int hp = 80) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = id;
    m["hit_points"]["average"] = hp;
    if (bound)
        m["traits"].push_back({{"id", "bound"}, {"name", "Bound"}, {"description", "test"}, {"effect", kBound}});
    m["reactions"] = Json::array();
    for (const Json& r : reactions) m["reactions"].push_back(r);
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, const std::string& kind = "monster", int version = 5) {
    return lib.add(m, kind, "", version);
}
Json swing_action() {
    Json dmg = Json::object();
    dmg["flat"] = 5;
    dmg["type"] = "bludgeoning";
    Json act = Json::object();
    act["id"] = "hack";
    act["name"] = "hack";
    act["kind"] = "melee_weapon_attack";
    act["to_hit"] = 0;
    act["reach"] = 5;
    act["targets"] = 1;
    act["damage"] = Json::array({dmg});
    return act;
}
Json striker_monster() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.striker";
    m["actions"] = Json::array({swing_action()});
    return m;
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
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    REQUIRE(c.submit(e).ok);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
struct Resolved {
    bool present = false;
    int natural = 0;
    bool hit = false;
    bool crit = false;
};
Resolved last_resolved(const Combat& c) {
    Resolved o;
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == rules5e::EventKind::AttackResolved) {
            o.present = true;
            o.natural = e.rules.a;
            o.hit = (e.rules.c & 1) != 0;
            o.crit = (e.rules.c & 2) != 0;
        }
    return o;
}
class Answer final : public IActorPolicy {
public:
    explicit Answer(bool take) : take_(take) {}
    std::optional<CombatantId> choose_target(const Combat&, CombatantId) override { return std::nullopt; }
    rules5e::ReactionDecision decide_reaction(const Combat&, const rules5e::NeedsDecision& n) override {
        return {n.window_seq, take_, 0};
    }
private:
    bool take_;
};
}  // namespace

TEST_CASE("shield loader: ally_attacked ac_bonus 2 on a bound monster@5 compiles; other shapes do not") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json parry = {{"id", "parry"}, {"name", "Parry"}, {"window", {{"kind", "being_hit"}}},
                  {"effect", {{"kind", "ac_bonus"}, {"bonus", 2}}}};
    REQUIRE_FALSE(add(lib, creature("t.guard", true, {shield_reaction(), parry})));
    const auto* t = lib.find("t.guard");
    REQUIRE(t);
    REQUIRE(t->base.reactions.size() == 1);
    CHECK(t->base.reactions[0].kind == ReactionEffectKind::WearerAc);
    CHECK(t->base.reactions[0].window == ReactionTrigger::AllyAttacked);
    CHECK(t->base.reactions[0].ac_bonus == 2);
    CHECK(t->base.reactions[0].id == rules5e::spell_id_of("t.guard#shield"));
    CHECK(t->base.bound.radius_ft == 60);
    CHECK(t->base.bound.wearer == 0);
    REQUIRE(t->unsupported_features.size() == 1);
    CHECK(t->unsupported_features[0].find("being_hit") != std::string::npos);

    auto err = [&](const char* id, bool bound, std::vector<Json> rx, int version = 5, const char* kind = "monster") {
        const auto r = add(lib, creature(id, bound, std::move(rx)), kind, version);
        REQUIRE(r);
        return *r;
    };
    CHECK(err("t.nobound", false, {shield_reaction()}).find("needs the bound trait") != std::string::npos);
    CHECK(err("t.bonus", true, {shield_reaction("shield", 5)}).find("bonus must be 2") != std::string::npos);
    CHECK(err("t.old", false, {shield_reaction()}, 4).find("shield reaction needs a monster@5 file") != std::string::npos);
    CHECK(err("t.two", true, {shield_reaction("shield"), shield_reaction("shield_2")}).find("a creature has one shield") !=
          std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.shield_pc";
    ch["reactions"] = Json::array({shield_reaction()});
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc = pcs.add(ch, "character");
    REQUIRE(pc);
    CHECK(pc->find("only valid on monsters") != std::string::npos);
}

TEST_CASE("shield: 5 feet offers the guardian's reaction; 10 feet and another target do not") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, creature("t.guard", true, {shield_reaction()})));
    REQUIRE_FALSE(add(lib, creature("t.wearer", false, {}, 40)));
    REQUIRE_FALSE(add(lib, creature("t.other", false, {}, 40)));
    if (auto e = add(lib, striker_monster())) FAIL(*e);
    const int hack = [&] {
        const auto* s = lib.find("t.striker");
        for (int i = 0; i < int(s->base.attacks.size()); ++i)
            if (s->base.attacks[std::size_t(i)].id == "hack") return i;
        return -1;
    }();
    REQUIRE(hack >= 0);

    auto open = [&](int guard_y, uint64_t seed, ecs::Registry& reg, bool other) {
        std::vector<std::pair<std::string, Vec2i>> spawns = {{"t.striker", Vec2i{5, 5}}, {"t.wearer", Vec2i{6, 5}},
                                                              {"t.guard", Vec2i{6, guard_y}}};
        if (other) spawns.push_back({"t.other", Vec2i{5, 6}});
        auto ch = arena(std::move(spawns));
        ch.spawns[0].overrides_json = R"({"team":"party"})";
        ch.spawns[2].overrides_json = std::string("{\"bound_wearer\":") + std::to_string(ch.spawns[1].stable_id) + "}";
        auto c = Combat::create(reg, ch, lib, seed);
        REQUIRE(c);
        auto* wearer = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), ch.spawns[1].stable_id));
        wearer->ac = 12;
        return std::pair{std::move(ch), std::move(c)};
    };

    bool boundary = false;
    for (uint64_t seed = 1; seed < 80 && !boundary; ++seed) {
        ecs::Registry reg;
        auto [ch, c] = open(6, seed, reg, false);   // one tile: 5 feet
        const CombatantId striker = ch.spawns[0].stable_id;
        const CombatantId wearer = ch.spawns[1].stable_id;
        const CombatantId guard = ch.spawns[2].stable_id;
        to_actor(*c, striker);
        Command cmd;
        cmd.request = {striker, ActionKind::Attack, wearer, uint8_t(hack)};
        auto paused = c->submit(cmd);
        REQUIRE(paused.ok);
        if (!paused.needs) continue;
        CHECK(paused.needs->trigger == ReactionTrigger::AllyAttacked);
        CHECK(paused.needs->reactor == guard);
        CHECK(paused.needs->target == wearer);
        CHECK(paused.needs->attack_total == 0);
        REQUIRE(paused.needs->options.size() == 1);
        CHECK(paused.needs->options[0].ac_bonus == 2);
        Answer yes(true);
        auto took = c->decide(yes.decide_reaction(*c, *paused.needs));
        REQUIRE(took.ok);
        const auto y = last_resolved(*c);
        if (y.natural != 12 && y.natural != 20) continue;
        boundary = true;
        CHECK(rules5e::find(c->state(), guard)->reaction_used);
        CHECK(rules5e::find(c->state(), wearer)->reaction_ac_bonus == 0);
        CHECK(rules5e::find(c->state(), wearer)->ac == 12);
        if (y.natural == 12) {
            CHECK_FALSE(y.hit);
            CHECK(rules5e::find(c->state(), wearer)->hp == 40);
        } else {
            CHECK(y.hit);
            CHECK(y.crit);
        }

        ecs::Registry reg_no;
        auto [ch_no, c_no] = open(6, seed, reg_no, false);
        to_actor(*c_no, ch_no.spawns[0].stable_id);
        Command bare;
        bare.request = {ch_no.spawns[0].stable_id, ActionKind::Attack, ch_no.spawns[1].stable_id, uint8_t(hack)};
        auto ask = c_no->submit(bare);
        REQUIRE(ask.needs);
        Answer no(false);
        auto declined = c_no->decide(no.decide_reaction(*c_no, *ask.needs));
        REQUIRE(declined.ok);
        const auto d = last_resolved(*c_no);
        CHECK(d.natural == y.natural);
        CHECK_FALSE(rules5e::find(c_no->state(), ch_no.spawns[2].stable_id)->reaction_used);
        if (y.natural == 12) CHECK(d.hit);
        else CHECK(d.crit);
    }
    REQUIRE(boundary);

    ecs::Registry far_reg;
    auto [far_ch, far] = open(7, 1, far_reg, false);   // two tiles: 10 feet
    to_actor(*far, far_ch.spawns[0].stable_id);
    Command far_cmd;
    far_cmd.request = {far_ch.spawns[0].stable_id, ActionKind::Attack, far_ch.spawns[1].stable_id, uint8_t(hack)};
    auto far_step = far->submit(far_cmd);
    REQUIRE(far_step.ok);
    CHECK_FALSE(far_step.needs);
    CHECK_FALSE(rules5e::find(far->state(), far_ch.spawns[2].stable_id)->reaction_used);

    ecs::Registry other_reg;
    auto [other_ch, other] = open(6, 1, other_reg, true);
    to_actor(*other, other_ch.spawns[0].stable_id);
    Command other_cmd;
    other_cmd.request = {other_ch.spawns[0].stable_id, ActionKind::Attack, other_ch.spawns[3].stable_id, uint8_t(hack)};
    auto other_step = other->submit(other_cmd);
    REQUIRE(other_step.ok);
    CHECK_FALSE(other_step.needs);
    CHECK(rules5e::find(other->state(), other_ch.spawns[2].stable_id)->bound.wearer == other_ch.spawns[1].stable_id);
    CHECK_FALSE(rules5e::find(other->state(), other_ch.spawns[2].stable_id)->reaction_used);
}
