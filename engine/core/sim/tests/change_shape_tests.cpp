// Change Shape P1 (contract v0.1.2). MERGE reuses FormKind::Shapechanger. No new kind, no hash byte,
// schema stays 14. Fixtures only: no couatl content. D10, D11, and the D13 possession guard stay deferred.
#include <doctest/doctest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

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
        {{"id", "sting"}, {"name", "Sting"}, {"kind", "melee_weapon_attack"}, {"to_hit", 8}, {"reach", 5}, {"targets", 1},
         {"damage", Json::array({{{"dice", "1d6+3"}, {"type", "piercing"}}})}},
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
        {{"id", "slam"}, {"name", "Slam"}, {"kind", "melee_weapon_attack"}, {"to_hit", 6}, {"reach", 5}, {"targets", 1},
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

const char* kPool48[] = {
    "srd51.monster.ape", "srd51.monster.axe_beak", "srd51.monster.bandit", "srd51.monster.berserker",
    "srd51.monster.blood_hawk", "srd51.monster.boar", "srd51.monster.bugbear", "srd51.monster.cat",
    "srd51.monster.commoner", "srd51.monster.crab", "srd51.monster.cultist", "srd51.monster.draft_horse",
    "srd51.monster.eagle", "srd51.monster.elephant", "srd51.monster.elk", "srd51.monster.giant_boar",
    "srd51.monster.giant_crab", "srd51.monster.giant_eagle", "srd51.monster.giant_elk", "srd51.monster.giant_goat",
    "srd51.monster.giant_owl", "srd51.monster.giant_scorpion", "srd51.monster.giant_sea_horse",
    "srd51.monster.giant_vulture", "srd51.monster.giant_wasp", "srd51.monster.goat", "srd51.monster.goblin",
    "srd51.monster.grimlock", "srd51.monster.guard", "srd51.monster.hobgoblin", "srd51.monster.kobold",
    "srd51.monster.merfolk", "srd51.monster.mule", "srd51.monster.orc", "srd51.monster.owl", "srd51.monster.pony",
    "srd51.monster.raven", "srd51.monster.rhinoceros", "srd51.monster.riding_horse", "srd51.monster.scorpion",
    "srd51.monster.scout", "srd51.monster.spy", "srd51.monster.stirge", "srd51.monster.thug",
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

TEST_CASE("Change Shape pool: humanoid or beast, CR at most own, id order, and the six D4 groups stay out") {
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
    CHECK(a->feature_specs.at(1).form_choices == 2);

    // The pack, recounted: 112 humanoid or beast at CR 4 or lower, 88 after D4, 48 after D4(e).
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
    REQUIRE(pool->forms.size() == 48);
    CHECK(sel->feature_specs.at(fid).form_choices == 48);
    for (std::size_t i = 0; i < 48; ++i) CHECK(pool->forms[i] == kPool48[i]);
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
                               "srd51.monster.noble", "srd51.monster.knight", "srd51.monster.wolf"})
        CHECK(std::find(pool->forms.begin(), pool->forms.end(), absent) == pool->forms.end());
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

TEST_CASE("Change Shape hash, schema 14, and a Transformed stream reports v10") {
    CHECK(kSimEventSchemaVersion == 14);
    ContentLibrary plain;
    REQUIRE(plain.load(sample_set()).empty());
    auto ch = arena({{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {8, 4}}});
    ecs::Registry reg;
    auto fight = Combat::create(reg, ch, plain, 4);
    REQUIRE(fight);
    Command end;
    end.request = {rules5e::current_actor(fight->state()), ActionKind::EndTurn};
    REQUIRE(fight->submit(end).ok);
    CHECK(fight->hash() == 0x67bca9677135717dull);
    Json beast = load_json("content/packs/srd51.core/monsters/goblin.json");
    beast["$schema"] = "monster@7";
    beast["id"] = "test.legendary.beast";
    beast["type"] = "beast";
    beast["challenge"] = {{"rating", "0"}, {"xp", 10}};
    beast["name"] = "Legendary Beast";
    beast["hit_points"]["average"] = 80;
    beast["legendary_actions_per_round"] = 3;
    beast["legendary_actions"] = Json::array({
        {{"id", "bite"}, {"name", "Bite"}, {"description", "One bite."}, {"cost", 1}, {"use", {{"attack", "scimitar"}}}},
        {{"id", "sniff"}, {"name", "Sniff"}, {"description", "A Perception check."}, {"use", {{"check", "perception"}}}},
        {{"id", "stride"}, {"name", "Stride"}, {"description", "Move up to its speed."}, {"use", {{"move", "speed"}}}},
        {{"id", "glide"}, {"name", "Glide"}, {"description", "Move without provoking."},
         {"use", {{"move", "half_speed"}, {"no_opportunity_attacks", true}}}},
    });
    ContentLibrary legend;
    REQUIRE(legend.load(sample_set()).empty());
    REQUIRE_FALSE(legend.add(beast, "monster"));
    auto ch11 = arena({{"srd51.character.fighter_1", {7, 4}}, {"test.legendary.beast", {8, 4}}});
    ecs::Registry reg11;
    auto pinned = Combat::create(reg11, ch11, legend, 11);
    REQUIRE(pinned);
    CHECK(pinned->hash() == 0x40c5e7a2531bae06ull);

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
