// Change Shape P1 (contract v0.1.2) and P2 (the real couatl). MERGE reuses FormKind::Shapechanger.
// No new kind, no hash byte, schema stays 14. D10, D11, and the D13 possession guard stay deferred.
// The bite-in-form clause stays deferred (D4(e)). Gear absorbed or borne stays deferred (D8).
#include <doctest/doctest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/events.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::FormKind;

namespace {

Json load_json(const std::string& rel) {
    std::ifstream f(std::string(ENGINE_SOURCE_ROOT) + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

Json zap() {
    return {{"$schema", "spell@1"}, {"id", "test.spell.zap"}, {"name", "zap"}, {"level", 1}, {"school", "evocation"},
            {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
            {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}},
            {"effects", Json::array({{{"kind", "save"}, {"ability", "dex"}, {"on_success", "half"},
                                      {"damage", {{"dice", "2d6"}, {"type", "fire"}}}}})}};
}

Json goblin_base(const std::string& id) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["id"] = id;
    m["armor_class"]["note"] = "natural armor";
    return m;
}

// Celestial MERGE fixture. Explicit forms (the selector is a separate test). Own size Medium, type Celestial.
Json changer_doc(std::vector<std::string> forms) {
    Json m = goblin_base("test.change.couatl");
    m["$schema"] = "monster@5";
    m["name"] = "Change Shape Fixture";
    m["size"] = "medium";
    m["type"] = "celestial";
    m["challenge"] = {{"rating", "4"}, {"xp", 1100}};
    m["armor_class"]["value"] = 19;
    m["hit_points"]["average"] = 40;
    m["speed"] = {{"walk", 30}, {"fly", 90}};
    m["abilities"] = {{"str", 16}, {"dex", 12}, {"con", 18}, {"int", 17}, {"wis", 15}, {"cha", 14}};
    m["senses"] = {{"passive_perception", 12}};
    m["spellcasting"] = {{"ability", "wis"}, {"caster_level", 4}, {"slots", {{"1", 3}}}, {"spells", Json::array({"test.spell.zap"})}};
    m["actions"] = Json::array({
        {{"id", "sting"}, {"name", "Sting"}, {"kind", "melee_weapon_attack"}, {"to_hit", 30}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d6+3"}, {"type", "piercing"}}})},
         {"on_hit", Json::array({{{"kind", "save_condition"}, {"save", {{"ability", "con"}, {"dc", 13}}},
                                  {"apply", Json::array({{{"condition", "poisoned"}, {"duration", {{"kind", "rounds"}, {"n", 10}}}}})}}})}},
        {{"id", "glare"}, {"name", "Glare"}, {"kind", "save_effect"}, {"save", {{"ability", "wis"}, {"dc", 13}}},
         {"range_ft", 30}, {"damage", Json::array({{{"dice", "2d6"}, {"type", "psychic"}}})}},
    });
    Json forms_j = Json::array();
    for (const auto& id : forms) forms_j.push_back(id);
    m["traits"] = Json::array({
        {{"id", "change_shape"}, {"name", "Change Shape"}, {"description", "MERGE fixture."},
         {"effect", {{"kind", "change_shape"}, {"profile", "merge"}, {"cost", "action"}, {"forms", std::move(forms_j)}}}},
        {{"id", "mend"}, {"name", "Mend"}, {"description", "A feature the form blocks."},
         {"effect", {{"kind", "self_heal"}, {"dice", "1d8"}, {"cost", "action"}}}},
        {{"id", "magic_weapons"}, {"name", "Magic Weapons"}, {"description", "Weapon attacks are magical."},
         {"effect", {{"kind", "magic_weapons"}}}},
    });
    return m;
}

// Large beast. AC, speed, Str/Dex, a ranged grapple attack, a resistance, a vulnerability, darkvision.
Json form_a() {
    Json m = goblin_base("test.change.form_a");
    m["$schema"] = "monster@5";
    m["name"] = "Form A";
    m["size"] = "large";
    m["type"] = "beast";
    m["challenge"] = {{"rating", "1"}, {"xp", 200}};
    m["armor_class"]["value"] = 13;
    m["hit_points"]["average"] = 22;
    m["speed"] = {{"walk", 40}};
    m["abilities"] = {{"str", 14}, {"dex", 15}, {"con", 12}, {"int", 3}, {"wis", 12}, {"cha", 6}};
    m["damage_resistances"] = Json::array({"fire"});
    m["damage_vulnerabilities"] = Json::array({"cold"});
    m["senses"] = {{"darkvision", 60}, {"passive_perception", 13}};
    m["traits"] = Json::array();
    m["actions"] = Json::array({
        {{"id", "slam"}, {"name", "Slam"}, {"kind", "melee_weapon_attack"}, {"to_hit", 30}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d8+2"}, {"type", "bludgeoning"}}})}},
        {{"id", "spit"}, {"name", "Spit"}, {"kind", "ranged_weapon_attack"}, {"to_hit", 30}, {"range", Json::array({30, 120})},
         {"targets", 1}, {"damage", Json::array({{{"dice", "1d4"}, {"type", "acid"}}})},
         {"on_hit", Json::array({{{"kind", "grapple"}, {"escape_dc", 15}}})}},
    });
    return m;
}

Json form_b() {
    Json m = goblin_base("test.change.form_b");
    m["name"] = "Form B";
    m["type"] = "beast";
    m["size"] = "small";
    m["challenge"] = {{"rating", "1/4"}, {"xp", 50}};
    m["armor_class"]["value"] = 11;
    m["speed"] = {{"walk", 20}, {"climb", 20}};
    m["abilities"] = {{"str", 8}, {"dex", 16}, {"con", 10}, {"int", 2}, {"wis", 10}, {"cha", 4}};
    m["traits"] = Json::array();
    m["actions"] = Json::array({
        {{"id", "peck"}, {"name", "Peck"}, {"kind", "melee_weapon_attack"}, {"to_hit", 5}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}})}},
    });
    return m;
}

Json foe_doc() {
    Json m = goblin_base("test.change.foe");
    m["name"] = "Foe";
    m["armor_class"]["value"] = 10;
    m["hit_points"]["average"] = 30;
    m["traits"] = Json::array();
    m["actions"] = Json::array({
        {{"id", "club"}, {"name", "Club"}, {"kind", "melee_weapon_attack"}, {"to_hit", 30}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d8+4"}, {"type", "bludgeoning"}}})}},
    });
    return m;
}

world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns, std::vector<std::string> teams = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        std::string ov;
        if (i < teams.size() && !teams[i].empty()) ov = std::string("{\"team\":\"") + teams[i] + "\"}";
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ov});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}

ContentLibrary fixture_lib() {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add_spell(zap()));
    REQUIRE_FALSE(lib.add(form_a(), "monster"));
    REQUIRE_FALSE(lib.add(form_b(), "monster"));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    REQUIRE_FALSE(lib.add(changer_doc({"test.change.form_a", "test.change.form_b"}), "monster"));
    return lib;
}

struct Fight {
    ContentLibrary lib = fixture_lib();
    world::Chunk chunk = arena({{"test.change.couatl", {4, 4}}, {"test.change.foe", {5, 4}}}, {"", "foes"});
    ecs::Registry reg;
    std::optional<Combat> combat;
    CombatantId self = 0, foe = 0;
    uint32_t change_id = 0, mend_id = 0, glare_id = 0, zap_id = 0;
    Fight() {
        combat = Combat::create(reg, chunk, lib, 11);
        REQUIRE(combat);
        self = chunk.spawns[0].stable_id;
        foe = chunk.spawns[1].stable_id;
        const Archetype* a = lib.find("test.change.couatl");
        REQUIRE(a);
        for (const auto& [fid, ff] : a->form_features)
            if (ff.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) change_id = fid;
        for (std::size_t i = 0; i < a->features.size(); ++i)
            if (a->features[i].id == "mend") mend_id = uint32_t(i + 1);
        REQUIRE(a->save_actions.size() == 1);
        glare_id = a->save_actions[0].spell_id;
        zap_id = rules5e::spell_id_of("test.spell.zap");
        REQUIRE(change_id != 0);
        REQUIRE(mend_id != 0);
    }
    Combat& c() { return *combat; }
    rules5e::Combatant& who(CombatantId id) {
        for (auto& k : CombatTestAccess::state(c()).combatants)
            if (k.id == id) return k;
        REQUIRE(false);
        return CombatTestAccess::state(c()).combatants.front();
    }
    void end_turn() {
        Command e;
        e.request = {rules5e::current_actor(c().state()), ActionKind::EndTurn};
        REQUIRE(c().submit(e).ok);
    }
    void advance_to(CombatantId w) {
        for (int i = 0; i < 8 && rules5e::current_actor(c().state()) != w; ++i) end_turn();
        REQUIRE(rules5e::current_actor(c().state()) == w);
    }
    SubmitResult use_form(uint8_t form) {
        Command u;
        u.request = {self, ActionKind::UseFeature};
        u.request.feature_id = change_id;
        u.request.form_archetype = form;
        return c().submit(u);
    }
    bool offered(ActionKind kind, uint32_t spell) {
        for (const auto& q : c().legal_actions())
            if (q.kind == kind && q.spell_id == spell) return true;
        return false;
    }
};

// Id byte order. D4(f) drops goblin, spy, orc, berserker, boar, and giant_boar (bonus action or trait lost, or Relentless unlimited).
const char* kPool42[] = {
    "srd51.monster.ape", "srd51.monster.axe_beak", "srd51.monster.bandit", "srd51.monster.blood_hawk",
    "srd51.monster.bugbear", "srd51.monster.cat", "srd51.monster.commoner", "srd51.monster.crab",
    "srd51.monster.cultist", "srd51.monster.draft_horse", "srd51.monster.eagle", "srd51.monster.elephant",
    "srd51.monster.elk", "srd51.monster.giant_crab", "srd51.monster.giant_eagle", "srd51.monster.giant_elk",
    "srd51.monster.giant_goat", "srd51.monster.giant_owl", "srd51.monster.giant_scorpion",
    "srd51.monster.giant_sea_horse", "srd51.monster.giant_vulture", "srd51.monster.giant_wasp",
    "srd51.monster.goat", "srd51.monster.grimlock", "srd51.monster.guard", "srd51.monster.hobgoblin",
    "srd51.monster.kobold", "srd51.monster.merfolk", "srd51.monster.mule", "srd51.monster.owl",
    "srd51.monster.pony", "srd51.monster.raven", "srd51.monster.rhinoceros", "srd51.monster.riding_horse",
    "srd51.monster.scorpion", "srd51.monster.scout", "srd51.monster.stirge", "srd51.monster.thug",
    "srd51.monster.tribal_warrior", "srd51.monster.veteran", "srd51.monster.vulture", "srd51.monster.warhorse",
};

}  // namespace

TEST_CASE("Change Shape MERGE: the form replaces AC, speed, Str and Dex and attacks; hp and the rest stay own") {
    Fight f;
    auto& me = f.who(f.self);
    CHECK(me.ac == 19);
    CHECK(me.speed_ft == 30);
    CHECK(me.abilities[rules5e::Ability::Str] == 16);
    CHECK(me.abilities[rules5e::Ability::Dex] == 12);
    CHECK(me.abilities[rules5e::Ability::Con] == 18);
    CHECK(me.size == rules5e::Size::Medium);
    CHECK(me.creature_type == rules5e::CreatureType::Celestial);
    CHECK(me.attacks.size() == 1);
    CHECK(me.attacks[0].id == "sting");
    f.advance_to(f.self);
    auto tr = f.use_form(1);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    CHECK(me.form == FormKind::Shapechanger);
    CHECK(me.ac == 13);
    CHECK(me.speed_ft == 40);
    CHECK(me.fly_ft == 0);
    CHECK(me.abilities[rules5e::Ability::Str] == 14);
    CHECK(me.abilities[rules5e::Ability::Dex] == 15);
    CHECK(me.abilities[rules5e::Ability::Con] == 18);
    CHECK(me.abilities[rules5e::Ability::Int] == 17);
    CHECK(me.abilities[rules5e::Ability::Wis] == 15);
    CHECK(me.abilities[rules5e::Ability::Cha] == 14);
    CHECK(me.hp == 40);
    CHECK(me.hp_max == 40);
    CHECK(me.size == rules5e::Size::Medium);                         // D5
    CHECK(me.creature_type == rules5e::CreatureType::Celestial);     // D5
    REQUIRE(me.attacks.size() == 2);
    CHECK(me.attacks[0].id == "slam");
    CHECK(me.attacks[1].id == "spit");
    bool transformed = false;
    for (const auto& e : f.c().events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Transformed && e.rules.c == int32_t(FormKind::Shapechanger))
            transformed = true;
    CHECK(transformed);
}

TEST_CASE("Change Shape: one hit point pool, and death in the form reverts then the creature dies at 0") {
    Fight f;
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    f.end_turn();
    f.advance_to(f.foe);
    bool hurt = false;
    for (int n = 0; n < 12 && !hurt; ++n) {
        const int before = f.who(f.self).hp;
        Command atk;
        atk.request = {f.foe, ActionKind::Attack, f.self, 0};
        auto r = f.c().submit(atk);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (f.who(f.self).hp < before) hurt = true;
        else f.end_turn(), f.advance_to(f.foe);
    }
    REQUIRE(hurt);
    const int wounded = f.who(f.self).hp;
    CHECK(wounded > 0);
    CHECK(wounded < 40);
    f.end_turn();
    f.advance_to(f.self);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    REQUIRE(f.c().submit(rev).ok);
    CHECK(f.who(f.self).form == FormKind::None);
    CHECK(f.who(f.self).hp == wounded);   // the damage carried across the one pool
    f.end_turn();
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    f.end_turn();
    for (int n = 0; n < 30 && !f.who(f.self).dead; ++n) {
        if (rules5e::current_actor(f.c().state()) != f.foe) f.advance_to(f.foe);
        if (f.c().state().ended) break;
        Command atk;
        atk.request = {f.foe, ActionKind::Attack, f.self, 0};
        auto r = f.c().submit(atk);
        if (!r.ok) break;
        if (!f.who(f.self).dead && f.who(f.self).hp > 0) f.end_turn();
    }
    CHECK(f.who(f.self).dead);
    CHECK(f.who(f.self).hp == 0);
    CHECK(f.who(f.self).form == FormKind::None);
    bool reverted = false;
    for (const auto& e : f.c().events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Reverted && e.rules.actor == f.self && e.rules.b == 4)
            reverted = true;
    CHECK(reverted);
}

TEST_CASE("Change Shape: revert spends an action, a switch is one action, encounter end reverts with b=5") {
    Fight f;
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    CHECK_FALSE(f.c().state().budget.action);
    f.end_turn();
    f.advance_to(f.self);
    const auto before_switch = f.c().events().size();
    auto sw = f.use_form(2);
    REQUIRE_MESSAGE(sw.ok, sw.error);
    CHECK(f.who(f.self).form_archetype == 2);
    CHECK(f.who(f.self).ac == 11);
    CHECK(f.who(f.self).speed_ft == 20);
    CHECK(f.who(f.self).climb_ft == 20);
    CHECK(f.who(f.self).attacks[0].id == "peck");
    CHECK_FALSE(f.c().state().budget.action);   // the switch is the one action (D9)
    bool saw_revert = false, saw_transform = false;
    for (std::size_t i = before_switch; i < f.c().events().size(); ++i) {
        const auto& e = f.c().events()[i];
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::Reverted && e.rules.b == 3) saw_revert = true;
        if (e.rules.kind == EventKind::Transformed) saw_transform = true;
    }
    CHECK(saw_revert);
    CHECK(saw_transform);
    f.end_turn();
    f.advance_to(f.self);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    const auto before_revert = f.c().events().size();
    auto back = f.c().submit(rev);
    REQUIRE_MESSAGE(back.ok, back.error);
    CHECK_FALSE(f.c().state().budget.action);
    CHECK(f.who(f.self).form == FormKind::None);
    CHECK(f.who(f.self).ac == 19);
    bool reason3 = false;
    for (std::size_t i = before_revert; i < f.c().events().size(); ++i) {
        const auto& e = f.c().events()[i];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Reverted && e.rules.b == 3) reason3 = true;
    }
    CHECK(reason3);
    f.end_turn();
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    // Drop the foe. The encounter ends and the form reverts with reason 5 (D15).
    f.who(f.foe).hp = 1;
    f.end_turn();
    f.advance_to(f.self);
    bool ended = false;
    for (int n = 0; n < 20 && !f.c().state().ended; ++n) {
        Command atk;
        atk.request = {f.self, ActionKind::Attack, f.foe, 0};
        auto r = f.c().submit(atk);
        if (!r.ok) { f.end_turn(); f.advance_to(f.self); continue; }
        if (!f.c().state().ended) f.end_turn();
    }
    CHECK(f.c().state().ended);
    CHECK(f.who(f.self).form == FormKind::None);
    for (const auto& e : f.c().events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Reverted && e.rules.actor == f.self && e.rules.b == 5)
            ended = true;
    CHECK(ended);
    (void)ended;
}

TEST_CASE("Change Shape union: a resistance, a vulnerability and a sense the creature lacks are gained, then restored") {
    Fight f;
    CHECK(f.who(f.self).resistances == 0);
    CHECK(f.who(f.self).vulnerabilities == 0);
    CHECK(f.who(f.self).darkvision_ft == 0);
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    CHECK((f.who(f.self).resistances & rules5e::bit(rules5e::DamageType::Fire)) != 0);
    CHECK((f.who(f.self).vulnerabilities & rules5e::bit(rules5e::DamageType::Cold)) != 0);
    CHECK(f.who(f.self).darkvision_ft == 60);
    f.end_turn();
    f.advance_to(f.self);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    REQUIRE(f.c().submit(rev).ok);
    CHECK(f.who(f.self).resistances == 0);
    CHECK(f.who(f.self).vulnerabilities == 0);
    CHECK(f.who(f.self).darkvision_ft == 0);

    // Every other union family, on a form the fixture does not share with the other tests.
    Json rich = form_a();
    rich["id"] = "test.change.union";
    rich["damage_immunities"] = Json::array({"poison"});
    rich["condition_immunities"] = Json::array({"frightened"});
    rich["saving_throws"] = {{"con", true}};
    rich["senses"] = {{"darkvision", 60}, {"blindsight", 10}, {"tremorsense", 30}, {"truesight", 15}, {"passive_perception", 13}};
    rich["traits"] = Json::array({
        {{"id", "pack_tactics"}, {"name", "Pack Tactics"}, {"effect", {{"kind", "pack_tactics"}}}},
        {{"id", "keen_smell"}, {"name", "Keen Smell"}, {"effect", {{"kind", "keen_senses"}, {"senses", Json::array({"smell"})}}}},
        {{"id", "damage_resistances_nonmagical"}, {"name", "Damage Resistances"},
         {"effect", {{"kind", "damage_mitigation"}, {"resistances", {{"nonmagical", Json::array({"bludgeoning"})}}}}}},
        {{"id", "rampage"}, {"name", "Rampage"},
         {"effect", {{"kind", "on_event"}, {"on", "reduce_to_zero"}, {"limit", "once_per_turn"},
                     {"when", Json::array({{{"kind", "weapon"}, {"is", "melee"}}})},
                     {"effects", Json::array({{{"kind", "bonus_attack"}, {"attack_id", "slam"}}})}}}},
    });
    rich["reactions"] = Json::array({
        {{"id", "shrug"}, {"name", "Shrug"}, {"window", {{"kind", "damaged"}}},
         {"effect", {{"kind", "reduce_damage"}, {"fraction", "half"}}}},
    });
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add_spell(zap()));
    auto added = lib.add(rich, "monster");
    REQUIRE_MESSAGE(!added, added.value_or(""));
    Json who = changer_doc({"test.change.union"});
    who["id"] = "test.change.union_who";
    added = lib.add(who, "monster");
    REQUIRE_MESSAGE(!added, added.value_or(""));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    auto chunk = arena({{"test.change.union_who", {4, 4}}, {"test.change.foe", {5, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 3);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    auto actor = [&]() -> rules5e::Combatant& {
        for (auto& k : CombatTestAccess::state(*combat).combatants)
            if (k.id == self) return k;
        REQUIRE(false);
        return CombatTestAccess::state(*combat).combatants.front();
    };
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    const Archetype* form = lib.find("test.change.union");
    REQUIRE(form);
    CHECK(actor().roll_traits.empty());
    CHECK(actor().reactions.empty());
    CHECK(actor().bonus_attacks.empty());
    CHECK((actor().immunities & rules5e::bit(rules5e::DamageType::Poison)) == 0);
    CHECK((actor().condition_immunities & rules5e::bit(rules5e::Condition::Frightened)) == 0);
    CHECK((actor().save_proficiencies & rules5e::bit(rules5e::Ability::Con)) == 0);
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = 1;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    CHECK((actor().resistances & rules5e::bit(rules5e::DamageType::Fire)) != 0);
    CHECK((actor().immunities & rules5e::bit(rules5e::DamageType::Poison)) != 0);
    CHECK((actor().vulnerabilities & rules5e::bit(rules5e::DamageType::Cold)) != 0);
    CHECK((actor().resist_qualified.nonmagical & rules5e::bit(rules5e::DamageType::Bludgeoning)) != 0);
    CHECK((actor().condition_immunities & rules5e::bit(rules5e::Condition::Frightened)) != 0);
    CHECK((actor().save_proficiencies & rules5e::bit(rules5e::Ability::Con)) != 0);
    CHECK(actor().darkvision_ft == 60);
    CHECK(actor().blindsight_ft == 10);
    CHECK(actor().tremorsense_ft == 30);
    CHECK(actor().truesight_ft == 15);
    CHECK((actor().keen_senses & rules5e::kKeenSmell) != 0);
    REQUIRE(actor().roll_traits.size() == form->base.roll_traits.size());
    CHECK(actor().roll_traits[0].when == form->base.roll_traits[0].when);
    REQUIRE(actor().reactions.size() == 1);
    CHECK(actor().reactions[0].id == form->base.reactions[0].id);
    CHECK(actor().bonus_attacks.size() == form->base.bonus_attacks.size());
    bool rampage = false;
    for (const auto& t : actor().triggers)
        for (const auto& e : t.effects)
            rampage = rampage || e.kind == rules5e::RiderKind::BonusAttack;
    CHECK(rampage);
    Command end;
    end.request = {self, ActionKind::EndTurn};
    REQUIRE(combat->submit(end).ok);
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    Command rev2;
    rev2.request = {self, ActionKind::Revert};
    auto reverted = combat->submit(rev2);
    REQUIRE_MESSAGE(reverted.ok, reverted.error);
    CHECK((actor().immunities & rules5e::bit(rules5e::DamageType::Poison)) == 0);
    CHECK((actor().resist_qualified.nonmagical & rules5e::bit(rules5e::DamageType::Bludgeoning)) == 0);
    CHECK((actor().condition_immunities & rules5e::bit(rules5e::Condition::Frightened)) == 0);
    CHECK((actor().save_proficiencies & rules5e::bit(rules5e::Ability::Con)) == 0);
    CHECK(actor().blindsight_ft == 0);
    CHECK(actor().tremorsense_ft == 0);
    CHECK(actor().truesight_ft == 0);
    CHECK(actor().keen_senses == 0);
    CHECK(actor().roll_traits.empty());
    CHECK(actor().reactions.empty());
    CHECK(actor().bonus_attacks.empty());
    rampage = false;
    for (const auto& t : actor().triggers)
        for (const auto& e : t.effects)
            rampage = rampage || e.kind == rules5e::RiderKind::BonusAttack;
    CHECK_FALSE(rampage);
}

TEST_CASE("Change Shape D7b: Magic Weapons is re-applied to the form's attacks and does not stay after revert") {
    Fight f;
    const Archetype* form = f.lib.find("test.change.form_a");
    REQUIRE(form);
    CHECK((form->base.attacks[0].qualifiers & rules5e::kAttackMagical) == 0);
    CHECK((f.who(f.self).attacks[0].qualifiers & rules5e::kAttackMagical) != 0);
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    REQUIRE(f.who(f.self).attacks.size() == 2);
    CHECK((f.who(f.self).attacks[0].qualifiers & rules5e::kAttackMagical) != 0);
    CHECK((f.who(f.self).attacks[1].qualifiers & rules5e::kAttackMagical) != 0);
    CHECK((form->base.attacks[0].qualifiers & rules5e::kAttackMagical) == 0);   // the form archetype itself is untouched
    f.end_turn();
    f.advance_to(f.self);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    REQUIRE(f.c().submit(rev).ok);
    REQUIRE(f.who(f.self).attacks.size() == 1);
    CHECK(f.who(f.self).attacks[0].id == "sting");
    CHECK((f.who(f.self).attacks[0].qualifiers & rules5e::kAttackMagical) != 0);
    // Control: the same form, without the magic_weapons trait, keeps non-magical rows.
    ContentLibrary plain;
    REQUIRE_FALSE(plain.add(form_a(), "monster"));
    REQUIRE_FALSE(plain.add(foe_doc(), "monster"));
    Json bare = changer_doc({"test.change.form_a"});
    bare["id"] = "test.change.plain";
    Json kept = Json::array();
    for (const Json& t : bare["traits"])
        if (t.value("id", "") != "magic_weapons") kept.push_back(t);
    bare["traits"] = std::move(kept);
    REQUIRE_FALSE(plain.add(bare, "monster"));
    auto chunk = arena({{"test.change.plain", {4, 4}}, {"test.change.foe", {5, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, plain, 9);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = 1;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    const auto* me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    REQUIRE(me->attacks.size() == 2);
    CHECK((me->attacks[0].qualifiers & rules5e::kAttackMagical) == 0);
    CHECK((me->attacks[1].qualifiers & rules5e::kAttackMagical) == 0);
}

TEST_CASE("Change Shape M1: a MERGE form does not fire the own attack's on_hit rider; revert does") {
    Fight f;
    uint32_t sting_rider = 0;
    for (const auto& t : f.who(f.self).triggers)
        for (const auto& p : t.when)
            if (p.kind == rules5e::PredicateKind::AttackIs && p.a == 0) sting_rider = t.id;
    REQUIRE(sting_rider != 0);
    auto saves_since = [&](std::size_t mark) {
        int n = 0;
        for (std::size_t i = mark; i < f.c().events().size(); ++i) {
            const auto& e = f.c().events()[i];
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == f.foe) ++n;
        }
        return n;
    };
    auto hit = [&](uint8_t attack) {
        for (int n = 0; n < 8; ++n) {
            f.advance_to(f.self);
            const auto mark = f.c().events().size();
            Command atk;
            atk.request = {f.self, ActionKind::Attack, f.foe, attack};
            auto r = f.c().submit(atk);
            REQUIRE_MESSAGE(r.ok, r.error);
            bool landed = false;
            for (std::size_t i = mark; i < f.c().events().size(); ++i) {
                const auto& e = f.c().events()[i];
                if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == f.self && (e.rules.c & 1))
                    landed = true;
            }
            if (landed) return mark;
            f.end_turn();
        }
        FAIL("attack did not hit");
        return f.c().events().size();
    };
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    for (const auto& t : f.who(f.self).triggers) CHECK(t.id != sting_rider);
    f.end_turn();
    const auto form_mark = hit(0);   // form attack 0 (slam), the slot the own sting rider used to index
    CHECK(saves_since(form_mark) == 0);
    f.end_turn();
    f.advance_to(f.self);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    REQUIRE(f.c().submit(rev).ok);
    bool restored = false;
    for (const auto& t : f.who(f.self).triggers)
        if (t.id == sting_rider) restored = true;
    CHECK(restored);
    f.end_turn();
    const auto own_mark = hit(0);
    CHECK(saves_since(own_mark) > 0);
}

TEST_CASE("Change Shape own-actions gate: save action and feature are refused, a real spell is not, Change Shape is never listed") {
    Fight f;
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    const auto& blocked = f.who(f.self).form_blocked_features;
    CHECK(std::find(blocked.begin(), blocked.end(), f.change_id) == blocked.end());
    CHECK(std::find(blocked.begin(), blocked.end(), f.mend_id) != blocked.end());
    CHECK(std::find(blocked.begin(), blocked.end(), f.glare_id) != blocked.end());
    CHECK(std::find(blocked.begin(), blocked.end(), f.zap_id) == blocked.end());
    f.end_turn();
    f.advance_to(f.self);
    CHECK_FALSE(f.c().can_use_feature(f.self, f.mend_id));
    CHECK(f.c().can_use_feature(f.self, f.change_id));
    CHECK_FALSE(f.offered(ActionKind::CastSpell, f.glare_id));
    CHECK(f.offered(ActionKind::CastSpell, f.zap_id));
    // The AI save-action probe reads the same list (driver.cpp). Glare is in reach and would be the action.
    Command ai = next_command(f.c(), f.foe);
    CHECK(ai.request.spell_id != f.glare_id);
    CHECK(ai.request.kind == ActionKind::Attack);
    Command glare;
    glare.request = {f.self, ActionKind::CastSpell, f.foe};
    glare.request.spell_id = f.glare_id;
    glare.request.targets = {f.foe};
    auto refused = f.c().submit(glare);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "feature not usable in this form");
    Command mend;
    mend.request = {f.self, ActionKind::UseFeature};
    mend.request.feature_id = f.mend_id;
    auto mend_no = f.c().submit(mend);
    CHECK_FALSE(mend_no.ok);
    CHECK(mend_no.error == "feature not usable in this form");
    Command cast;
    cast.request = {f.self, ActionKind::CastSpell, f.foe};
    cast.request.spell_id = f.zap_id;
    cast.request.spell_level = 1;
    cast.request.targets = {f.foe};
    auto casted = f.c().submit(cast);
    REQUIRE_MESSAGE(casted.ok, casted.error);
    f.end_turn();
    f.advance_to(f.self);
    auto sw = f.use_form(2);
    REQUIRE_MESSAGE(sw.ok, sw.error);   // Change Shape itself is not on the list
    f.end_turn();
    f.advance_to(f.self);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    REQUIRE(f.c().submit(rev).ok);
    CHECK(f.who(f.self).form_blocked_features.empty());
    f.end_turn();
    f.advance_to(f.self);
    CHECK(f.c().can_use_feature(f.self, f.mend_id));
    CHECK(f.offered(ActionKind::CastSpell, f.glare_id));
    Command mend2;
    mend2.request = {f.self, ActionKind::UseFeature};
    mend2.request.feature_id = f.mend_id;
    auto mended = f.c().submit(mend2);
    REQUIRE_MESSAGE(mended.ok, mended.error);
}

TEST_CASE("Change Shape pool: the fourteen exclusion groups stay out, and test 16 pins the 42 S1 ids in byte order") {
    ContentLibrary small;
    auto eligible = [](const std::string& id, const char* cr) {
        Json m = goblin_base(id);
        m["challenge"] = {{"rating", cr}, {"xp", 10}};
        m["traits"] = Json::array();
        m["actions"] = Json::array({
            {{"id", "slam"}, {"name", "Slam"}, {"kind", "melee_weapon_attack"}, {"to_hit", 3}, {"reach", 5}, {"targets", 1},
             {"damage", Json::array({{{"dice", "1d4"}, {"type", "bludgeoning"}}})}},
        });
        return m;
    };
    REQUIRE_FALSE(small.add(eligible("test.pool.m", "1/4"), "monster"));
    REQUIRE_FALSE(small.add(eligible("test.pool.a", "0"), "monster"));
    Json high = eligible("test.pool.high", "5");
    REQUIRE_FALSE(small.add(high, "monster"));
    Json dragon = eligible("test.pool.dragon", "1");
    dragon["type"] = "dragon";
    REQUIRE_FALSE(small.add(dragon, "monster"));
    Json caster = eligible("test.pool.caster", "1/4");
    caster["traits"] = Json::array({{{"id", "innate_spellcasting"}, {"name", "Innate Spellcasting"},
                                     {"description", "Descriptive only."}}});
    REQUIRE_FALSE(small.add(caster, "monster"));
    Json shaper = eligible("test.pool.shaper", "1/4");
    shaper["$schema"] = "monster@5";
    shaper["shapechanger"] = true;
    shaper["traits"] = Json::array({{{"id", "shapechanger"}, {"name", "Shapechanger"},
                                     {"effect", {{"kind", "shapechanger"}, {"forms", Json::array({{{"id", "wolf"}, {"name", "Wolf"}}})}}}}});
    REQUIRE_FALSE(small.add(shaper, "monster"));
    Json swarm = eligible("test.pool.swarm", "1/4");
    swarm["tags"] = Json::array({"swarm"});
    REQUIRE_FALSE(small.add(swarm, "monster"));
    Json special = eligible("test.pool.special", "1/4");
    special["actions"].push_back({{"id", "ink"}, {"name", "Ink"}, {"kind", "special"}, {"description", "A non-attack action."}});
    REQUIRE_FALSE(small.add(special, "monster"));
    Json parry = eligible("test.pool.parry", "1/4");
    parry["reactions"] = Json::array({{{"id", "parry"}, {"name", "Parry"},
                                       {"description", "Printed, left uncompiled."},
                                       {"window", {{"kind", "being_hit"}, {"melee_only", true}}},
                                       {"effect", {{"kind", "ac_bonus"}, {"bonus", 2}}}}});
    REQUIRE_FALSE(small.add(parry, "monster"));
    Json aggressive = eligible("test.pool.aggressive", "1/4");
    aggressive["traits"] = Json::array({{{"id", "aggressive"}, {"name", "Aggressive"}, {"effect", {{"kind", "aggressive"}}}}});
    REQUIRE_FALSE(small.add(aggressive, "monster"));
    Json reckless = eligible("test.pool.reckless", "1/4");
    reckless["traits"] = Json::array({{{"id", "reckless"}, {"name", "Reckless"}, {"effect", {{"kind", "reckless_attack"}}}}});
    REQUIRE_FALSE(small.add(reckless, "monster"));
    Json nimble = eligible("test.pool.nimble", "1/4");
    nimble["traits"] = Json::array({{{"id", "nimble_escape"}, {"name", "Nimble Escape"},
                                     {"effect", {{"kind", "bonus_action_grant"}, {"actions", Json::array({"disengage", "hide"})}}}}});
    REQUIRE_FALSE(small.add(nimble, "monster"));
    Json relentless = eligible("test.pool.relentless", "1/4");
    relentless["$schema"] = "monster@5";
    relentless["traits"] = Json::array({{{"id", "relentless"}, {"name", "Relentless"},
                                         {"effect", {{"kind", "on_event"}, {"on", "drop_to_zero"},
                                                     {"when", Json::array({{{"kind", "damage_at_most"}, {"amount", 7}}})},
                                                     {"usage", {{"kind", "per_rest"}, {"uses", 1}, {"rest", "short"}}},
                                                     {"effects", Json::array({{{"kind", "set_hp"}, {"value", 1}}})}}}}});
    REQUIRE_FALSE(small.add(relentless, "monster"));
    Json recharge = eligible("test.pool.recharge", "1/4");
    recharge["$schema"] = "monster@5";
    recharge["traits"] = Json::array({{{"id", "breath"}, {"name", "Breath"},
                                       {"effect", {{"kind", "on_event"}, {"on", "turn_start"},
                                                   {"usage", {{"kind", "recharge"}, {"min", 5}}},
                                                   {"effects", Json::array({{{"kind", "heal_self"}, {"value", 1}}})}}}}});
    REQUIRE_FALSE(small.add(recharge, "monster"));
    Json pooled = eligible("test.pool.pool", "1/4");
    pooled["$schema"] = "monster@5";
    pooled["resources"] = Json::array({{{"id", "fury"}, {"max", 1}, {"recharge", "short_rest"}}});
    pooled["traits"] = Json::array({{{"id", "spend"}, {"name", "Spend"},
                                     {"effect", {{"kind", "on_event"}, {"on", "turn_start"},
                                                 {"usage", {{"kind", "pool"}, {"pool", "fury"}, {"cost", 1}}},
                                                 {"effects", Json::array({{{"kind", "heal_self"}, {"value", 1}}})}}}}});
    REQUIRE_FALSE(small.add(pooled, "monster"));
    Json per_day = eligible("test.pool.per_day", "1/4");
    per_day["actions"][0]["usage"] = {{"kind", "per_day"}, {"uses", 1}};
    REQUIRE_FALSE(small.add(per_day, "monster"));
    Json res_only = eligible("test.pool.resources", "1/4");
    res_only["resources"] = Json::array({{{"id", "fury"}, {"max", 1}, {"recharge", "long_rest"}}});
    REQUIRE_FALSE(small.add(res_only, "monster"));
    Json bite = eligible("test.pool.bite", "1/4");
    bite["actions"] = Json::array({
        {{"id", "bite"}, {"name", "Bite"}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}})}},
    });
    REQUIRE_FALSE(small.add(bite, "monster"));
    Json who = goblin_base("test.pool.who");
    who["$schema"] = "monster@5";
    who["type"] = "celestial";
    who["challenge"] = {{"rating", "1"}, {"xp", 200}};
    who["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                  {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                              {"select", {{"types", Json::array({"humanoid", "beast"})}, {"max_cr", "own"}}}}}}});
    who["actions"] = Json::array({
        {{"id", "sting"}, {"name", "Sting"}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}})}},
    });
    REQUIRE_FALSE(small.add(who, "monster"));
    const Archetype* a = small.find("test.pool.who");
    REQUIRE(a);
    const Archetype::FormFeature* ff = nullptr;
    for (const auto& [fid, feat] : a->form_features)
        if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) ff = &feat;
    REQUIRE(ff);
    REQUIRE(ff->forms.size() == 2);
    CHECK(ff->forms[0] == "test.pool.a");
    CHECK(ff->forms[1] == "test.pool.m");
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.nimble") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.aggressive") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.reckless") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.relentless") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.recharge") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.pool") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.per_day") == ff->forms.end());
    CHECK(std::find(ff->forms.begin(), ff->forms.end(), "test.pool.resources") == ff->forms.end());
    CHECK(a->feature_specs.at(1).form_choices == 2);

    // The pack: 112 humanoid or beast at CR 4 or lower, 88 after D4, then D4(e) and D4(f) leave 42.
    ContentLibrary pack;
    const auto dir = std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core";
    REQUIRE(pack.load_pack(dir.string()).empty());
    Json couatl = who;
    couatl["id"] = "test.change.selector";
    couatl["challenge"] = {{"rating", "4"}, {"xp", 1100}};
    REQUIRE_FALSE(pack.add(couatl, "monster"));
    const Archetype* sel = pack.find("test.change.selector");
    REQUIRE(sel);
    const Archetype::FormFeature* pool = nullptr;
    uint32_t fid = 0;
    for (const auto& [id, feat] : sel->form_features)
        if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) { pool = &feat; fid = id; }
    REQUIRE(pool);
    REQUIRE(pool->forms.size() == 42);
    CHECK(sel->feature_specs.at(fid).form_choices == 42);
    for (std::size_t i = 0; i < 42; ++i) CHECK(pool->forms[i] == kPool42[i]);
    for (const std::string& id : pool->forms) {
        const Archetype* form = pack.find(id);
        REQUIRE(form);
        CHECK((form->base.creature_type == rules5e::CreatureType::Humanoid || form->base.creature_type == rules5e::CreatureType::Beast));
        CHECK(form->cr_x8 >= 0);
        CHECK(form->cr_x8 <= sel->cr_x8);
    }
    for (const char* absent : {"srd51.monster.acolyte", "srd51.monster.cult_fanatic", "srd51.monster.deep_gnome",
                               "srd51.monster.drow", "srd51.monster.druid", "srd51.monster.priest",
                               "srd51.monster.wereboar", "srd51.monster.wererat", "srd51.monster.weretiger",
                               "srd51.monster.werewolf", "srd51.monster.swarm_of_rats", "srd51.monster.bandit_captain",
                               "srd51.monster.noble", "srd51.monster.knight", "srd51.monster.wolf",
                               "srd51.monster.goblin", "srd51.monster.spy", "srd51.monster.orc",
                               "srd51.monster.berserker", "srd51.monster.boar", "srd51.monster.giant_boar"})
        CHECK(std::find(pool->forms.begin(), pool->forms.end(), absent) == pool->forms.end());
}

TEST_CASE("Change Shape test 21: D4(f) keeps the six out while MERGE does not carry their capabilities") {
    const char* six[] = {"srd51.monster.goblin", "srd51.monster.spy", "srd51.monster.orc",
                         "srd51.monster.berserker", "srd51.monster.boar", "srd51.monster.giant_boar"};
    ContentLibrary pack;
    const auto dir = std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core";
    REQUIRE(pack.load_pack(dir.string()).empty());
    Json selector = goblin_base("test.change.d4f");
    selector["$schema"] = "monster@5";
    selector["type"] = "celestial";
    selector["challenge"] = {{"rating", "4"}, {"xp", 1100}};
    selector["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                       {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                   {"select", {{"types", Json::array({"humanoid", "beast"})}, {"max_cr", "own"}}}}}}});
    selector["actions"] = Json::array({
        {{"id", "sting"}, {"name", "Sting"}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}})}},
    });
    REQUIRE_FALSE(pack.add(selector, "monster"));
    const Archetype* sel = pack.find("test.change.d4f");
    REQUIRE(sel);
    const Archetype::FormFeature* pool = nullptr;
    for (const auto& [id, feat] : sel->form_features)
        if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) pool = &feat;
    REQUIRE(pool);
    REQUIRE(pool->forms.size() == 42);
    for (const char* id : six)
        CHECK(std::find(pool->forms.begin(), pool->forms.end(), id) == pool->forms.end());
    const Archetype* real = pack.find("srd51.monster.couatl");
    REQUIRE(real);
    const Archetype::FormFeature* real_pool = nullptr;
    for (const auto& [id, feat] : real->form_features)
        if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) real_pool = &feat;
    REQUIRE(real_pool);
    REQUIRE(real_pool->forms.size() == 42);
    for (const char* id : six)
        CHECK(std::find(real_pool->forms.begin(), real_pool->forms.end(), id) == real_pool->forms.end());

    // An explicit form is allowed, so the absence above is not what this half checks. MERGE still does not
    // copy bonus_action_grants, aggressive, reckless_attack, or the form's usage pool. If it starts to, this
    // fails, and the D4(f) exclusion has to be lifted in the same change.
    Json form = goblin_base("test.d4f.form");
    form["$schema"] = "monster@5";
    form["type"] = "beast";
    form["traits"] = Json::array({
        {{"id", "nimble_escape"}, {"name", "Nimble Escape"},
         {"effect", {{"kind", "bonus_action_grant"}, {"actions", Json::array({"disengage", "hide"})}}}},
        {{"id", "aggressive"}, {"name", "Aggressive"}, {"effect", {{"kind", "aggressive"}}}},
        {{"id", "reckless"}, {"name", "Reckless"}, {"effect", {{"kind", "reckless_attack"}}}},
        {{"id", "relentless"}, {"name", "Relentless"},
         {"effect", {{"kind", "on_event"}, {"on", "drop_to_zero"},
                     {"when", Json::array({{{"kind", "damage_at_most"}, {"amount", 7}}})},
                     {"usage", {{"kind", "per_rest"}, {"uses", 1}, {"rest", "short"}}},
                     {"effects", Json::array({{{"kind", "set_hp"}, {"value", 1}}})}}}},
    });
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(form, "monster"));
    Json bare = goblin_base("test.d4f.who");
    bare["$schema"] = "monster@5";
    bare["type"] = "celestial";
    bare["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                   {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                               {"forms", Json::array({"test.d4f.form"})}}}}});
    bare["actions"] = Json::array({
        {{"id", "sting"}, {"name", "Sting"}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d6"}, {"type", "piercing"}}})}},
    });
    REQUIRE_FALSE(lib.add(bare, "monster"));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    const Archetype* fa = lib.find("test.d4f.form");
    REQUIRE(fa);
    CHECK(fa->base.aggressive);
    CHECK(fa->base.reckless_attack);
    CHECK((fa->base.bonus_action_grants & rules5e::bit(ActionKind::Disengage)) != 0);
    REQUIRE_FALSE(fa->resources.empty());
    auto chunk = arena({{"test.d4f.who", {4, 4}}, {"test.change.foe", {5, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 5);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = 1;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    const auto* me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    CHECK_FALSE(me->aggressive);
    CHECK_FALSE(me->reckless_attack);
    CHECK((me->bonus_action_grants & rules5e::bit(ActionKind::Disengage)) == 0);
    CHECK((me->bonus_action_grants & rules5e::bit(ActionKind::Hide)) == 0);
    CHECK(rules5e::find_pool(combat->state(), self, fa->resources[0].pool_id) == nullptr);
}

TEST_CASE("Change Shape D17: 256 eligible forms fail to load and 255 load") {
    auto one = [](const std::string& id) {
        Json m = goblin_base(id);
        m["traits"] = Json::array();
        return m;
    };
    Json who = goblin_base("test.change.cap");
    who["$schema"] = "monster@5";
    who["type"] = "celestial";
    who["challenge"] = {{"rating", "4"}, {"xp", 1100}};
    who["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                  {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                              {"select", {{"types", Json::array({"humanoid", "beast"})}, {"max_cr", "own"}}}}}}});
    {
        ContentLibrary too_many;
        for (int i = 0; i < 256; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "test.cap.%03d", i);
            REQUIRE_FALSE(too_many.add(one(buf), "monster"));
        }
        auto err = too_many.add(who, "monster");
        REQUIRE(err);
        CHECK(err->find("test.change.cap") != std::string::npos);
        CHECK(err->find("255") != std::string::npos);
        CHECK_FALSE(too_many.find("test.change.cap"));
    }
    {
        ContentLibrary ok;
        for (int i = 0; i < 255; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "test.cap.%03d", i);
            REQUIRE_FALSE(ok.add(one(buf), "monster"));
        }
        REQUIRE_FALSE(ok.add(who, "monster"));
        const Archetype* a = ok.find("test.change.cap");
        REQUIRE(a);
        const auto spec = a->feature_specs.find(1);
        REQUIRE(spec != a->feature_specs.end());
        CHECK(spec->second.form_choices == 255);
        CHECK(a->form_features.at(1).forms.size() == 255);
        auto err = ok.add(one("test.late.255"), "monster");
        REQUIRE(err);
        CHECK(err->find("test.late.255") != std::string::npos);
        CHECK(err->find("test.change.cap") != std::string::npos);
        CHECK(err->find("255") != std::string::npos);
        CHECK(ok.find("test.change.cap"));
        CHECK_FALSE(ok.find("test.late.255"));
    }
}

TEST_CASE("Change Shape D8: equipment on a change_shape creature is a load error naming the creature") {
    ContentLibrary lib;
    Json m = goblin_base("test.change.geared");
    m["$schema"] = "monster@5";
    m["type"] = "celestial";
    m["equipment"] = Json::array({{{"item", "srd51.item.spear"}}});
    m["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                            {"select", {{"types", Json::array({"beast"})}, {"max_cr", "own"}}}}}}});
    auto err = lib.add(m, "monster");
    REQUIRE(err);
    CHECK(err->find("test.change.geared") != std::string::npos);
    CHECK(err->find("equipment") != std::string::npos);
}

namespace {
// One temp root per construction. The process id keeps concurrent ctest runs apart, and the
// counter keeps each test's directory distinct inside one process.
std::filesystem::path unique_change_shape_root(const std::string& id) {
    static unsigned seq = 0;
#ifdef _WIN32
    const auto pid = _getpid();
#else
    const auto pid = getpid();
#endif
    return std::filesystem::temp_directory_path() /
           ("isoforge_change_shape_" + std::to_string(pid) + "_" + std::to_string(++seq) + "_" + id);
}

const char* kPackAttr = "System Reference Document 5.1 test fixture.";

Json pack_manifest(const std::string& id, Json contents, Json depends = Json::array()) {
    Json m = {{"$schema", "manifest@1"},
              {"id", id},
              {"version", "1.0.0"},
              {"engine_min", "0.1.0"},
              {"feature_level", 1},
              {"api_level", 1},
              {"rules_profile", "srd51"},
              {"requires", Json::array()},
              {"attribution", kPackAttr},
              {"contents", std::move(contents)}};
    if (!depends.empty()) m["depends"] = std::move(depends);
    return m;
}

Json pack_monster(const std::string& id, const char* type) {
    Json m = goblin_base(id);
    m["$schema"] = "monster@5";
    m["type"] = type;
    m["armor_class"]["note"] = "natural armor";
    m.erase("behavior");
    m.erase("skills");
    m.erase("tags");
    return m;
}

void write_json(const std::filesystem::path& path, const Json& j) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    REQUIRE(out.good());
    out << j.dump(2) << "\n";
}

// A one-pack directory. Contents are loaded in sorted path order.
struct TempPack {
    std::filesystem::path root, dir;
    explicit TempPack(const std::string& id) {
        root = unique_change_shape_root(id);
        std::filesystem::remove_all(root);
        dir = root / id;
        std::filesystem::create_directories(dir / "monsters");
    }
    ~TempPack() { std::filesystem::remove_all(root); }
    void write(const std::string& rel, const Json& j) { write_json(dir / rel, j); }
    Json monster(const std::string& id, const char* type) { return pack_monster(id, type); }
};

// Two pack directories under one root, so PackSet::resolve can see a depends edge.
struct TempPackRoot {
    std::filesystem::path root;
    explicit TempPackRoot(const std::string& id) {
        root = unique_change_shape_root(id);
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }
    ~TempPackRoot() { std::filesystem::remove_all(root); }
    std::filesystem::path pack_dir(const std::string& id) const {
        auto d = root / id;
        std::filesystem::create_directories(d / "monsters");
        return d;
    }
};

void advance_actor(Combat& c, CombatantId self) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == self);
}

bool landed_hit(const Combat& c, std::size_t mark, CombatantId self) {
    for (std::size_t i = mark; i < c.events().size(); ++i) {
        const auto& e = c.events()[i];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == self && (e.rules.c & 1))
            return true;
    }
    return false;
}
}  // namespace

TEST_CASE("Change Shape: an unknown explicit form is a load error, and a later same-pack form loads") {
    const char* kAttr = "System Reference Document 5.1 test fixture.";
    {
        TempPack pack("test.change_shape");
        Json owner = pack.monster("test.pack.couatl", "celestial");
        owner["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                        {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                    {"forms", Json::array({"test.pack.wolf"})}}}}});
        Json form = pack.monster("test.pack.wolf", "beast");
        form["traits"] = Json::array();
        pack.write("monsters/couatl.json", owner);
        pack.write("monsters/wolf.json", form);
        pack.write("manifest.json", {{"$schema", "manifest@1"},
                                     {"id", "test.change_shape"},
                                     {"version", "1.0.0"},
                                     {"engine_min", "0.1.0"},
                                     {"feature_level", 1},
                                     {"api_level", 1},
                                     {"rules_profile", "srd51"},
                                     {"requires", Json::array()},
                                     {"attribution", kAttr},
                                     {"contents", Json::array({"monsters/couatl.json", "monsters/wolf.json"})}});
        ContentLibrary lib;
        auto errs = lib.load_pack(pack.dir.string());
        const std::string why = errs.empty() ? std::string() : errs[0];
        REQUIRE_MESSAGE(errs.empty(), why);
        const Archetype* who = lib.find("test.pack.couatl");
        REQUIRE(who);
        const Archetype::FormFeature* ff = nullptr;
        for (const auto& [id, feat] : who->form_features)
            if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) ff = &feat;
        REQUIRE(ff);
        REQUIRE(ff->forms.size() == 1);
        CHECK(ff->forms[0] == "test.pack.wolf");
    }
    {
        TempPack pack("test.change_missing");
        Json owner = pack.monster("test.pack.couatl", "celestial");
        owner["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                        {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                    {"forms", Json::array({"test.pack.missing"})}}}}});
        pack.write("monsters/couatl.json", owner);
        pack.write("manifest.json", {{"$schema", "manifest@1"},
                                     {"id", "test.change_missing"},
                                     {"version", "1.0.0"},
                                     {"engine_min", "0.1.0"},
                                     {"feature_level", 1},
                                     {"api_level", 1},
                                     {"rules_profile", "srd51"},
                                     {"requires", Json::array()},
                                     {"attribution", kAttr},
                                     {"contents", Json::array({"monsters/couatl.json"})}});
        ContentLibrary lib;
        auto errs = lib.load_pack(pack.dir.string());
        REQUIRE_FALSE(errs.empty());
        bool named = false;
        for (const std::string& e : errs) named = named || e.find("unknown form 'test.pack.missing'") != std::string::npos;
        CHECK(named);
    }
}

TEST_CASE("Change Shape: a same-pack form that sorts before its owner loads, and add() does not reject an unknown form") {
    {
        TempPack pack("test.change_early");
        Json form = pack.monster("test.pack.ape", "beast");
        form["traits"] = Json::array();
        Json owner = pack.monster("test.pack.zz", "celestial");
        owner["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                        {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                    {"forms", Json::array({"test.pack.ape"})}}}}});
        pack.write("monsters/ape.json", form);
        pack.write("monsters/zz.json", owner);
        pack.write("manifest.json", pack_manifest("test.change_early", Json::array({"monsters/ape.json", "monsters/zz.json"})));
        ContentLibrary lib;
        auto errs = lib.load_pack(pack.dir.string());
        const std::string why = errs.empty() ? std::string() : errs[0];
        REQUIRE_MESSAGE(errs.empty(), why);
        const Archetype* who = lib.find("test.pack.zz");
        REQUIRE(who);
        const Archetype::FormFeature* ff = nullptr;
        for (const auto& [id, feat] : who->form_features)
            if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) ff = &feat;
        REQUIRE(ff);
        REQUIRE(ff->forms.size() == 1);
        CHECK(ff->forms[0] == "test.pack.ape");
    }
    {
        ContentLibrary direct;
        Json owner = pack_monster("test.direct.unknown", "celestial");
        owner["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                        {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                    {"forms", Json::array({"test.not.loaded"})}}}}});
        auto err = direct.add(owner, "monster");
        const std::string why = err ? *err : std::string();
        REQUIRE_MESSAGE(!err, why);
        CHECK(direct.find("test.direct.unknown"));
    }
}

TEST_CASE("Change Shape: an explicit form from a dependency pack loads, and a form from a later pack loads") {
    {
        TempPackRoot root("dep");
        const auto dep = root.pack_dir("test.cs.dep");
        const auto owner = root.pack_dir("test.cs.owner");
        Json form = pack_monster("test.dep.bite", "beast");
        form["traits"] = Json::array();
        form["actions"] = Json::array({
            {{"id", "bite"}, {"name", "Bite"}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5}, {"targets", 1},
             {"damage", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}})}},
        });
        write_json(dep / "monsters" / "form.json", form);
        write_json(dep / "manifest.json", pack_manifest("test.cs.dep", Json::array({"monsters/form.json"})));
        Json who = pack_monster("test.owner.couatl", "celestial");
        who["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                      {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                  {"forms", Json::array({"test.dep.bite"})}}}}});
        Json selector = pack_monster("test.owner.selector", "celestial");
        selector["challenge"] = {{"rating", "4"}, {"xp", 1100}};
        selector["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                           {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                       {"select", {{"types", Json::array({"beast"})}, {"max_cr", "own"}}}}}}});
        selector["actions"] = Json::array({
            {{"id", "sting"}, {"name", "Sting"}, {"kind", "melee_weapon_attack"}, {"to_hit", 4}, {"reach", 5}, {"targets", 1},
             {"damage", Json::array({{{"dice", "1d4"}, {"type", "piercing"}}})}},
        });
        write_json(owner / "monsters" / "couatl.json", who);
        write_json(owner / "monsters" / "selector.json", selector);
        write_json(owner / "manifest.json",
                   pack_manifest("test.cs.owner", Json::array({"monsters/couatl.json", "monsters/selector.json"}),
                                 Json::array({{{"id", "test.cs.dep"}, {"version", "^1.0"}}})));
        const std::filesystem::path roots[] = {root.root};
        const std::string req[] = {"test.cs.owner"};
        const auto set = assets::PackSet::resolve(roots, req);
        REQUIRE(set.ok());
        ContentLibrary lib;
        auto errs = lib.load(set);
        const std::string why = errs.empty() ? std::string() : errs[0];
        REQUIRE_MESSAGE(errs.empty(), why);
        const Archetype* bite = lib.find("test.dep.bite");
        REQUIRE(bite);
        CHECK(bite->pool_bite);
        const Archetype* explicit_owner = lib.find("test.owner.couatl");
        REQUIRE(explicit_owner);
        const Archetype::FormFeature* kept = nullptr;
        for (const auto& [id, feat] : explicit_owner->form_features)
            if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) kept = &feat;
        REQUIRE(kept);
        REQUIRE(kept->forms.size() == 1);
        CHECK(kept->forms[0] == "test.dep.bite");
        const Archetype* sel = lib.find("test.owner.selector");
        REQUIRE(sel);
        const Archetype::FormFeature* pool = nullptr;
        for (const auto& [id, feat] : sel->form_features)
            if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) pool = &feat;
        REQUIRE(pool);
        CHECK(pool->forms.empty());
    }
    {
        TempPackRoot root("later");
        const auto early = root.pack_dir("test.cs.early");
        const auto later = root.pack_dir("test.cs.later");
        Json who = pack_monster("test.early.couatl", "celestial");
        who["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"},
                                      {"effect", {{"kind", "change_shape"}, {"profile", "merge"},
                                                  {"forms", Json::array({"test.later.form"})}}}}});
        write_json(early / "monsters" / "couatl.json", who);
        write_json(early / "manifest.json", pack_manifest("test.cs.early", Json::array({"monsters/couatl.json"})));
        Json form = pack_monster("test.later.form", "beast");
        form["traits"] = Json::array();
        write_json(later / "monsters" / "form.json", form);
        write_json(later / "manifest.json",
                   pack_manifest("test.cs.later", Json::array({"monsters/form.json"}),
                                 Json::array({{{"id", "test.cs.early"}, {"version", "^1.0"}}})));
        const std::filesystem::path roots[] = {root.root};
        const std::string req[] = {"test.cs.later"};
        const auto set = assets::PackSet::resolve(roots, req);
        REQUIRE(set.ok());
        ContentLibrary lib;
        auto errs = lib.load(set);
        const std::string why = errs.empty() ? std::string() : errs[0];
        REQUIRE_MESSAGE(errs.empty(), why);
        const Archetype* found = lib.find("test.early.couatl");
        REQUIRE(found);
        const Archetype::FormFeature* ff = nullptr;
        for (const auto& [id, feat] : found->form_features)
            if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) ff = &feat;
        REQUIRE(ff);
        REQUIRE(ff->forms.size() == 1);
        CHECK(ff->forms[0] == "test.later.form");
        CHECK(lib.find("test.later.form"));
    }
}

TEST_CASE("Change Shape D1: a form's own on_hit rider fires") {
    Json form = form_a();
    form["actions"][0]["on_hit"] = Json::array({{{"kind", "apply_condition"},
                                                  {"apply", Json::array({{{"condition", "blinded"},
                                                                          {"duration", {{"kind", "rounds"}, {"n", 1}}}}})}}});
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(form, "monster"));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    REQUIRE_FALSE(lib.add(changer_doc({"test.change.form_a"}), "monster"));
    auto chunk = arena({{"test.change.couatl", {4, 4}}, {"test.change.foe", {5, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 7);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id, foe = chunk.spawns[1].stable_id;
    advance_actor(*combat, self);
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = 1;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    Command done;
    done.request = {self, ActionKind::EndTurn};
    REQUIRE(combat->submit(done).ok);
    bool blinded = false;
    for (int n = 0; n < 8 && !blinded; ++n) {
        advance_actor(*combat, self);
        const auto mark = combat->events().size();
        Command atk;
        atk.request = {self, ActionKind::Attack, foe, 0};
        auto r = combat->submit(atk);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (landed_hit(*combat, mark, self))
            blinded = rules5e::find(combat->state(), foe)->has(rules5e::Condition::Blinded);
        if (!blinded) {
            Command e;
            e.request = {self, ActionKind::EndTurn};
            if (!combat->submit(e).ok) break;
        }
    }
    CHECK(blinded);
}

TEST_CASE("Change Shape: an own on_event trigger still fires while the creature is in a form") {
    Json who = changer_doc({"test.change.form_a"});
    who["traits"].push_back(
        {{"id", "mark"},
         {"name", "Mark"},
         {"description", "Own on_event."},
         {"effect",
          {{"kind", "on_event"},
           {"on", "hit"},
           {"effects", Json::array({{{"kind", "apply_condition"},
                                     {"apply", Json::array({{{"condition", "frightened"},
                                                             {"duration", {{"kind", "rounds"}, {"n", 1}}}}})}}})}}}});
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(form_a(), "monster"));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    auto err = lib.add(who, "monster");
    const std::string why = err ? *err : std::string();
    REQUIRE_MESSAGE(!err, why);
    auto chunk = arena({{"test.change.couatl", {4, 4}}, {"test.change.foe", {5, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 8);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id, foe = chunk.spawns[1].stable_id;
    const uint32_t mark_id = rules5e::spell_id_of("test.change.couatl#mark#0");
    advance_actor(*combat, self);
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = 1;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    const auto* me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    bool present = false;
    for (const auto& t : me->triggers)
        if (t.id == mark_id) present = true;
    CHECK(present);
    Command done;
    done.request = {self, ActionKind::EndTurn};
    REQUIRE(combat->submit(done).ok);
    bool frightened = false;
    for (int n = 0; n < 8 && !frightened; ++n) {
        advance_actor(*combat, self);
        const auto mark = combat->events().size();
        Command atk;
        atk.request = {self, ActionKind::Attack, foe, 0};
        auto r = combat->submit(atk);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (landed_hit(*combat, mark, self))
            frightened = rules5e::find(combat->state(), foe)->has(rules5e::Condition::Frightened);
        if (!frightened) {
            Command e;
            e.request = {self, ActionKind::EndTurn};
            if (!combat->submit(e).ok) break;
        }
    }
    CHECK(frightened);
}

TEST_CASE("Change Shape AI: the true form picks Glare") {
    Fight f;
    f.advance_to(f.self);
    CHECK(f.who(f.self).form == FormKind::None);
    Command ai = next_command(f.c(), f.foe);
    CHECK(ai.request.kind == ActionKind::CastSpell);
    CHECK(ai.request.spell_id == f.glare_id);
}

TEST_CASE("Change Shape attack_archetype: range and grapple_on_hit come from the form") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(form_a(), "monster"));
    REQUIRE_FALSE(lib.add(form_b(), "monster"));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    REQUIRE_FALSE(lib.add(changer_doc({"test.change.form_a", "test.change.form_b"}), "monster"));
    auto chunk = arena({{"test.change.couatl", {4, 4}}, {"test.change.foe", {12, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 3);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id, foe = chunk.spawns[1].stable_id;
    const uint32_t change_id = 1;
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = change_id;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    // The transform spent the action. Come back with a fresh one before the ranged shot.
    {
        Command e;
        e.request = {self, ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    const Archetype* used = combat->attack_archetype(self);
    REQUIRE(used);
    CHECK(used->id == "test.change.form_a");
    CHECK(used->grapple_on_hit.count(1) == 1);
    CHECK(combat->archetype(self)->grapple_on_hit.count(1) == 0);
    bool accepted = false, grappled = false;
    for (int n = 0; n < 24 && !grappled; ++n) {
        if (rules5e::current_actor(combat->state()) != self) {
            for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
                Command e;
                e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
                if (!combat->submit(e).ok) break;
            }
        }
        Command shot;
        shot.request = {self, ActionKind::Attack, foe, 1};
        auto r = combat->submit(shot);
        if (!r.ok) {
            REQUIRE_MESSAGE(false, r.error);
            break;
        }
        accepted = true;
        for (const auto& e : combat->events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && (e.rules.c & 1) && e.rules.actor == self)
                grappled = rules5e::find(combat->state(), foe)->has(rules5e::Condition::Grappled);
        if (!grappled) {
            Command e;
            e.request = {self, ActionKind::EndTurn};
            if (!combat->submit(e).ok) break;
        }
    }
    CHECK(accepted);
    CHECK(grappled);
}

TEST_CASE("Change Shape D16: a transformed possessor is refused; D13 stays deferred under D5") {
    // D13 (refuse Possession of a body in a Change Shape form, and refuse Change Shape while possessed
    // or possessing) is DEFERRED. D5 keeps the couatl's own creature type, so the humanoid gate never
    // opens on it. Only D16 is required: begin_possession refuses a possessor that is already in a form.
    Fight f;
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    std::vector<rules5e::Event> ev;
    auto err = rules5e::begin_possession(CombatTestAccess::state(f.c()), f.self, f.foe, 1, 0, 0, ev);
    CHECK(err == "possessor is transformed");
    CHECK(f.who(f.self).possessing == 0);
    CHECK(f.who(f.foe).possessed_by == 0);
}

TEST_CASE("Change Shape AI never selects the feature") {
    Fight f;
    NearestEnemyPolicy pol;
    const auto rr = run_to_end(f.c(), [&](CombatantId) -> IActorPolicy* { return &pol; }, 400);
    REQUIRE_MESSAGE(rr.ended, rr.error);
    for (const auto& e : f.c().events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureUsed)
            CHECK(e.rules.a != int32_t(f.change_id));
    CHECK(f.who(f.self).form == FormKind::None);
}

TEST_CASE("Change Shape schema stays 14, and a Transformed stream reports v10") {
    // The two determinism pins live in legendary_actions_tests.cpp (seed 11 at the legendary beast,
    // seed 4 at fighter vs goblin). They are not copied here; that file is where they move.
    CHECK(kSimEventSchemaVersion == 14);
    Fight f;
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    Command rev;
    rev.request = {f.self, ActionKind::Revert};
    // The action was spent on the transform. Revert on the next turn so the stream has both events.
    f.end_turn();
    f.advance_to(f.self);
    REQUIRE(f.c().submit(rev).ok);
    CHECK(event_stream_version(f.c().events()) == 10);
}

TEST_CASE("Change Shape replay: the same library replays") {
    // A pool index is a u8 into a list compiled from the loaded content. replay() takes the caller's
    // ContentLibrary and records no content hash, so a replay needs the same loaded set. Polymorph's
    // beasts_ index has the same exposure. This test does not assert a fix for that (DEFERRED 12).
    Fight f;
    f.advance_to(f.self);
    REQUIRE(f.use_form(1).ok);
    f.end_turn();
    ecs::Registry reg2;
    std::string err;
    auto again = replay(reg2, f.chunk, f.lib, 11, f.c().log(), &err);
    INFO(err);
    REQUIRE(again);
    CHECK(again->hash() == f.c().hash());
    CHECK(again->log_hash() == f.c().log_hash());
}

TEST_CASE("Change Shape keeps its size, so a larger form still fits where a Large footprint would not") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(form_a(), "monster"));
    REQUIRE_FALSE(lib.add(foe_doc(), "monster"));
    REQUIRE_FALSE(lib.add(changer_doc({"test.change.form_a"}), "monster"));
    auto chunk = arena({{"test.change.couatl", {31, 31}}, {"test.change.foe", {4, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 5);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    for (int i = 0; i < 6 && rules5e::current_actor(combat->state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = 1;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    const auto* me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    CHECK(me->form == FormKind::Shapechanger);
    CHECK(me->size == rules5e::Size::Medium);
    CHECK(combat->footprint_size(self) == 1);
}

namespace {

struct CouatlPool {
    ContentLibrary lib;
    const Archetype* who = nullptr;
    const Archetype::FormFeature* pool = nullptr;
    uint32_t fid = 0;
    CouatlPool() {
        const auto dir = std::filesystem::path(ENGINE_SOURCE_ROOT) / "content/packs/srd51.core";
        REQUIRE(lib.load_pack(dir.string()).empty());
        who = lib.find("srd51.monster.couatl");
        REQUIRE(who);
        for (const auto& [id, feat] : who->form_features)
            if (feat.sub_kind == Archetype::FormSubKind::ChangeShapeMerge) { pool = &feat; fid = id; }
        REQUIRE(pool);
    }
};

void reach(Combat& combat, CombatantId self) {
    for (int i = 0; i < 8 && rules5e::current_actor(combat.state()) != self; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat.state()), ActionKind::EndTurn};
        REQUIRE(combat.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(combat.state()) == self);
}

bool attack_named(const rules5e::Attack& at, const char* needle) {
    return at.id.find(needle) != std::string_view::npos;
}

}  // namespace

TEST_CASE("Change Shape test 16: the real couatl's pool is the 42 pinned ids, and Change Shape is available at spawn") {
    CouatlPool c;
    CHECK_FALSE(c.who->base.shapechanger);
    CHECK(c.who->base.creature_type == rules5e::CreatureType::Celestial);
    CHECK(c.who->size == CreatureSize::Medium);
    CHECK(c.who->base.proficiency_bonus == 2);
    CHECK(c.who->equipment.empty());
    CHECK(c.pool->select_pool);
    REQUIRE(c.pool->forms.size() == 42);
    CHECK(c.who->feature_specs.at(c.fid).form_choices == 42);
    for (std::size_t i = 0; i < 42; ++i) CHECK(c.pool->forms[i] == kPool42[i]);
    for (const char* absent : {"srd51.monster.goblin", "srd51.monster.spy", "srd51.monster.orc",
                               "srd51.monster.berserker", "srd51.monster.boar", "srd51.monster.giant_boar"})
        CHECK(std::find(c.pool->forms.begin(), c.pool->forms.end(), absent) == c.pool->forms.end());
    for (const std::string& id : c.pool->forms) {
        const Archetype* form = c.lib.find(id);
        REQUIRE(form);
        CHECK(form->base.proficiency_bonus == 2);
        CHECK_FALSE(form->pool_bite);
        CHECK((form->base.creature_type == rules5e::CreatureType::Humanoid || form->base.creature_type == rules5e::CreatureType::Beast));
    }
    auto chunk = arena({{"srd51.monster.couatl", {4, 4}}, {"srd51.monster.goblin", {6, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, c.lib, 11);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    const auto* spawned = rules5e::find(combat->state(), self);
    REQUIRE(spawned);
    CHECK(spawned->form == FormKind::None);
    CHECK(spawned->size == rules5e::Size::Medium);
    CHECK(spawned->creature_type == rules5e::CreatureType::Celestial);
    CHECK_FALSE(spawned->shapechanger);
    CHECK(spawned->proficiency_bonus == 2);
    reach(*combat, self);
    // UseFeature is submitted directly. legal_actions does not enumerate it. can_use_feature is the spawn gate.
    CHECK(combat->can_use_feature(self, c.fid));
    Command ai = next_command(*combat, chunk.spawns[1].stable_id);
    CHECK(ai.request.kind != ActionKind::UseFeature);
    CHECK(rules5e::find(combat->state(), self)->form == FormKind::None);
}

TEST_CASE("Change Shape test 17: a form without a bite can be left at will, and Bite and Constrict are replaced") {
    CouatlPool c;
    const Archetype* form = c.lib.find(c.pool->forms[0]);
    REQUIRE(form);
    CHECK_FALSE(form->pool_bite);
    for (const auto& at : form->base.attacks) CHECK_FALSE(attack_named(at, "bite"));
    auto chunk = arena({{"srd51.monster.couatl", {4, 4}}, {"srd51.monster.goblin", {6, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, c.lib, 11);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    reach(*combat, self);
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = c.fid;
    u.request.form_archetype = 1;
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    const auto* me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    CHECK(me->form == FormKind::Shapechanger);
    CHECK(me->size == rules5e::Size::Medium);
    CHECK(me->creature_type == rules5e::CreatureType::Celestial);
    CHECK(me->proficiency_bonus == 2);
    REQUIRE_FALSE(me->attacks.empty());
    for (const auto& at : me->attacks) {
        CHECK(at.id != "bite");
        CHECK(at.id != "constrict");
        CHECK_FALSE(attack_named(at, "bite"));
    }
    Command end;
    end.request = {self, ActionKind::EndTurn};
    REQUIRE(combat->submit(end).ok);
    reach(*combat, self);
    bool revert_offered = false;
    for (const auto& q : combat->legal_actions())
        if (q.kind == ActionKind::Revert) revert_offered = true;
    CHECK(revert_offered);
    CHECK(combat->can_use_feature(self, c.fid));
    Command rev;
    rev.request = {self, ActionKind::Revert};
    auto reverted = combat->submit(rev);
    REQUIRE_MESSAGE(reverted.ok, reverted.error);
    me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    CHECK(me->form == FormKind::None);
    REQUIRE(me->attacks.size() == 2);
    CHECK(me->attacks[0].id == "bite");
    CHECK(me->attacks[1].id == "constrict");
}

TEST_CASE("Change Shape test 18: the loader rejects a malformed change_shape effect, and the real couatl loads") {
    // The validator half is tests/validate/test_v4_validator.py (cs_bad_profile, cs_cost_reaction,
    // cs_unknown_form, cs_two, and the equipment check). This is the loader half.
    auto bad = [](Json effect) {
        ContentLibrary lib;
        Json m = goblin_base("test.change.bad");
        m["$schema"] = "monster@5";
        m["type"] = "celestial";
        m["traits"] = Json::array({{{"id", "change_shape"}, {"name", "Change Shape"}, {"effect", std::move(effect)}}});
        auto err = lib.add(m, "monster");
        REQUIRE(err);
        return *err;
    };
    CHECK(bad({{"kind", "change_shape"}, {"profile", "replace"}, {"forms", Json::array({"x"})}}).find("profile must be merge") != std::string::npos);
    CHECK(bad({{"kind", "change_shape"}, {"profile", "merge"}}).find("needs forms or select") != std::string::npos);
    CHECK(bad({{"kind", "change_shape"}, {"profile", "merge"}, {"note", true},
               {"select", {{"types", Json::array({"beast"})}, {"max_cr", "own"}}}}).find("unknown key 'note'") != std::string::npos);
    CHECK(bad({{"kind", "change_shape"}, {"profile", "merge"},
               {"select", {{"types", Json::array({"dragon"})}, {"max_cr", "own"}}}}).find("not humanoid or beast") != std::string::npos);
    CouatlPool c;
    CHECK(c.pool->select_pool);
    CHECK(c.pool->forms.size() == 42);
    CHECK_FALSE(c.who->base.shapechanger);
}

TEST_CASE("Change Shape test 20: D4(e) keeps every bite form out of the real couatl pool") {
    // 40 humanoid or beast forms at CR 4 or below survive D4 and carry a bite. They stay out until
    // the bite rule exists. A pool that starts accepting them fails here.
    CouatlPool c;
    int bite_forms = 0;
    for (const auto& [id, o] : c.lib.all()) {
        if (o.schema_type != "monster" || o.cr_x8 < 0 || o.cr_x8 > c.who->cr_x8) continue;
        if (o.base.creature_type != rules5e::CreatureType::Humanoid && o.base.creature_type != rules5e::CreatureType::Beast) continue;
        if (o.pool_spellcasting || o.pool_shapechanger || o.swarm || o.pool_nonattack || !o.pool_bite) continue;
        ++bite_forms;
        CHECK(std::find(c.pool->forms.begin(), c.pool->forms.end(), id) == c.pool->forms.end());
    }
    CHECK(bite_forms == 40);
    for (const std::string& id : c.pool->forms) {
        const Archetype* form = c.lib.find(id);
        REQUIRE(form);
        CHECK_FALSE(form->pool_bite);
    }
}

TEST_CASE("Change Shape D7b: the real couatl's form attacks are magical while transformed and revert clears them") {
    CouatlPool c;
    const Archetype* form = c.lib.find("srd51.monster.ape");
    REQUIRE(form);
    REQUIRE_FALSE(form->base.attacks.empty());
    for (const auto& at : form->base.attacks) CHECK((at.qualifiers & rules5e::kAttackMagical) == 0);
    std::size_t ape = c.pool->forms.size();
    for (std::size_t i = 0; i < c.pool->forms.size(); ++i)
        if (c.pool->forms[i] == "srd51.monster.ape") ape = i;
    REQUIRE(ape < c.pool->forms.size());
    auto chunk = arena({{"srd51.monster.couatl", {4, 4}}, {"srd51.monster.goblin", {6, 4}}}, {"", "foes"});
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, c.lib, 11);
    REQUIRE(combat);
    const CombatantId self = chunk.spawns[0].stable_id;
    reach(*combat, self);
    const auto* before = rules5e::find(combat->state(), self);
    REQUIRE(before);
    REQUIRE(before->attacks.size() == 2);
    CHECK((before->attacks[0].qualifiers & rules5e::kAttackMagical) != 0);
    CHECK((before->attacks[1].qualifiers & rules5e::kAttackMagical) != 0);
    Command u;
    u.request = {self, ActionKind::UseFeature};
    u.request.feature_id = c.fid;
    u.request.form_archetype = uint8_t(ape + 1);
    auto tr = combat->submit(u);
    REQUIRE_MESSAGE(tr.ok, tr.error);
    const auto* me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    REQUIRE(me->attacks.size() == form->base.attacks.size());
    for (std::size_t i = 0; i < me->attacks.size(); ++i) {
        CHECK(me->attacks[i].id == form->base.attacks[i].id);
        CHECK((me->attacks[i].qualifiers & rules5e::kAttackMagical) != 0);
        CHECK((form->base.attacks[i].qualifiers & rules5e::kAttackMagical) == 0);
    }
    Command end;
    end.request = {self, ActionKind::EndTurn};
    REQUIRE(combat->submit(end).ok);
    reach(*combat, self);
    Command rev;
    rev.request = {self, ActionKind::Revert};
    REQUIRE(combat->submit(rev).ok);
    me = rules5e::find(combat->state(), self);
    REQUIRE(me);
    CHECK(me->form == FormKind::None);
    REQUIRE(me->attacks.size() == 2);
    CHECK(me->attacks[0].id == "bite");
    CHECK(me->attacks[1].id == "constrict");
    CHECK((me->attacks[0].qualifiers & rules5e::kAttackMagical) != 0);
    CHECK((me->attacks[1].qualifiers & rules5e::kAttackMagical) != 0);
    for (const auto& at : form->base.attacks) CHECK((at.qualifiers & rules5e::kAttackMagical) == 0);
}
