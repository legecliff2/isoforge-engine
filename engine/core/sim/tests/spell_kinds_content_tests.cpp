// R-G15 sim wiring (rules #105 / #106, VERSION_BUMP_BATCH 7.2f / 7.2g hand-offs): the spell@1 kinds hp_pool, dispel, revive,
// end_condition, delayed_damage (also as a rider) and sustained_damage compile into rules5e SpellDefs; they cast through
// sim::Combat (area aim for Sleep, a dead target for Revivify), the AI sustains Heat Metal and wakes a sleeping ally, and
// the new state replays.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;
using rules5e::SpellEffectKind;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json spell(const std::string& name, int level, Json effects, Json extra = Json::object()) {
    Json j = {{"$schema", "spell@1"}, {"id", "test.spell." + name}, {"name", name}, {"level", level},
              {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 90}}},
              {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}}, {"effects", std::move(effects)}};
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    return j;
}

Json sleep_doc() {
    return spell("sleep", 1,
                 Json::array({{{"kind", "hp_pool"}, {"dice", "1d1+29"}, {"condition", "unconscious"}, {"ends_on_damage", true},
                               {"excludes_creature_type", {"undead"}}}}),
                 {{"area", {{"shape", "sphere"}, {"size_ft", 20}}}, {"duration", {{"kind", "minutes"}, {"amount", 1}}},
                  {"scaling", {{"upcast", {{"dice", 2}}}}}});
}
Json color_spray_doc() {
    return spell("color_spray", 1, Json::array({{{"kind", "hp_pool"}, {"dice", "6d10"}, {"condition", "blinded"}}}),
                 {{"range", {{"kind", "self"}}}, {"area", {{"shape", "cone"}, {"size_ft", 15}}},
                  {"duration", {{"kind", "rounds"}, {"amount", 1}}}, {"scaling", {{"upcast", {{"dice", 2}}}}}});
}
Json dispel_doc() {
    return spell("dispel_magic", 3,
                 Json::array({{{"kind", "dispel"}, {"check", {{"ability", "spellcasting"}, {"dc_base", 10}, {"plus", "spell_level"}}}}}),
                 {{"range", {{"kind", "ranged"}, {"feet", 120}}}});
}
Json revivify_doc(bool priced) {
    Json m = {{"text", "diamonds worth 300 gp"}};
    if (priced) { m["cost_cp"] = 30000; m["consumed"] = true; }
    return spell(priced ? "revivify" : "revivify_free", 3, Json::array({{{"kind", "revive"}, {"within_rounds", 10}}}),
                 {{"range", {{"kind", "touch"}}}, {"components", {{"v", true}, {"s", true}, {"m", m}}}});
}
Json restoration_doc() {
    return spell("lesser_restoration", 2,
                 Json::array({{{"kind", "end_condition"}, {"conditions", {"blinded", "deafened", "paralyzed", "poisoned"}}}}),
                 {{"range", {{"kind", "touch"}}}});
}
Json acid_arrow_doc() {
    return spell("acid_arrow", 2,
                 Json::array({{{"kind", "spell_attack"}, {"attack", "ranged"}, {"damage", {{"dice", "4d4"}, {"type", "acid"}}}},
                              {{"kind", "delayed_damage"}, {"applies_to", "affected"}, {"damage", {{"dice", "2d4"}, {"type", "acid"}}},
                               {"expiry", {{"kind", "until_end_of_target_next_turn"}}}}}),
                 {{"scaling", {{"upcast", {{"dice", 1}}}}}});
}
Json heat_metal_doc() {
    return spell("heat_metal", 2, Json::array({{{"kind", "sustained_damage"}, {"damage", {{"dice", "2d8"}, {"type", "fire"}}}}}),
                 {{"casting_time", {{"kind", "bonus_action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
                  {"duration", {{"kind", "minutes"}, {"amount", 1}, {"concentration", true}}}, {"scaling", {{"upcast", {{"dice", 1}}}}}});
}
Json hold_doc() {   // a level-2 condition without a save (deterministic): Dispel Magic / Lesser Restoration end it
    return spell("hold", 2, Json::array({{{"kind", "apply_condition"}, {"condition", "paralyzed"}}}),
                 {{"duration", {{"kind", "minutes"}, {"amount", 1}, {"concentration", true}}}});
}

rules5e::SpellDef def_of(const Json& j) {
    auto r = rules5e_content::spell_from_json(j);
    INFO(r.error);
    REQUIRE(r.def);
    return *r.def;
}
std::string error_of(const Json& j) {
    auto r = rules5e_content::spell_from_json(j);
    CHECK_FALSE(r.def);
    return r.error;
}

const char* kWizard = "test.character.wizard";
Json monster(const std::string& id, int hp, const char* base = "content/packs/srd51.core/monsters/goblin.json") {
    Json g = load_json(base);
    g["id"] = id;
    g["hit_points"]["average"] = hp;
    return g;
}

ContentLibrary make_lib() {
    ContentLibrary lib;
    for (const Json& d : {sleep_doc(), color_spray_doc(), dispel_doc(), revivify_doc(true), revivify_doc(false), restoration_doc(),
                          acid_arrow_doc(), heat_metal_doc(), hold_doc()})
        REQUIRE_FALSE(lib.add_spell(d));
    REQUIRE_FALSE(lib.add_spell(load_json("content/packs/srd51.core/spells/magic_missile.json")));
    Json w = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    w["id"] = kWizard;
    w["abilities"]["int"] = 16;
    w["hit_points"]["average"] = 40;
    Json known = Json::array();
    for (const char* n : {"sleep", "color_spray", "dispel_magic", "revivify", "revivify_free", "lesser_restoration", "acid_arrow",
                          "heat_metal", "hold"})
        known.push_back(std::string("test.spell.") + n);
    known.push_back("srd51.spell.magic_missile");
    w["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"slots", {{"1", 4}, {"2", 3}, {"3", 3}}}, {"spells", known}};
    REQUIRE_FALSE(lib.add(w, "character"));
    REQUIRE_FALSE(lib.add(monster("test.monster.gob7", 7), "monster"));
    REQUIRE_FALSE(lib.add(monster("test.monster.gob10", 10), "monster"));
    REQUIRE_FALSE(lib.add(monster("test.monster.gob20", 20), "monster"));
    REQUIRE_FALSE(lib.add(monster("test.monster.gob1", 1), "monster"));
    REQUIRE_FALSE(lib.add(monster("test.monster.gob60", 60), "monster"));
    REQUIRE_FALSE(lib.add(monster("test.monster.bones", 13, "content/packs/srd51.core/monsters/skeleton.json"), "monster"));
    return lib;
}

struct Spawn { std::string archetype; int x, y; };
world::Chunk arena(const std::vector<Spawn>& spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    uint64_t seed;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, std::vector<Spawn> spawns, uint64_t s = 1) : lib(&l), seed(s) {
        chunk = arena(spawns);
        c = Combat::create(reg, chunk, l, s);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    SubmitResult settle(SubmitResult r) {   // decline every reaction
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void end_turn() {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(settle(c->submit(e)).ok);
    }
    void advance_to(CombatantId who) {
        for (int i = 0; i < 16 && rules5e::current_actor(c->state()) != who; ++i) end_turn();
        REQUIRE(rules5e::current_actor(c->state()) == who);
    }
    void next_turn_of(CombatantId who) {   // end the current turn, then advance to `who`
        end_turn();
        advance_to(who);
    }
    Command cast_cmd(const std::string& sp, uint8_t level, CombatantId target, std::vector<CombatantId> targets = {}) const {
        Command cmd;
        cmd.request = {rules5e::current_actor(c->state()), ActionKind::CastSpell, target};
        cmd.request.spell_id = spell_id_of(sp);
        cmd.request.spell_level = level;
        cmd.request.targets = std::move(targets);
        return cmd;
    }
    SubmitResult cast(const std::string& sp, uint8_t level, CombatantId target, std::vector<CombatantId> targets = {}) {
        return settle(c->submit(cast_cmd(sp, level, target, std::move(targets))));
    }
    const rules5e::Combatant& who(CombatantId i) const { return *rules5e::find(c->state(), i); }
    int count(EventKind k, CombatantId target = 0) const {
        int n = 0;
        for (const auto& e : c->events())
            n += e.kind == SimEvent::Kind::Rules && e.rules.kind == k && (target == 0 || e.rules.target == target || e.rules.actor == target);
        return n;
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, seed, c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
};
} // namespace

TEST_CASE("R-G15 loader: hp_pool, dispel, revive, end_condition, delayed_damage, sustained_damage") {
    const auto sl = def_of(sleep_doc());
    CHECK(sl.kind == SpellEffectKind::HpPool);
    CHECK(sl.dice.count == 1); CHECK(sl.dice.sides == 1); CHECK(sl.dice.bonus == 29);
    CHECK(sl.condition == Condition::Unconscious);
    CHECK(sl.ends_on_damage);
    CHECK(sl.excluded_types == rules5e::bit(rules5e::CreatureType::Undead));
    CHECK(sl.expiry == 1); CHECK(sl.duration_rounds == 10);   // 1 minute
    CHECK(sl.upcast_dice == 2);
    CHECK_FALSE(sl.applies_condition);
    const auto cs = def_of(color_spray_doc());
    CHECK(cs.kind == SpellEffectKind::HpPool);
    CHECK(cs.condition == Condition::Blinded);
    CHECK_FALSE(cs.ends_on_damage);
    CHECK(cs.excluded_types == 0);
    CHECK(cs.expiry == 1); CHECK(cs.duration_rounds == 1);   // proposal 7.2f: end of the caster's next turn ~ 1 round
    CHECK(cs.area == rules5e::AreaShape::Cone);

    const auto dm = def_of(dispel_doc());
    CHECK(dm.kind == SpellEffectKind::Dispel);
    CHECK(dm.dispel_dc_base == 10);
    Json dm12 = dispel_doc(); dm12["effects"][0]["check"]["dc_base"] = 12;
    CHECK(def_of(dm12).dispel_dc_base == 12);

    const auto rv = def_of(revivify_doc(true));
    CHECK(rv.kind == SpellEffectKind::Revive);
    CHECK(rv.revive_within_rounds == 10);
    CHECK(rv.material_cost_cp == 30000);
    CHECK(rv.material_consumed);
    Json rv3 = revivify_doc(false); rv3["effects"][0]["within_rounds"] = 3;
    CHECK(def_of(rv3).revive_within_rounds == 3);
    Json rv0 = revivify_doc(false); rv0["effects"][0].erase("within_rounds");
    CHECK(def_of(rv0).revive_within_rounds == 10);

    const auto lr = def_of(restoration_doc());
    CHECK(lr.kind == SpellEffectKind::EndCondition);
    CHECK(lr.ends_conditions == (rules5e::bit(Condition::Blinded) | rules5e::bit(Condition::Deafened) |
                                 rules5e::bit(Condition::Paralyzed) | rules5e::bit(Condition::Poisoned)));
    CHECK(lr.range_ft == 5);

    const auto aa = def_of(acid_arrow_doc());
    CHECK(aa.kind == SpellEffectKind::SpellAttack);
    CHECK(aa.upcast_dice == 1);
    REQUIRE(aa.extra_effects.size() == 1);
    const auto& later = aa.extra_effects[0];
    CHECK(later.kind == SpellEffectKind::DelayedDamage);
    CHECK(later.affected_only);
    CHECK(later.dice.count == 2); CHECK(later.dice.sides == 4);
    CHECK(later.damage_type == rules5e::DamageType::Acid);
    CHECK(later.expiry == 2);
    Json timed = spell("acid_later", 2, Json::array({{{"kind", "delayed_damage"}, {"damage", {{"dice", "2d4"}, {"type", "acid"}}},
                                                      {"expiry", {{"kind", "rounds"}, {"n", 2}}}}}));
    const auto dl = def_of(timed);
    CHECK(dl.kind == SpellEffectKind::DelayedDamage);
    CHECK(dl.expiry == 1); CHECK(dl.duration_rounds == 2);

    const auto hm = def_of(heat_metal_doc());
    CHECK(hm.kind == SpellEffectKind::SustainedDamage);
    CHECK(hm.dice.count == 2); CHECK(hm.dice.sides == 8);
    CHECK(hm.damage_type == rules5e::DamageType::Fire);
    CHECK(hm.concentration);
    CHECK(hm.upcast_dice == 1);
    CHECK(hm.casting_time == rules5e::CastingTime::BonusAction);
    CHECK(hm.expiry == 1); CHECK(hm.duration_rounds == 10);

    // Spells without the new kinds keep expiry 0 (nothing in older content moves).
    CHECK(def_of(hold_doc()).expiry == 0);
    CHECK(def_of(load_json("content/packs/srd51.core/spells/magic_missile.json")).expiry == 0);
}

TEST_CASE("R-G15 loader: bad shapes are load errors") {
    auto with = [](Json j, const char* key, Json v) { j["effects"][0][key] = std::move(v); return j; };
    auto without = [](Json j, const char* key) { j["effects"][0].erase(key); return j; };
    CHECK(error_of(without(sleep_doc(), "dice")) == "effect dice invalid");
    CHECK(error_of(with(sleep_doc(), "dice", "7")) == "effect dice invalid");   // a flat value is not a pool roll
    CHECK(error_of(without(sleep_doc(), "condition")) == "effect condition invalid");
    CHECK(error_of(with(sleep_doc(), "ends_on_damage", 1)) == "effect ends_on_damage must be a boolean");
    CHECK(error_of(with(sleep_doc(), "excludes_creature_type", Json::array())) == "effect excludes_creature_type invalid");
    CHECK(error_of(with(sleep_doc(), "excludes_creature_type", {"ghost"})) == "effect excludes_creature_type invalid");
    Json inst = sleep_doc(); inst["duration"] = {{"kind", "instantaneous"}};
    CHECK(error_of(inst) == "hp_pool needs a rounds, minutes or hours duration");
    Json hm_inst = heat_metal_doc(); hm_inst["duration"] = {{"kind", "until_dispelled"}};
    CHECK(error_of(hm_inst) == "sustained_damage needs a rounds, minutes or hours duration");
    Json tg = sleep_doc(); tg["targets"] = {{"count", 2}};
    CHECK(error_of(tg) == "targets not allowed on a hp_pool effect (one target, per dart/beam, or self)");
    // dispel reads counter's check object, with counter's validation: required, exact ability / plus, dc_base 1..99.
    const std::string bad_check = "effect check must be {ability: spellcasting, dc_base, plus: spell_level}";
    CHECK(error_of(without(dispel_doc(), "check")) == bad_check);
    CHECK(error_of(with(without(dispel_doc(), "check"), "dc_base", 12)) == bad_check);   // the bare integer is gone
    for (int dc : {0, 100}) {
        Json j = dispel_doc(); j["effects"][0]["check"]["dc_base"] = dc;
        CHECK(error_of(j) == bad_check);
    }
    Json no_base = dispel_doc(); no_base["effects"][0]["check"].erase("dc_base");
    CHECK(error_of(no_base) == bad_check);
    Json wrong_ability = dispel_doc(); wrong_ability["effects"][0]["check"]["ability"] = "int";
    CHECK(error_of(wrong_ability) == bad_check);
    Json wrong_plus = dispel_doc(); wrong_plus["effects"][0]["check"]["plus"] = "slot_level";
    CHECK(error_of(wrong_plus) == bad_check);
    CHECK(error_of(with(revivify_doc(false), "within_rounds", 0)) == "effect within_rounds invalid");
    CHECK(error_of(with(restoration_doc(), "conditions", Json::array())) == "effect conditions invalid");
    CHECK(error_of(with(restoration_doc(), "conditions", {"disease"})) == "effect conditions invalid");
    CHECK(error_of(without(heat_metal_doc(), "damage")) == "effect damage invalid");
    CHECK(error_of(with(heat_metal_doc(), "expiry", {{"kind", "rounds"}, {"n", 1}})) ==
          "effect expiry is only for delayed_damage (sustained_damage lasts the spell's duration)");
    Json dl = spell("acid_later", 2, Json::array({{{"kind", "delayed_damage"}, {"damage", {{"dice", "2d4"}, {"type", "acid"}}}}}));
    CHECK(error_of(dl) == "effect expiry must be {kind: rounds, n} or {kind: until_end_of_target_next_turn}");
    CHECK(error_of(with(dl, "expiry", {{"kind", "rounds"}, {"n", 0}})) == "effect expiry n invalid");
    // Only delayed_damage rides another effect.
    for (const char* k : {"hp_pool", "dispel", "revive", "end_condition", "sustained_damage"}) {
        Json j = acid_arrow_doc();
        j["effects"][1] = {{"kind", k}, {"dice", "1d8"}, {"condition", "blinded"}, {"conditions", {"blinded"}},
                           {"damage", {{"dice", "1d8"}, {"type", "fire"}}},
                           {"check", {{"ability", "spellcasting"}, {"dc_base", 10}, {"plus", "spell_level"}}}};
        CHECK(error_of(j) == std::string("effects[1]: ") + k + " is only allowed as the first effect");
    }
    // Upcast dice are allowed on the new damage / pool kinds only.
    Json up = dispel_doc(); up["scaling"] = {{"upcast", {{"dice", 1}}}};
    CHECK(error_of(up) == "scaling.upcast.dice not allowed on a dispel effect");
}

TEST_CASE("R-G15 sim: Sleep through an aimed area (lowest HP first, undead excluded), Wake by the AI, replay") {
    const auto lib = make_lib();
    // Caster far from a cluster: hp 7, 10, 20 goblins and a 13 hp skeleton. A 30-point pool takes 7 and 10 (13 left; 20 does
    // not fit; the skeleton is undead and skipped).
    Fight f(lib, {{kWizard, 2, 2}, {"test.monster.gob7", 12, 12}, {"test.monster.gob10", 13, 12}, {"test.monster.gob20", 12, 13},
                  {"test.monster.bones", 13, 13}});
    const CombatantId wiz = f.id(0), g7 = f.id(1), g10 = f.id(2), g20 = f.id(3), sk = f.id(4);
    f.advance_to(wiz);
    Command cmd = f.cast_cmd("test.spell.sleep", 1, 0);
    cmd.aim = true;
    cmd.to = {12, 12};
    const auto r = f.settle(f.c->submit(cmd));
    INFO(r.error);
    REQUIRE(r.ok);
    CHECK(f.who(g7).has(Condition::Unconscious));
    CHECK(f.who(g10).has(Condition::Unconscious));
    CHECK_FALSE(f.who(g20).has(Condition::Unconscious));
    CHECK_FALSE(f.who(sk).has(Condition::Unconscious));
    CHECK(f.who(wiz).spell_slots[1] == 3);
    int sleepers = 0;
    for (const auto& e : f.c->state().effects) sleepers += e.ends_on_damage && e.timed.kind == rules5e::Expiry::Rounds;
    CHECK(sleepers == 2);
    REQUIRE(f.c->log().back().to);   // the aim tile is logged, as for other area spells
    f.check_replay();

    // The awake goblin stands next to both sleepers: its AI turn shakes the first one awake (special 5, the action).
    f.next_turn_of(g20);
    const Command wake = next_command(*f.c, wiz);
    REQUIRE(wake.kind == CommandKind::Rules);
    CHECK(wake.request.kind == ActionKind::Attack);
    CHECK(wake.request.special == 5);
    const CombatantId woken = wake.request.target;
    CHECK((woken == g7 || woken == g10));
    REQUIRE(f.settle(f.c->submit(wake)).ok);
    CHECK_FALSE(f.who(woken).has(Condition::Unconscious));
    CHECK(f.who(woken == g7 ? g10 : g7).has(Condition::Unconscious));
    CHECK_FALSE(f.c->state().budget.action);
    // The action is spent: no second wake this turn.
    const Command after = next_command(*f.c, wiz);
    CHECK_FALSE((after.kind == CommandKind::Rules && after.request.special == 5));
    f.check_replay();
}

TEST_CASE("R-G15 sim: Color Spray cone from the caster; Wake needs 5 ft (sim refuses farther)") {
    const auto lib = make_lib();
    Fight f(lib, {{kWizard, 5, 5}, {"test.monster.gob7", 7, 5}, {"test.monster.gob60", 8, 5}, {"test.monster.gob10", 20, 20}});
    const CombatantId wiz = f.id(0), g7 = f.id(1);
    f.advance_to(wiz);
    Command cmd = f.cast_cmd("test.spell.color_spray", 1, 0);
    cmd.aim = true;
    cmd.to = {7, 5};
    REQUIRE(f.settle(f.c->submit(cmd)).ok);
    CHECK(f.who(g7).has(Condition::Blinded));   // the cone (3 tiles toward the aim) catches it; the 60 hp goblin does not fit
    CHECK_FALSE(f.who(f.id(2)).has(Condition::Blinded));
    CHECK_FALSE(f.who(f.id(3)).has(Condition::Blinded));   // outside the cone
    f.check_replay();
    // Wake (special 5) from farther than 5 ft is refused by the sim before rules see it.
    f.next_turn_of(f.id(3));
    Command w;
    w.request = {f.id(3), ActionKind::Attack, g7};
    w.request.special = 5;
    const auto wr = f.c->submit(w);
    CHECK_FALSE(wr.ok);
    CHECK(wr.error == "target not within 5 ft");
}

TEST_CASE("R-G15 sim: Heat Metal is sustained by the AI with a bonus action on the next turn; log_hash folds sustain") {
    const auto lib = make_lib();
    Fight f(lib, {{kWizard, 5, 5}, {"test.monster.gob60", 12, 5}});
    const CombatantId wiz = f.id(0), gob = f.id(1);
    f.advance_to(wiz);
    REQUIRE(f.cast("test.spell.heat_metal", 2, gob).ok);
    CHECK(f.who(wiz).concentration_spell == spell_id_of("test.spell.heat_metal"));
    const int hp_after_cast = f.who(gob).hp;
    CHECK(hp_after_cast < 60);
    // Not on the turn of the cast: the AI does not offer it (and rules would refuse).
    const Command now = next_command(*f.c, gob);
    CHECK_FALSE((now.kind == CommandKind::Rules && now.request.sustain));
    Command bad = f.cast_cmd("test.spell.heat_metal", 0, gob, {gob});
    bad.request.sustain = true;
    CHECK(f.c->submit(bad).error == "sustain takes a single target and no aim");

    f.next_turn_of(wiz);
    const Command again = next_command(*f.c, gob);
    REQUIRE(again.kind == CommandKind::Rules);
    CHECK(again.request.kind == ActionKind::CastSpell);
    CHECK(again.request.sustain);
    CHECK(again.request.target == gob);
    const int slots = f.who(wiz).spell_slots[2];
    const int resolved = f.count(EventKind::SpellResolved);
    REQUIRE(f.settle(f.c->submit(again)).ok);
    CHECK(f.who(gob).hp < hp_after_cast);
    CHECK(f.who(wiz).spell_slots[2] == slots);   // no slot
    CHECK(f.count(EventKind::SpellResolved) == resolved + 1);
    CHECK_FALSE(f.c->state().budget.bonus_action);
    const Command twice = next_command(*f.c, gob);
    CHECK_FALSE((twice.kind == CommandKind::Rules && twice.request.sustain));
    CHECK(f.c->log().back().step.request.sustain);
    f.check_replay();

    // log_hash folds the flag: the same log with `sustain` cleared hashes differently (and no longer replays as a sustain).
    ecs::Registry r2;
    auto plain = replay(r2, f.chunk, lib, f.seed, std::vector<LogEntry>(f.c->log().begin(), f.c->log().end() - 1));
    REQUIRE(plain);
    CHECK(plain->log_hash() != f.c->log_hash());
}

TEST_CASE("R-G15 sim: Acid Arrow's delayed rider lands at the end of the target's next turn after a hit") {
    const auto lib = make_lib();
    bool hit_seen = false;
    for (uint64_t seed = 1; seed <= 40 && !hit_seen; ++seed) {
        Fight f(lib, {{kWizard, 5, 5}, {"test.monster.gob60", 12, 5}}, seed);
        const CombatantId wiz = f.id(0), gob = f.id(1);
        f.advance_to(wiz);
        REQUIRE(f.cast("test.spell.acid_arrow", 2, gob).ok);
        bool hit = false;
        for (const auto& e : f.c->events())
            hit = hit || (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == wiz && (e.rules.c & 1));
        int pending = 0;
        for (const auto& e : f.c->state().effects) pending += e.damage_when == 1 && e.target == gob;
        CHECK(pending == (hit ? 1 : 0));   // the rider only follows a hit
        if (!hit) continue;
        hit_seen = true;
        const int hp = f.who(gob).hp;
        const int dealt = f.count(EventKind::DamageDealt, gob);
        f.next_turn_of(gob);   // the target's next turn starts ...
        CHECK(f.who(gob).hp == hp);
        f.end_turn();          // ... and ends: 2d4 acid
        CHECK(f.who(gob).hp < hp);
        CHECK(f.count(EventKind::DamageDealt, gob) == dealt + 1);
        pending = 0;
        for (const auto& e : f.c->state().effects) pending += e.damage_when == 1;
        CHECK(pending == 0);
        f.check_replay();
    }
    CHECK(hit_seen);
}

TEST_CASE("R-G15 sim: Dispel Magic and Lesser Restoration end a held creature's paralysis") {
    const auto lib = make_lib();
    Fight f(lib, {{kWizard, 5, 5}, {"test.monster.gob60", 6, 5}, {"test.monster.gob20", 4, 5}});
    const CombatantId wiz = f.id(0), a = f.id(1), b = f.id(2);
    f.advance_to(wiz);
    REQUIRE(f.cast("test.spell.hold", 2, a).ok);
    REQUIRE(f.who(a).has(Condition::Paralyzed));
    f.next_turn_of(wiz);
    REQUIRE(f.cast("test.spell.dispel_magic", 3, a).ok);   // level 2 <= slot 3: ends without a check
    CHECK_FALSE(f.who(a).has(Condition::Paralyzed));
    CHECK(f.who(wiz).concentration_spell == 0);
    CHECK(f.count(EventKind::SpellCheckRolled) == 0);
    f.next_turn_of(wiz);
    REQUIRE(f.cast("test.spell.hold", 2, b).ok);
    REQUIRE(f.who(b).has(Condition::Paralyzed));
    f.next_turn_of(wiz);
    const int removed = f.count(EventKind::ConditionRemoved, b);
    REQUIRE(f.cast("test.spell.lesser_restoration", 2, b).ok);   // touch: adjacent
    CHECK_FALSE(f.who(b).has(Condition::Paralyzed));
    CHECK(f.count(EventKind::ConditionRemoved, b) == removed + 1);
    f.check_replay();
}

TEST_CASE("R-G15 sim: Revivify a creature that died this encounter; material cost and an occupied space are refused") {
    const auto lib = make_lib();
    Fight f(lib, {{kWizard, 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob60", 20, 20}});
    const CombatantId wiz = f.id(0), gob = f.id(1);
    f.advance_to(wiz);
    REQUIRE(f.cast("srd51.spell.magic_missile", 1, gob).ok);
    REQUIRE(f.who(gob).dead);
    CHECK(f.who(gob).died_round == f.c->state().round);
    f.next_turn_of(wiz);
    // Priced, consumed material (300 gp diamonds) with no components.m.item: unsatisfiable, so the cast is refused.
    CHECK_FALSE(f.cast("test.spell.revivify", 3, gob).ok);
    // The dead creature's tile is free to stand on; a creature there blocks the revival.
    Command st;
    st.kind = CommandKind::Step;
    st.to = {6, 5};
    REQUIRE(f.settle(f.c->submit(st)).ok);
    const auto blocked = f.cast("test.spell.revivify_free", 3, gob);
    CHECK_FALSE(blocked.ok);
    CHECK(blocked.error == "revive target's space is occupied");
    st.to = {5, 5};
    REQUIRE(f.settle(f.c->submit(st)).ok);
    const int healed = f.count(EventKind::Healed, gob);
    const auto ok = f.cast("test.spell.revivify_free", 3, gob);
    INFO(ok.error);
    REQUIRE(ok.ok);
    CHECK_FALSE(f.who(gob).dead);
    CHECK(f.who(gob).hp == 1);
    CHECK(f.count(EventKind::Healed, gob) == healed + 1);
    f.check_replay();
}
