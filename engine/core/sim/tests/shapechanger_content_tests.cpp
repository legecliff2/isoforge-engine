// Shapechange B1-B7, sim side (shapechange-contract v0.1 §5). Fixtures only: no pack monster is edited.
// Loader gates, true-form subsets, per-form speed / senses / traits / weapon dice, Moonbeam's 0xEA lock, replay.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e/encounter.hpp"
#include "sim/combat.hpp"
#include "sim/events.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::FormKind;
using rules5e::Light;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kLycan = "test.monster.lycan";
const char* kOni = "test.monster.oni_form";
const char* kMage = "test.character.shape_mage";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json melee(const char* id, const char* dice, int to_hit, Json extra = Json::object()) {
    Json a = {{"id", id}, {"name", id}, {"kind", "melee_weapon_attack"}, {"to_hit", to_hit}, {"reach", 5}, {"targets", 1},
              {"damage", Json::array({{{"dice", dice}, {"type", "slashing"}}})}};
    for (auto it = extra.begin(); it != extra.end(); ++it) a[it.key()] = it.value();
    return a;
}
Json on_hit_trait(const char* id) {
    return {{"id", id}, {"name", id}, {"description", "x"},
            {"effect", {{"kind", "on_event"}, {"on", "hit"},
                        {"effects", Json::array({{{"kind", "extra_damage"},
                                                   {"damage", Json::array({{{"dice", "1d4"}, {"type", "bludgeoning"}}})}}})}}}};
}
Json lycan() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = kLycan;
    m["size"] = "medium";
    m["hit_points"]["average"] = 40;
    m["armor_class"]["value"] = 11;
    m["speed"] = {{"walk", 30}, {"fly", 30}};
    m["abilities"] = {{"str", 17}, {"dex", 12}, {"con", 14}, {"int", 10}, {"wis", 11}, {"cha", 8}};
    m["saving_throws"] = {{"con", 4}};
    m["damage_resistances"] = Json::array({"fire"});
    m["senses"] = {{"passive_perception", 10}};
    m["shapechanger"] = true;
    m["actions"] = Json::array({melee("spear", "1d6+3", 30), melee("bite", "1d8+3", 30), melee("claws", "2d4+3", 30)});
    Json shape = {{"id", "shapechanger"}, {"name", "Shapechanger"}, {"description", "x"},
                  {"effect", {{"kind", "shapechanger"},
                              {"true_form", {{"actions", Json::array({"spear"})},
                                             {"traits", Json::array({"shapechanger"})}}},
                              {"forms", Json::array({
                                  {{"id", "hybrid"}, {"name", "Hybrid"}, {"armor_class", 12},
                                   {"actions", Json::array({"bite", "claws"})},
                                   {"traits", Json::array({"charge", "pounce", "shapechanger"})}},
                                  {{"id", "wolf"}, {"name", "Wolf"}, {"armor_class", 12}, {"speed", {{"walk", 40}}},
                                   {"actions", Json::array({"bite"})}, {"traits", Json::array({"shapechanger"})}},
                                  {{"id", "rat"}, {"name", "Rat"}, {"senses", {{"darkvision", 60}}},
                                   {"actions", Json::array({"bite"})}},
                                  {{"id", "bear"}, {"name", "Bear"}, {"speed", {{"walk", 40}, {"climb", 30}}},
                                   {"actions", Json::array({"bite", "claws"})}},
                                  {{"id", "raven"}, {"name", "Raven"}, {"speed", {{"walk", 20}, {"fly", 60}}},
                                   {"actions", Json::array({"bite"})}},
                                  {{"id", "humanoid"}, {"name", "Humanoid"}, {"speed", {{"walk", 30}, {"fly", 0}}},
                                   {"actions", Json::array({"spear"})}}
                              })}}}};
    m["traits"] = Json::array({on_hit_trait("charge"), on_hit_trait("pounce"), shape});
    m.erase("behavior");
    return m;
}
Json oni() {
    Json m = lycan();
    m["id"] = kOni;
    m.erase("shapechanger");
    m["weapon_dice_multiplier"] = 2;
    m["hit_points"]["average"] = 30;
    m["actions"] = Json::array({
        melee("claw", "1d8+4", 7, {{"natural", {{"dice", "1d8"}, {"ability", "str"}}}}),
        melee("glaive", "2d10+4", 7, {{"weapon", "srd51.item.glaive"}})});
    m["traits"] = Json::array({{{"id", "change"}, {"name", "Change Shape"}, {"description", "x"},
                                {"effect", {{"kind", "shapechanger"}, {"trait", false},
                                            {"forms", Json::array({
                                                {{"id", "medium"}, {"name", "Medium"}, {"size", "medium"},
                                                 {"weapon_dice_multiplier", 1}},
                                                {{"id", "giant"}, {"name", "Giant"}, {"size", "large"}}
                                            })}}}}});
    return m;
}
Json mage() {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = kMage;
    c["hit_points"]["average"] = 40;
    c["spellcasting"] = {{"ability", "int"}, {"caster_level", 9}, {"dc", 30}, {"slots", {{"1", 4}, {"2", 4}, {"4", 2}}},
                         {"spells", Json::array({"test.spell.moonbeam", "test.spell.blast", "test.spell.poly"})}};
    return c;
}
Json moonbeam() {
    Json j = load_json("tests/validate/fixtures/zones/spells/moonbeam.json");
    j["id"] = "test.spell.moonbeam";
    return j;
}
Json blast() {
    return {{"$schema", "spell@1"}, {"id", "test.spell.blast"}, {"name", "Blast"}, {"level", 1}, {"school", "evocation"},
            {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
            {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}},
            {"effects", Json::array({{{"kind", "save"}, {"ability", "dex"}, {"on_success", "none"},
                                      {"damage", {{"dice", "1d1+40"}, {"type", "force"}}}}})}};
}
Json poly_spell() {
    return {{"$schema", "spell@1"}, {"id", "test.spell.poly"}, {"name", "Polymorph"}, {"level", 4}, {"school", "transmutation"},
            {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
            {"components", {{"v", true}, {"s", true}}},
            {"duration", {{"kind", "hours"}, {"amount", 1}, {"concentration", true}}},
            {"effects", Json::array({{{"kind", "polymorph"}, {"forms", Json::array({"srd51.monster.wolf"})}}})}};
}

world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns) {
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
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    uint64_t seed;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns, uint64_t s = 1)
        : lib(&l), seed(s) {
        chunk = arena(spawns);
        std::string err;
        c = Combat::create(reg, chunk, l, s, {}, &err);
        INFO(err);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    const rules5e::Combatant& who(std::size_t i) const { return *rules5e::find(c->state(), id(i)); }
    SubmitResult settle(SubmitResult r) {
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void end_turn() {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        REQUIRE(settle(c->submit(e)).ok);
    }
    void advance_to(CombatantId w) {
        for (int i = 0; i < 16 && rules5e::current_actor(c->state()) != w; ++i) end_turn();
        REQUIRE(rules5e::current_actor(c->state()) == w);
    }
    SubmitResult use(CombatantId a, uint32_t feature, uint8_t form) {
        Command u;
        u.request = {a, ActionKind::UseFeature};
        u.request.feature_id = feature;
        u.request.form_archetype = form;
        return settle(c->submit(u));
    }
    SubmitResult revert(CombatantId a) {
        Command u;
        u.request = {a, ActionKind::Revert};
        return settle(c->submit(u));
    }
    int index_of(EventKind k, int from = 0) const {
        const auto& ev = c->events();
        for (int i = from; i < int(ev.size()); ++i)
            if (ev[std::size_t(i)].kind == SimEvent::Kind::Rules && ev[std::size_t(i)].rules.kind == k) return i;
        return -1;
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, seed, c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
        CHECK(event_stream_hash(again->events()) == event_stream_hash(c->events()));
    }
};
bool has_attack(const rules5e::Combatant& c, const char* id) {
    for (const auto& a : c.attacks) if (a.id == id) return true;
    return false;
}
const rules5e::Attack* attack_named(const rules5e::Combatant& c, const char* id) {
    for (const auto& a : c.attacks) if (a.id == id) return &a;
    return nullptr;
}
bool legal_attack(const Fight& f, CombatantId a, const char* id) {
    const auto& me = *rules5e::find(f.c->state(), a);
    for (const auto& r : rules5e::legal_actions(f.c->state())) {
        if (r.actor == a && r.kind == ActionKind::Attack && r.attack_index < me.attacks.size() && me.attacks[r.attack_index].id == id)
            return true;
    }
    return false;
}
uint32_t shape_feature(const Archetype& a) {
    for (std::size_t i = 0; i < a.features.size(); ++i)
        if (a.features[i].kind == "shapechanger") return uint32_t(i + 1);
    return 0;
}
ContentLibrary library() {
    ContentLibrary lib;
    for (const Json& sp : {moonbeam(), blast(), poly_spell()}) {
        auto e = lib.add_spell(sp);
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
    }
    // Polymorph's form is a beast already in the pack. The wolf has to be loaded before the spell is cast.
    REQUIRE_FALSE(lib.add(load_json("content/packs/srd51.core/monsters/wolf.json"), "monster"));
    for (const Json& m : {lycan(), oni(), mage()}) {
        auto e = lib.add(m, m["id"].get<std::string>().find("character") != std::string::npos ? "character" : "monster");
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
    }
    return lib;
}

Json old_shape(Json extra_effect = Json::object(), Json form_extra = Json::object()) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = "test.monster.oldshape";
    m["shapechanger"] = true;
    Json form = {{"id", "wolf"}, {"name", "Wolf"}, {"speed", {{"walk", 40}}}};
    for (auto it = form_extra.begin(); it != form_extra.end(); ++it) form[it.key()] = it.value();
    Json effect = {{"kind", "shapechanger"}, {"forms", Json::array({form})}};
    for (auto it = extra_effect.begin(); it != extra_effect.end(); ++it) effect[it.key()] = it.value();
    m["traits"].push_back({{"id", "shape"}, {"name", "Shapechanger"}, {"description", "x"}, {"effect", effect}});
    return m;
}

}  // namespace

TEST_CASE("shapechanger loader: monster@5 keys, and monster@4 / characters keep the old set") {
    auto err_of = [](Json d, const char* type) {
        ContentLibrary l;
        return l.add(d, type).value_or("");
    };
    CHECK(err_of(lycan(), "monster").empty());
    CHECK(err_of(oni(), "monster").empty());
    CHECK(err_of(old_shape(), "monster").empty());   // monster@4 still loads the old key set
    CHECK(err_of(old_shape({}, {{"speed", {{"walk", 40}, {"climb", 30}}}}), "monster").find("speed.climb needs a monster@5 file") != std::string::npos);
    CHECK(err_of(old_shape({{"trait", false}}), "monster").find("trait needs a monster@5 file") != std::string::npos);
    CHECK(err_of(old_shape({{"true_form", {{"actions", Json::array({"scimitar"})}}}}), "monster").find("true_form needs a monster@5 file") != std::string::npos);
    CHECK(err_of(old_shape({}, {{"senses", {{"darkvision", 60}}}}), "monster").find("senses needs a monster@5 file") != std::string::npos);
    CHECK(err_of(old_shape({}, {{"traits", Json::array({"nimble_escape"})}}), "monster").find("traits needs a monster@5 file") != std::string::npos);
    CHECK(err_of(old_shape({}, {{"weapon_dice_multiplier", 1}}), "monster").find("weapon_dice_multiplier needs a monster@5 file") != std::string::npos);
    CHECK(err_of(old_shape({}, {{"speed", {{"walk", -5}}}}), "monster").find("shapechanger bad speed.walk") != std::string::npos);
    Json bad_sense = lycan();
    bad_sense["traits"][2]["effect"]["forms"][0]["senses"] = {{"darkvision", -1}};
    CHECK(err_of(bad_sense, "monster").find("shapechanger bad senses.darkvision") != std::string::npos);
    Json bad_dice = oni();
    bad_dice["traits"][0]["effect"]["forms"][0]["weapon_dice_multiplier"] = 5;
    CHECK(err_of(bad_dice, "monster").find("shapechanger bad weapon_dice_multiplier") != std::string::npos);
    Json bad_trait = oni();
    bad_trait["traits"][0]["effect"]["trait"] = 1;
    CHECK(err_of(bad_trait, "monster").find("shapechanger bad trait") != std::string::npos);
    Json bad_fly = lycan();
    bad_fly["traits"][2]["effect"]["forms"][4]["speed"]["fly"] = 300;
    CHECK(err_of(bad_fly, "monster").find("shapechanger bad speed.fly") != std::string::npos);
    Json unknown_action = lycan();
    unknown_action["traits"][2]["effect"]["forms"][0]["actions"] = Json::array({"howl"});
    CHECK(err_of(unknown_action, "monster").find("names unknown action 'howl'") != std::string::npos);
    Json unknown_trait = lycan();
    unknown_trait["traits"][2]["effect"]["forms"][0]["traits"] = Json::array({"nope"});
    CHECK(err_of(unknown_trait, "monster").find("names unknown trait 'nope'") != std::string::npos);
    Json unknown_true = lycan();
    unknown_true["traits"][2]["effect"]["true_form"]["actions"] = Json::array({"howl"});
    CHECK(err_of(unknown_true, "monster").find("true_form names unknown action 'howl'") != std::string::npos);
    Json character = mage();
    character["shapechanger"] = true;
    character["features"].push_back({{"id", "shape"}, {"name", "Shapechanger"}, {"description", "x"},
                                     {"effect", {{"kind", "shapechanger"}, {"trait", false},
                                                 {"forms", Json::array({{{"id", "wolf"}, {"name", "Wolf"}}})}}}});
    CHECK(err_of(character, "character").find("trait needs a monster@5 file") != std::string::npos);
    Json no_fact = lycan();
    no_fact.erase("shapechanger");
    CHECK(err_of(no_fact, "monster").find("requires shapechanger: true") != std::string::npos);
}

TEST_CASE("shapechanger forms: statistics, true-form attacks, speed, senses, traits, and one-action switch") {
    ContentLibrary lib = library();
    const Archetype& a = *lib.find(kLycan);
    CHECK(a.base.shapechanger);
    const uint32_t fid = shape_feature(a);
    REQUIRE(fid != 0);
    Fight f(lib, {{kLycan, {4, 4}}, {kMage, {5, 4}}});
    const auto& born = f.who(0);
    CHECK(has_attack(born, "spear"));
    CHECK_FALSE(has_attack(born, "bite"));
    CHECK_FALSE(has_attack(born, "claws"));
    CHECK(born.abilities[rules5e::Ability::Str] == 17);
    CHECK((born.save_proficiencies & rules5e::bit(rules5e::Ability::Con)) != 0);
    CHECK((born.resistances & rules5e::bit(rules5e::DamageType::Fire)) != 0);
    CHECK(born.fly_ft == 30);
    CHECK(born.darkvision_ft == 0);
    bool charge = false, pounce = false;
    for (const auto& t : born.triggers) {
        charge = charge || t.owner_id == spell_id_of("charge");
        pounce = pounce || t.owner_id == spell_id_of("pounce");
    }
    CHECK_FALSE(charge);
    CHECK_FALSE(pounce);

    f.advance_to(f.id(0));
    CHECK_FALSE(legal_attack(f, f.id(0), "bite"));
    const int hp = f.who(0).hp;
    REQUIRE(f.use(f.id(0), fid, 1).ok);   // hybrid
    CHECK(f.who(0).form == FormKind::Shapechanger);
    CHECK(f.who(0).ac == 12);
    CHECK(f.who(0).abilities[rules5e::Ability::Str] == 17);
    CHECK(f.who(0).hp == hp);
    CHECK(has_attack(f.who(0), "bite"));
    CHECK(has_attack(f.who(0), "claws"));
    CHECK_FALSE(has_attack(f.who(0), "spear"));
    charge = pounce = false;
    for (const auto& t : f.who(0).triggers) {
        charge = charge || t.owner_id == spell_id_of("charge");
        pounce = pounce || t.owner_id == spell_id_of("pounce");
    }
    CHECK(charge);
    CHECK(pounce);
    f.end_turn();
    f.advance_to(f.id(0));
    CHECK(legal_attack(f, f.id(0), "bite"));
    CHECK_FALSE(legal_attack(f, f.id(0), "spear"));
    Command atk;
    atk.request = {f.id(0), ActionKind::Attack, f.id(1), 0};
    REQUIRE(f.settle(f.c->submit(atk)).ok);
    CHECK(f.index_of(EventKind::FeatureTriggered) >= 0);

    f.end_turn();
    f.advance_to(f.id(0));
    const auto events_before = int(f.c->events().size());
    auto sw = f.use(f.id(0), fid, 2);   // wolf, one action
    REQUIRE(sw.ok);
    const int rev = f.index_of(EventKind::Reverted, events_before);
    const int tr = f.index_of(EventKind::Transformed, events_before);
    REQUIRE(rev >= 0); REQUIRE(tr > rev);
    CHECK(f.c->events()[std::size_t(rev)].rules.b == 3);
    CHECK(has_attack(f.who(0), "bite"));
    CHECK_FALSE(has_attack(f.who(0), "claws"));
    CHECK(f.who(0).speed_ft == 40);
    CHECK(f.who(0).fly_ft == 30);   // omitted fly keeps the true form
    charge = false;
    for (const auto& t : f.who(0).triggers) charge = charge || t.owner_id == spell_id_of("charge");
    CHECK_FALSE(charge);
    CHECK(f.use(f.id(0), fid, 3).error == "no action available");

    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.revert(f.id(0)).ok);
    CHECK(f.who(0).form == FormKind::None);
    CHECK_FALSE(has_attack(f.who(0), "bite"));
    CHECK(has_attack(f.who(0), "spear"));
    CHECK(f.who(0).speed_ft == 30);
    CHECK(f.who(0).fly_ft == 30);
    CHECK_FALSE(f.c->state().budget.action);

    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 3).ok);   // rat: darkvision 60 only here
    CHECK(f.who(0).darkvision_ft == 60);
    rules5e::SightFacts facts;
    facts.distance_ft = 60;
    facts.subject.light = Light::Dark;
    auto seen = rules5e::derive_sight(f.who(0), facts);
    CHECK(seen.can_see);
    CHECK(seen.light_at_subject == Light::Dim);
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.revert(f.id(0)).ok);
    CHECK(f.who(0).darkvision_ft == 0);
    facts.distance_ft = 60;
    auto blind = rules5e::derive_sight(f.who(0), facts);
    CHECK(blind.light_at_subject == Light::Dark);

    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 4).ok);   // bear: climb 30
    CHECK(f.who(0).speed_ft == 40);
    CHECK(f.who(0).climb_ft == 30);
    CHECK(rules5e::move_allowance(f.c->state(), f.who(0), 3) != -1);
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.revert(f.id(0)).ok);
    CHECK(f.who(0).climb_ft == 0);

    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 5).ok);   // raven: fly 60
    CHECK(f.who(0).fly_ft == 60);
    CHECK(rules5e::move_allowance(f.c->state(), f.who(0), 1) != -1);
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 6).ok);   // humanoid: fly 0, a fly move is refused
    CHECK(f.who(0).fly_ft == 0);
    CHECK(rules5e::move_allowance(f.c->state(), f.who(0), 1) == -1);
    f.check_replay();
}

TEST_CASE("oni B5/B6: weapon dice follow the form, trait false is not a shapechanger, death restores the glaive") {
    ContentLibrary lib = library();
    const Archetype& a = *lib.find(kOni);
    CHECK_FALSE(a.base.shapechanger);
    const uint32_t fid = shape_feature(a);
    Fight f(lib, {{kOni, {4, 4}}, {kMage, {10, 10}}, {"srd51.monster.wolf", {2, 2}}});
    REQUIRE(attack_named(f.who(0), "glaive"));
    CHECK(attack_named(f.who(0), "glaive")->damage.count == 2);
    CHECK(attack_named(f.who(0), "claw")->damage.count == 1);
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 1).ok);   // medium
    CHECK(f.who(0).form == FormKind::Shapechanger);
    CHECK(attack_named(f.who(0), "glaive")->damage.count == 1);
    CHECK(attack_named(f.who(0), "glaive")->damage.sides == 10);
    CHECK(attack_named(f.who(0), "glaive")->damage.bonus == 4);
    CHECK(attack_named(f.who(0), "claw")->damage.count == 1);
    CHECK(attack_named(f.who(0), "claw")->damage.sides == 8);
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 2).ok);   // large giant: the monster multiplier is back
    CHECK(attack_named(f.who(0), "glaive")->damage.count == 2);
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.revert(f.id(0)).ok);
    CHECK(attack_named(f.who(0), "glaive")->damage.count == 2);

    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 1).ok);
    f.end_turn();
    f.advance_to(f.id(1));
    Command kill;
    kill.request = {f.id(1), ActionKind::CastSpell, f.id(0)};
    kill.request.spell_id = spell_id_of("test.spell.blast");
    kill.request.spell_level = 1;
    kill.request.targets = {f.id(0)};
    const int mark = int(f.c->events().size());
    auto died = f.settle(f.c->submit(kill));
    REQUIRE(died.ok);
    const int rev = f.index_of(EventKind::Reverted, mark);
    const int dead = f.index_of(EventKind::CombatantDied, mark);
    REQUIRE(rev >= 0); REQUIRE(dead > rev);
    CHECK(f.c->events()[std::size_t(rev)].rules.b == 4);
    CHECK(f.who(0).form == FormKind::None);
    CHECK(attack_named(f.who(0), "glaive")->damage.count == 2);

    // A fresh oni is not refused as a shapechanger. The wolf is a legal beast form.
    Fight g(lib, {{kOni, {4, 4}}, {kMage, {8, 8}}});
    g.advance_to(g.id(1));
    Command poly;
    poly.request = {g.id(1), ActionKind::CastSpell, g.id(0)};
    poly.request.spell_id = spell_id_of("test.spell.poly");
    poly.request.spell_level = 4;
    poly.request.targets = {g.id(0)};
    poly.request.form_archetype = 1;
    auto casted = g.settle(g.c->submit(poly));
    REQUIRE(casted.ok);
    CHECK(g.who(0).form == FormKind::Polymorph);

    // A Large form that does not fit is refused and the action is not spent.
    Fight tight(lib, {{kOni, {1, 1}}, {kMage, {2, 1}}});
    tight.advance_to(tight.id(0));
    const bool could = tight.c->state().budget.action;
    auto refused = tight.use(tight.id(0), fid, 2);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "form not available");
    CHECK(tight.who(0).form == FormKind::None);
    CHECK(tight.c->state().budget.action == could);
}

TEST_CASE("Moonbeam B7 through content: reason 6, the lock, 0xEA only while locked, replay") {
    ContentLibrary lib = library();
    const uint32_t fid = shape_feature(*lib.find(kLycan));
    Fight f(lib, {{kLycan, {4, 4}}, {kMage, {2, 2}}, {kOni, {6, 6}}});
    f.advance_to(f.id(0));
    REQUIRE(f.use(f.id(0), fid, 1).ok);
    f.end_turn();
    f.advance_to(f.id(2));
    const uint32_t oni_fid = shape_feature(*lib.find(kOni));
    REQUIRE(f.use(f.id(2), oni_fid, 1).ok);
    f.end_turn();
    f.advance_to(f.id(1));
    const uint64_t before = f.c->hash();
    Command beam;
    beam.request = {f.id(1), ActionKind::CastSpell, f.id(1)};
    beam.request.spell_id = spell_id_of("test.spell.moonbeam");
    beam.request.spell_level = 2;
    beam.request.has_zone_dest = true;
    beam.request.zone_x = 8;
    beam.request.zone_y = 4;
    REQUIRE(f.settle(f.c->submit(beam)).ok);
    CHECK(f.who(0).form_lock_zone == 0);
    CHECK(f.c->hash() != before);   // the zone itself folds under 0xAB; the lock does not
    f.end_turn();
    f.advance_to(f.id(0));
    const int mark = int(f.c->events().size());
    // size 5 ft covers Chebyshev 1 around (8, 4). (7, 4) is inside; (6, 4) is not.
    for (int x : {5, 6, 7}) {
        Command step;
        step.kind = CommandKind::Step;
        step.to = {x, 4};
        REQUIRE(f.settle(f.c->submit(step)).ok);
    }
    const int rev = f.index_of(EventKind::Reverted, mark);
    REQUIRE(rev >= 0);
    CHECK(f.c->events()[std::size_t(rev)].rules.b == 6);
    CHECK(f.who(0).form == FormKind::None);
    REQUIRE(f.who(0).form_lock_zone != 0);
    const uint64_t locked = f.c->hash();
    auto& st = const_cast<rules5e::EncounterState&>(f.c->state());
    for (auto& c : st.combatants) if (c.id == f.id(0)) {
        const uint32_t zone = c.form_lock_zone;
        c.form_lock_zone = 0;
        CHECK(f.c->hash() != locked);   // 0xEA is absent once nothing is locked
        c.form_lock_zone = zone;
        CHECK(f.c->hash() == locked);
    }
    CHECK(f.use(f.id(0), fid, 2).error == "can't assume a different form until you leave the light");
    CHECK(f.c->hash() == locked);
    Command out;
    out.kind = CommandKind::Step;
    out.to = {6, 4};
    REQUIRE(f.settle(f.c->submit(out)).ok);
    CHECK(f.who(0).form_lock_zone == 0);
    CHECK(f.c->hash() != locked);
    REQUIRE(f.use(f.id(0), fid, 2).ok);

    // The oni (trait false) walks into the same light and is not reverted or locked.
    f.end_turn();
    f.advance_to(f.id(2));
    for (Vec2i to : {Vec2i{7, 6}, Vec2i{8, 5}, Vec2i{8, 4}}) {
        Command step;
        step.kind = CommandKind::Step;
        step.to = to;
        auto r = f.settle(f.c->submit(step));
        REQUIRE(r.ok);
    }
    CHECK(f.who(2).form == FormKind::Shapechanger);
    CHECK(f.who(2).form_lock_zone == 0);
    f.check_replay();
}
