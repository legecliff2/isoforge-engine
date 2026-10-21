// Row 11, sim side: reactions[] entries compile into Combatant::reactions rows (+ synthetic Retaliate SpellDefs), the
// in_range hook opens Damaged / AllyAttacked windows for the right creatures, and the default decider answers them.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
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
const char* kFighter = "srd51.character.fighter_1";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json reaction(const char* id, const char* window, Json effect, Json win_extra = Json::object()) {
    Json w = {{"kind", window}};
    for (auto& [k, v] : win_extra.items()) w[k] = v;
    return {{"id", id}, {"name", "R"}, {"window", w}, {"effect", effect}};
}
Json goblin(const std::string& id, std::vector<Json> reactions) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    m["reactions"] = Json::array();
    for (const Json& r : reactions) m["reactions"].push_back(r);
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
ContentLibrary lib_with(const std::vector<Json>& docs) {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    for (const Json& d : docs) { const auto e = lib.add(d, "monster"); REQUIRE_MESSAGE(!e, e.value_or("")); }
    return lib;
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
}  // namespace

TEST_CASE("reactions content: damaged / ally_attacked entries compile; the rest is listed") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json save = {{"kind", "retaliate"}, {"save", {{"ability", "dex"}, {"dc", 13}}}, {"on_success", "half"},
                 {"damage", Json::array({{{"dice", "2d10"}, {"type", "fire"}}})}};
    Json nosave = {{"kind", "retaliate"}, {"damage", Json::array({{{"dice", "1d4"}, {"type", "force"}}})}};
    REQUIRE_FALSE(lib.add(goblin("t.r", {reaction("uncanny", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}}),
                                         reaction("deflect", "damaged", {{"kind", "reduce_damage"}, {"dice", "1d10+5"}}, {{"ranged_only", true}}),
                                         reaction("protect", "ally_attacked", {{"kind", "impose_disadvantage"}}, {{"melee_only", true}}),
                                         reaction("rebuke", "damaged", save), reaction("zap", "damaged", nosave),
                                         reaction("riposte", "damaged", {{"kind", "attack"}, {"attack_id", "scimitar"}}),
                                         reaction("parry", "being_hit", {{"kind", "ac_bonus"}, {"bonus", 2}}),
                                         reaction("casty", "damaged", {{"kind", "cast"}, {"spell", "srd51.spell.shield"}}),
                                         reaction("usey", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}}, Json::object())}), "monster"));
    const auto* a = lib.find("t.r");
    const auto& rx = a->base.reactions;
    REQUIRE(rx.size() == 7);   // parry (window) and casty (effect) are listed
    CHECK(rx[0].kind == ReactionEffectKind::ReduceDamage);
    CHECK(rx[0].half);
    CHECK(rx[0].window == ReactionTrigger::Damaged);
    CHECK(rx[0].id == rules5e::spell_id_of("t.r#uncanny"));
    CHECK(rx[1].dice.sides == 10);
    CHECK(rx[1].dice.bonus == 5);
    CHECK(rx[1].ranged_only);
    CHECK_FALSE(rx[0].requires_sight);
    CHECK(rx[2].window == ReactionTrigger::AllyAttacked);
    CHECK(rx[2].kind == ReactionEffectKind::ImposeDisadvantage);
    CHECK(rx[2].melee_only);
    const auto* rb = lib.spells()->find(rx[3].spell_id);
    REQUIRE(rb);
    CHECK(rb->kind == rules5e::SpellEffectKind::Save);
    CHECK(rb->fixed_dc == 13);
    CHECK_FALSE(rb->is_spell);
    const auto* zp = lib.spells()->find(rx[4].spell_id);
    REQUIRE(zp);
    CHECK(zp->kind == rules5e::SpellEffectKind::AutoHit);
    CHECK_FALSE(zp->is_spell);
    CHECK(rx[5].kind == ReactionEffectKind::Attack);
    CHECK(rx[5].attack_index == 0);
    CHECK(a->unsupported_features.size() == 2);
    CHECK(a->trigger_spell_ids.size() == 2);
    // load errors
    for (const auto& bad : std::vector<Json>{
             reaction("x", "ally_attacked", {{"kind", "reduce_damage"}, {"fraction", "half"}}),
             reaction("x", "damaged", {{"kind", "impose_disadvantage"}}),
             reaction("x", "damaged", {{"kind", "attack"}, {"attack_id", "nope"}}),
             reaction("x", "damaged", {{"kind", "reduce_damage"}}),
             reaction("x", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}}, {{"melee_only", true}, {"ranged_only", true}})}) {
        const auto e = lib.add(goblin("t.bad", {bad}), "monster");
        REQUIRE(e);
        CHECK(e->find("t.bad") != std::string::npos);
    }
    const auto dup = lib.add(goblin("t.dup", {reaction("same", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}}),
                                              reaction("same", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}})}), "monster");
    REQUIRE(dup);
    CHECK(dup->find("collides") != std::string::npos);
    // a file that declared < 4 compiles nothing
    REQUIRE_FALSE(lib.add(goblin("t.old", {reaction("u", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}})}), "monster", "", 3));
    CHECK(lib.find("t.old")->base.reactions.empty());
}

TEST_CASE("reactions in a fight: Damaged halves a hit; AllyAttacked only opens for a creature within 5 ft") {
    Json half = reaction("uncanny", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}});
    Json prot = reaction("protect", "ally_attacked", {{"kind", "impose_disadvantage"}});
    auto lib = lib_with({goblin("t.dodge", {half}), goblin("t.guard", {prot}), goblin("t.plain", {})});
    class P final : public IActorPolicy {
    public:
        std::optional<CombatantId> choose_target(const Combat&, CombatantId) override { return std::nullopt; }
    } policy;
    {   // Damaged: the fighter hits the goblin; the window belongs to the goblin and takes option 0
        auto ch = arena({{"t.dodge", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId g = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        bool saw = false;
        for (int t = 0; t < 12 && !saw; ++t) {
            to_actor(*c, f);
            Command a;
            a.request = {f, ActionKind::Attack, g};
            a.request.attack_index = 0;
            auto r = c->submit(a);
            REQUIRE_MESSAGE(r.ok, r.error);
            if (r.needs) {
                CHECK(r.needs->trigger == ReactionTrigger::Damaged);
                CHECK(r.needs->reactor == g);
                saw = true;
                CHECK(c->decide(policy.decide_reaction(*c, *r.needs)).ok);
            }
            Command e;
            e.request = {f, ActionKind::EndTurn};
            if (rules5e::current_actor(c->state()) == f) c->submit(e);
        }
        CHECK(saw);
    }
    for (int dist : {1, 8}) {   // AllyAttacked: guard next to the goblin (window) vs far away (none)
        auto ch = arena({{"t.guard", Vec2i{5, 5 + dist}}, {"t.plain", Vec2i{5, 5}}, {kFighter, Vec2i{6, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 4);
        REQUIRE(c);
        const CombatantId guard = ch.spawns[0].stable_id, tgt = ch.spawns[1].stable_id, f = ch.spawns[2].stable_id;
        to_actor(*c, f);
        Command a;
        a.request = {f, ActionKind::Attack, tgt};
        a.request.attack_index = 0;
        auto r = c->submit(a);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (dist == 1) {
            REQUIRE(r.needs);
            CHECK(r.needs->trigger == ReactionTrigger::AllyAttacked);
            CHECK(r.needs->reactor == guard);
        } else {
            CHECK_FALSE(r.needs);
        }
    }
}

TEST_CASE("7.2y reactions content: requires_sight on the window (Uncanny Dodge)") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(lib.add(goblin("t.ud", {reaction("uncanny", "damaged", {{"kind", "reduce_damage"}, {"fraction", "half"}},
                                                   {{"requires_sight", true}})}), "monster"));
    REQUIRE(lib.find("t.ud")->base.reactions.size() == 1);
    CHECK(lib.find("t.ud")->base.reactions[0].requires_sight);
}
