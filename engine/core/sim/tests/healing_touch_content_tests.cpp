// Healing Touch content: monster@5 action healing_touch. Specimen stays t.touch and does not load unicorn.json.
// The Horn attack is a separate melee attack. Replay reproduces the heal. The built-in AI does not cast it.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json horn_attack() {
    return {{"id", "horn"}, {"name", "Horn"}, {"kind", "melee_weapon_attack"},
            {"to_hit", 7}, {"reach", 5}, {"targets", 1},
            {"damage", Json::array({{{"dice", "1d8+4"}, {"type", "piercing"}}})}};
}
Json touch_action() {
    return {{"id", "healing_touch"}, {"name", "Healing Touch"}, {"kind", "healing_touch"}};
}
Json monster_doc(const std::string& id, std::vector<Json> actions, int version = 5) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@" + std::to_string(version);
    m["id"] = id;
    m["hit_points"]["average"] = 40;
    m["actions"] = Json::array();
    for (auto& a : actions) m["actions"].push_back(std::move(a));
    return m;
}
std::optional<std::string> add(ContentLibrary& lib, const Json& m, int version = 5) {
    return lib.add(m, "monster", "", version);
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
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 12 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
SubmitResult submit(Combat& c, Command cmd) {
    auto r = c.submit(cmd);
    while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
    return r;
}
}  // namespace

TEST_CASE("healing touch loader: monster@5, 2d8+2, 3/day, horn attack unchanged") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.touch", {horn_attack(), touch_action()})));
    const auto* arch = lib.find("t.touch");
    REQUIRE(arch);
    REQUIRE(arch->base.attacks.size() == 1);
    CHECK(arch->base.attacks[0].id == "horn");
    CHECK(arch->base.attacks[0].to_hit == 7);
    CHECK(arch->base.attacks[0].damage.count == 1);
    CHECK(arch->base.attacks[0].damage.sides == 8);
    CHECK(arch->base.attacks[0].damage.bonus == 4);
    CHECK(arch->base.attacks[0].type == rules5e::DamageType::Piercing);
    CHECK(arch->base.attacks[0].reach_or_range_ft == 5);
    CHECK_FALSE(arch->base.attacks[0].ranged);
    REQUIRE(arch->save_actions.size() == 1);
    CHECK(arch->save_actions[0].healing_touch);
    CHECK(arch->save_actions[0].uses == 3);
    CHECK(arch->save_actions[0].recharge == Recharge::LongRest);
    CHECK(arch->save_actions[0].usage_kind == 2);
    CHECK(arch->save_actions[0].range_ft == 5);
    const auto* d = lib.spells()->find(rules5e::spell_id_of("t.touch#healing_touch"));
    REQUIRE(d);
    CHECK(d->healing_touch);
    CHECK(d->kind == rules5e::SpellEffectKind::Heal);
    CHECK_FALSE(d->is_spell);
    CHECK(d->magical);
    CHECK(d->dice.count == 2);
    CHECK(d->dice.sides == 8);
    CHECK(d->dice.bonus == 2);
    CHECK(d->range_ft == 5);
    CHECK(d->usage_kind == 2);

    Json extra = touch_action();
    extra["usage"] = {{"kind", "per_day"}, {"uses", 3}};
    const auto key = add(lib, monster_doc("t.key", {extra}));
    REQUIRE(key);
    CHECK(key->find("healing touch: unknown key 'usage'") != std::string::npos);
    const auto second = add(lib, monster_doc("t.two", {touch_action(), touch_action()}));
    REQUIRE(second);
    CHECK(second->find("healing touch: a creature has one") != std::string::npos);
    const auto old = add(lib, monster_doc("t.old", {touch_action()}, 4), 4);
    REQUIRE(old);
    CHECK(old->find("healing touch needs a monster@5 file") != std::string::npos);

    Json ch = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    ch["id"] = "t.touch_pc";
    ch["actions"].push_back(touch_action());
    ContentLibrary pcs;
    REQUIRE(pcs.load(sample_set()).empty());
    const auto pc = pcs.add(ch, "character");
    REQUIRE(pc);
    CHECK(pc->find("healing touch: only valid on monsters") != std::string::npos);
}

TEST_CASE("healing touch: out of reach is refused") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.touch", {horn_attack(), touch_action()})));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    REQUIRE_FALSE(add(lib, foe));
    auto ch = arena({{"t.touch", {2, 2}}, {"t.foe", {4, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ch.spawns[1].overrides_json = R"({"team":"foes"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId touch = ch.spawns[0].stable_id;
    const CombatantId foe_id = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.touch#healing_touch");
    const int before = rules5e::find(c->state(), foe_id)->hp;
    advance_to(*c, touch);
    Command far;
    far.request = {touch, ActionKind::CastSpell, foe_id};
    far.request.spell_id = sid;
    far.request.targets = {foe_id};
    auto refused = submit(*c, far);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "healing touch: touch range");
    CHECK(rules5e::find(c->state(), foe_id)->hp == before);
    CHECK(rules5e::feature_uses_left(c->state(), touch, sid) == 3);
}

TEST_CASE("healing touch: self refused, a logged hit then the heal replays, horn still attacks") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json doc = monster_doc("t.touch", {horn_attack(), touch_action()});
    doc["actions"][0]["to_hit"] = 30;
    REQUIRE_FALSE(add(lib, doc));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    foe["hit_points"]["average"] = 40;
    REQUIRE_FALSE(add(lib, foe));
    auto ch = arena({{"t.touch", {2, 2}}, {"t.foe", {3, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ch.spawns[1].overrides_json = R"({"team":"foes"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 11);
    REQUIRE(c);
    const CombatantId touch = ch.spawns[0].stable_id;
    const CombatantId foe_id = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.touch#healing_touch");
    CHECK(rules5e::feature_uses_left(c->state(), touch, sid) == 3);
    CHECK(rules5e::find(c->state(), touch)->attacks[0].damage.sides == 8);
    advance_to(*c, touch);

    Command self;
    self.request = {touch, ActionKind::CastSpell, touch};
    self.request.spell_id = sid;
    auto refused_self = submit(*c, self);
    CHECK_FALSE(refused_self.ok);
    CHECK(refused_self.error == "healing touch: another creature");
    CHECK(rules5e::feature_uses_left(c->state(), touch, sid) == 3);

    Command horn;
    horn.request = {touch, ActionKind::Attack, foe_id, 0};
    auto struck = submit(*c, horn);
    REQUIRE_MESSAGE(struck.ok, struck.error);
    const int wounded = rules5e::find(c->state(), foe_id)->hp;
    CHECK(wounded < 40);
    CHECK(rules5e::find(c->state(), touch)->attacks[0].damage.bonus == 4);
    Command done;
    done.request = {touch, ActionKind::EndTurn};
    REQUIRE(submit(*c, done).ok);

    advance_to(*c, touch);
    Command near;
    near.request = {touch, ActionKind::CastSpell, foe_id};
    near.request.spell_id = sid;
    near.request.targets = {foe_id};
    auto healed = submit(*c, near);
    REQUIRE_MESSAGE(healed.ok, healed.error);
    const int hp = rules5e::find(c->state(), foe_id)->hp;
    CHECK(hp > wounded);
    CHECK(hp <= 40);
    CHECK(rules5e::feature_uses_left(c->state(), touch, sid) == 2);
    const uint64_t after = c->hash();

    std::string err;
    ecs::Registry reg2;
    auto again = replay(reg2, ch, lib, c->seed(), c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == after);
    CHECK(rules5e::find(again->state(), foe_id)->hp == hp);
    CHECK(rules5e::find(again->state(), touch)->attacks[0].to_hit == 30);
    CHECK(rules5e::find(again->state(), touch)->attacks[0].damage.sides == 8);
}

TEST_CASE("healing touch: the built-in AI attacks with the horn and does not heal the foe") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.touch", {horn_attack(), touch_action()})));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    REQUIRE_FALSE(add(lib, foe));
    auto ch = arena({{"t.touch", {2, 2}}, {"t.foe", {3, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"monsters"})";
    ch.spawns[1].overrides_json = R"({"team":"party"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const CombatantId touch = ch.spawns[0].stable_id;
    const CombatantId foe_id = ch.spawns[1].stable_id;
    advance_to(*c, touch);
    const Command cmd = next_command(*c, foe_id);
    CHECK(cmd.request.kind == ActionKind::Attack);
    CHECK(cmd.request.attack_index == 0);
    CHECK(cmd.request.spell_id != rules5e::spell_id_of("t.touch#healing_touch"));
}

TEST_CASE("healing touch: three uses then the fourth is refused") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    REQUIRE_FALSE(add(lib, monster_doc("t.touch", {horn_attack(), touch_action()})));
    Json foe = load_json("content/packs/srd51.core/monsters/goblin.json");
    foe["$schema"] = "monster@5";
    foe["id"] = "t.foe";
    REQUIRE_FALSE(add(lib, foe));
    auto ch = arena({{"t.touch", {2, 2}}, {"t.foe", {3, 2}}});
    ch.spawns[0].overrides_json = R"({"team":"party"})";
    ch.spawns[1].overrides_json = R"({"team":"foes"})";
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    const CombatantId touch = ch.spawns[0].stable_id;
    const CombatantId foe_id = ch.spawns[1].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.touch#healing_touch");
    auto* patient = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), foe_id));
    patient->hp_max = 80;
    for (int n = 0; n < 3; ++n) {
        advance_to(*c, touch);
        patient->hp = 4;
        Command cast;
        cast.request = {touch, ActionKind::CastSpell, foe_id};
        cast.request.spell_id = sid;
        cast.request.targets = {foe_id};
        auto r = submit(*c, cast);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(rules5e::feature_uses_left(c->state(), touch, sid) == uint8_t(2 - n));
        Command done;
        done.request = {touch, ActionKind::EndTurn};
        REQUIRE(submit(*c, done).ok);
    }
    advance_to(*c, touch);
    Command cast;
    cast.request = {touch, ActionKind::CastSpell, foe_id};
    cast.request.spell_id = sid;
    cast.request.targets = {foe_id};
    auto denied = submit(*c, cast);
    CHECK_FALSE(denied.ok);
    CHECK(denied.error == "no uses left");
    CHECK(rules5e::feature_uses_left(c->state(), touch, sid) == 0);
    CHECK(c->state().budget.action);
}
