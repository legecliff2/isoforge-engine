// R-G12 transforms, sim side (VERSION_BUMP_BATCH 7.2q hand-off; SUMMONS_TRANSFORMS.md §3): wild_shape / shapechanger feature
// kinds, the build_form hook (CR / fly / swim limits, unknown archetypes, footprint fit), Combat::hash 0xAF, log_hash 0xBD, the
// 0 HP revert order, the encounter-end teardown and the v10 rule-7 gate, replay, and the AI's content-driven Wild Shape.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <initializer_list>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/rest.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::FormKind;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kDruid = "test.character.druid";
constexpr uint32_t kWildShape = 1;   // feature ids: features[] order
constexpr uint32_t kHumanoidOnly = 2;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json beast(const std::string& id, const char* cr, int hp, Json speed, const char* size = "medium") {
    Json w = load_json("content/packs/srd51.core/monsters/wolf.json");
    w["id"] = id;
    w["challenge"]["rating"] = cr;
    w["hit_points"]["average"] = hp;
    w["speed"] = std::move(speed);
    w["size"] = size;
    return w;
}
// forms: 0 wolf (1/4), 1 hawk (1/8, fly), 2 croc (1/2, swim), 3 bear (1, Large), 4 missing, 5 goblin (not a beast), 6 mouse (0, hp 1)
Json druid(int level, Json extra = Json::object()) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = kDruid;
    c["classes"] = Json::array({{{"class", "druid"}, {"level", level}, {"hit_die", "d8"}}});
    c["hit_points"]["average"] = 40;
    Json ws = {{"kind", "wild_shape"},
               {"forms", {"test.monster.wolf", "test.monster.hawk", "test.monster.croc", "test.monster.bear", "test.monster.missing",
                          "test.monster.gob", "test.monster.mouse"}},
               {"cost", "action"},
               {"pool", "wild_shape"},
               {"max_cr_by_level", Json::array({{{"level", 2}, {"max_cr", "1/4"}}, {{"level", 4}, {"max_cr", "1/2"}}, {{"level", 8}, {"max_cr", "1"}}})}};
    for (auto it = extra.begin(); it != extra.end(); ++it) ws[it.key()] = it.value();
    c["resources"] = Json::array({{{"id", "wild_shape"}, {"max", 2}, {"recharge", "short_rest"}}});
    Json hum = c["features"][1];   // Second Wind, marked as needing hands (common@2 trait flag: on the feature, not its effect)
    hum["id"] = "hum";
    hum["requires_humanoid_form"] = true;
    c["features"] = Json::array({{{"id", "ws"}, {"name", "Wild Shape"}, {"description", "x"}, {"effect", ws}}, hum});
    return c;
}
Json caster() {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = "test.character.mage";
    c["classes"] = Json::array({{{"class", "wizard"}, {"level", 3}, {"hit_die", "d6"}}});
    c["abilities"]["int"] = 16;
    c["hit_points"]["average"] = 40;
    c["spellcasting"] = {{"ability", "int"}, {"caster_level", 3}, {"slots", {{"1", 4}}}, {"spells", {"test.spell.zap"}}};
    return c;
}
Json zap() {   // a save spell that always deals at least 1 (half of 2d6, floor)
    return {{"$schema", "spell@1"}, {"id", "test.spell.zap"}, {"name", "zap"}, {"level", 1}, {"school", "evocation"},
            {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
            {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}},
            {"effects", Json::array({{{"kind", "save"}, {"ability", "dex"}, {"on_success", "half"},
                                      {"damage", {{"dice", "2d6"}, {"type", "fire"}}}}})}};
}
std::optional<std::string> build(ContentLibrary& lib, const Json& d, int gob_hp = 300) {
    for (const Json& b : {beast("test.monster.wolf", "1/4", 11, {{"walk", 40}}), beast("test.monster.hawk", "1/8", 5, {{"walk", 10}, {"fly", 60}}),
                          beast("test.monster.croc", "1/2", 19, {{"walk", 20}, {"swim", 30}}),
                          beast("test.monster.bear", "1", 34, {{"walk", 40}}, "large"), beast("test.monster.mouse", "0", 1, {{"walk", 20}})})
        if (auto e = lib.add(b, "monster")) return e;
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = "test.monster.gob";
    g["hit_points"]["average"] = gob_hp;
    if (auto e = lib.add(g, "monster")) return e;
    if (auto e = lib.add_spell(zap())) return e;
    if (auto e = lib.add(caster(), "character")) return e;
    return lib.add(d, "character");
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
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns, uint64_t seed = 1) : lib(&l) {
        chunk = arena(spawns);
        c = Combat::create(reg, chunk, l, seed);
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
    SubmitResult wild_shape(uint8_t form) {
        Command u;
        u.request = {id(0), ActionKind::UseFeature};
        u.request.feature_id = kWildShape;
        u.request.form_archetype = form;
        return settle(c->submit(u));
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, c->seed(), c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
        CHECK(event_stream_hash(again->events()) == event_stream_hash(c->events()));
    }
    void refused(uint8_t form) {
        const uint64_t h = c->hash(), lh = c->log_hash();
        const std::size_t n = c->log().size(), ne = c->events().size();
        CHECK_FALSE(wild_shape(form).ok);
        CHECK(c->hash() == h);
        CHECK(c->log_hash() == lh);
        CHECK(c->log().size() == n);
        CHECK(c->events().size() == ne);
        CHECK(who(0).form == FormKind::None);
    }
};
const Archetype* loaded(ContentLibrary& lib, const Json& d) {
    auto e = build(lib, d);
    if (e) MESSAGE(*e);
    REQUIRE_FALSE(e);
    return lib.find(kDruid);
}
std::size_t index_of(const std::vector<SimEvent>& ev, std::size_t from, EventKind k) {
    for (std::size_t i = from; i < ev.size(); ++i)
        if (ev[i].kind == SimEvent::Kind::Rules && ev[i].rules.kind == k) return i;
    return ev.size();
}
}  // namespace

TEST_CASE("wild_shape content: FeatureSpec, pool uses, duration, blocked features, load errors") {
    ContentLibrary lib;
    const Archetype& a = *loaded(lib, druid(4));
    const auto& spec = a.feature_specs.at(kWildShape);
    CHECK(spec.form == uint8_t(FormKind::WildShape));
    CHECK(spec.form_choices == 7);
    CHECK(spec.form_rounds == 1200);   // 600 * (4 / 2): 2 hours
    CHECK(spec.cost == 1);
    CHECK(spec.pool == spell_id_of("wild_shape"));   // uses and recharge come from the resources[] pool, 1 per use
    CHECK(spec.pool_cost == 1);
    REQUIRE(a.active.size() >= 1);
    CHECK(a.active[0].uses == 0);
    CHECK(a.active[0].recharge == Recharge::None);
    CHECK(a.form_features.at(kWildShape).no_fly_below == 8);   // defaults
    CHECK(a.form_features.at(kWildShape).no_swim_below == 4);
    CHECK(a.form_features.at(kWildShape).max_cr_x8 == 4);   // druid 4: CR 1/2
    CHECK(a.humanoid_only_features == std::vector<uint32_t>{kHumanoidOnly});
    CHECK(a.char_level == 4);
    CHECK(lib.find("test.monster.croc")->cr_x8 == 4);
    CHECK(lib.find("test.monster.croc")->swim_ft == 30);
    CHECK(lib.find("test.monster.hawk")->fly_ft == 60);
    CHECK(lib.find("test.monster.gob")->cr_x8 == 2);
    {   // druid level 1: no Wild Shape
        ContentLibrary l2;
        CHECK(build(l2, druid(1)));
    }
    {   // no forms
        ContentLibrary l2;
        Json d = druid(2);
        d["features"][0]["effect"].erase("forms");
        CHECK(build(l2, d));
    }
    {   // bad cost
        ContentLibrary l3;
        CHECK(build(l3, druid(2, {{"cost", "free"}})));
    }
    auto err = [](const Json& d) {
        ContentLibrary l;
        return build(l, d).value_or("");
    };
    // the old sim-only keys and a duration key are unknown keys (common@2 wild_shape_effect is closed)
    for (const char* k : {"uses", "recharge", "ai", "duration", "duration_hours"}) {
        INFO(k);
        CHECK(err(druid(2, {{k, 2}})).find(std::string("wild_shape effect: unknown key '") + k + "'") != std::string::npos);
    }
    CHECK(err(druid(2, {{"max_cr_by_level", Json::array({{{"level", 2}, {"cr", "1/4"}}})}})).find("unknown key 'cr'") != std::string::npos);
    CHECK(err(druid(2, {{"max_cr_by_level", Json::array({{{"level", 2}, {"max_cr", Json::parse("0.25")}}})}})).find("CR string") != std::string::npos);
    CHECK(err(druid(2, {{"max_cr_by_level", Json::array({{{"level", 4}, {"max_cr", "1/2"}}, {{"level", 2}, {"max_cr", "1/4"}}})}}))
              .find("strictly ascending") != std::string::npos);
    {
        Json d = druid(2);
        d["features"][0]["effect"].erase("max_cr_by_level");
        CHECK(err(d).find("needs max_cr_by_level") != std::string::npos);
        d = druid(2);
        d["features"][0]["effect"].erase("pool");
        CHECK(err(d).find("needs pool") != std::string::npos);
        d = druid(2);
        d["resources"][0]["recharge"] = "long_rest";
        CHECK(err(d).find("must recharge on short_rest") != std::string::npos);
        CHECK(err(druid(2, {{"pool", "nope"}})).find("unknown (or unlimited) pool 'nope'") != std::string::npos);
        CHECK(err(druid(2, {{"no_fly_below", 0}})).find("no_fly_below must be 1..20") != std::string::npos);
        d = druid(2);
        d["features"][0]["requires_humanoid_form"] = true;
        CHECK(err(d).find("requires_humanoid_form on the wild_shape feature itself") != std::string::npos);
    }
    {   // class: another class's level drives the limits; a druid-less class list fails
        ContentLibrary l4;
        Json d = druid(2, {{"class", "ranger"}});
        d["classes"] = Json::array({{{"class", "ranger"}, {"level", 8}, {"hit_die", "d10"}}});
        REQUIRE_FALSE(build(l4, d));
        const Archetype& r = *l4.find(kDruid);
        CHECK(r.form_features.at(kWildShape).level == 8);
        CHECK(r.form_features.at(kWildShape).max_cr_x8 == 8);
        CHECK(r.feature_specs.at(kWildShape).form_rounds == 2400);
        CHECK(err(druid(4, {{"class", "ranger"}})).find("needs ranger level 2..20") != std::string::npos);
    }
}

TEST_CASE("Wild Shape: transform and revert, 0xAF / 0xBD folds, v10 stream, replay") {
    ContentLibrary lib;
    loaded(lib, druid(2));
    Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
    f.advance_to(f.id(0));
    const uint64_t h0 = f.c->hash(), lh0 = f.c->log_hash();
    CHECK_FALSE(rules5e::has_summon_state(f.c->state()));
    CHECK(event_stream_version(f.c->events()) < 10);
    const std::size_t from = f.c->events().size();
    REQUIRE(f.wild_shape(1).ok);   // wolf
    const auto& me = f.who(0);
    CHECK(me.form == FormKind::WildShape);
    CHECK(me.hp == 11);
    CHECK(me.hp_max == 11);
    CHECK(me.speed_ft == 40);
    CHECK(me.abilities.score[4] == lib.find(kDruid)->base.abilities.score[4]);   // Wis kept
    const auto& ev = f.c->events();
    const std::size_t fu = index_of(ev, from, EventKind::FeatureUsed), tr = index_of(ev, from, EventKind::Transformed);
    REQUIRE(tr < ev.size());
    CHECK(fu < tr);
    CHECK(ev[tr].rules.b == 0);
    CHECK(ev[tr].rules.c == int32_t(FormKind::WildShape));
    CHECK(event_stream_version(ev) == 10);
    CHECK(rules5e::has_summon_state(f.c->state()));
    CHECK(f.c->hash() != h0);
    CHECK(f.c->log_hash() != lh0);
    CHECK(f.c->attack_archetype(f.id(0)) == lib.find("test.monster.wolf"));
    // the humanoid-only feature is blocked in the form
    Command hum;
    hum.request = {f.id(0), ActionKind::UseFeature};
    hum.request.feature_id = kHumanoidOnly;
    CHECK_FALSE(f.settle(f.c->submit(hum)).ok);
    // revert by choice: a bonus action
    Command rv;
    rv.request = {f.id(0), ActionKind::Revert};
    rv.request.as_bonus_action = true;
    const std::size_t before_rv = f.c->events().size();
    REQUIRE(f.settle(f.c->submit(rv)).ok);
    CHECK(f.who(0).form == FormKind::None);
    CHECK(f.who(0).hp == 40);
    const std::size_t r = index_of(f.c->events(), before_rv, EventKind::Reverted);
    REQUIRE(r < f.c->events().size());
    CHECK(f.c->events()[r].rules.b == 3);
    CHECK(rules5e::has_summon_state(f.c->state()));   // stays folded after the revert
    f.check_replay();
}

TEST_CASE("Wild Shape: unknown archetypes and out-of-limit forms are refused and change nothing") {
    {
        ContentLibrary lib;
        loaded(lib, druid(2));   // max CR 1/4, no fly, no swim
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
        f.advance_to(f.id(0));
        std::string why;
        CHECK_FALSE(f.c->build_form(f.who(0), FormKind::WildShape, kWildShape, 1, &why));
        CHECK(why == "flying form below the druid level");
        CHECK_FALSE(f.c->build_form(f.who(0), FormKind::WildShape, kWildShape, 2, &why));
        CHECK(why == "form above the druid's max CR");
        CHECK_FALSE(f.c->build_form(f.who(0), FormKind::WildShape, kWildShape, 4, &why));
        CHECK(why == "unknown form archetype");
        CHECK_FALSE(f.c->build_form(f.who(0), FormKind::WildShape, kWildShape, 5, &why));
        CHECK(why == "form is not a beast");
        for (uint8_t form : std::initializer_list<uint8_t>{2, 3, 4, 5, 6, 8}) {   // hawk, croc, bear, missing, goblin, past the list
            CAPTURE(int(form));
            f.refused(form);
        }
        f.refused(0);   // form_archetype required
        REQUIRE(f.wild_shape(1).ok);   // the wolf is fine
    }
    {   // druid 4: swimming allowed (croc, CR 1/2), still no flying
        ContentLibrary lib;
        loaded(lib, druid(4));
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
        f.advance_to(f.id(0));
        f.refused(2);
        REQUIRE(f.wild_shape(3).ok);
    }
}

TEST_CASE("loader: Combatant::size comes from the stat block (Medium and Large monster, character), footprints agree") {
    ContentLibrary lib;
    loaded(lib, druid(2));
    CHECK(lib.find("test.monster.wolf")->base.size == rules5e::Size::Medium);
    CHECK(lib.find("test.monster.bear")->base.size == rules5e::Size::Large);
    CHECK(lib.find("test.monster.gob")->base.size == rules5e::Size::Small);
    CHECK(lib.find(kDruid)->base.size == rules5e::Size::Medium);
    Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.bear", {10, 10}}, {"test.monster.wolf", {20, 20}}});
    CHECK(f.who(1).size == rules5e::Size::Large);
    CHECK(f.c->footprint_size(f.id(1)) == 2);
    CHECK(f.who(2).size == rules5e::Size::Medium);
    CHECK(f.c->footprint_size(f.id(2)) == 1);
}

TEST_CASE("Wild Shape into a Large form: the footprint must fit at the anchor; Footprint events on the size change") {
    ContentLibrary lib;
    loaded(lib, druid(8));   // CR 1: the Large bear
    {   // a creature on a tile the 2x2 would need: not available
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {3, 3}}});
        f.advance_to(f.id(0));
        std::string why;
        CHECK_FALSE(f.c->build_form(f.who(0), FormKind::WildShape, kWildShape, 3, &why));
        CHECK(why == "form does not fit here");
        f.refused(4);
    }
    Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
    f.advance_to(f.id(0));
    const std::size_t from = f.c->events().size();
    REQUIRE(f.wild_shape(4).ok);
    CHECK(f.c->footprint_size(f.id(0)) == 2);
    CHECK(f.who(0).size == rules5e::Size::Large);   // the form's size
    const auto& ev = f.c->events();
    bool fp = false;
    for (std::size_t i = from; i < ev.size(); ++i)
        if (ev[i].kind == SimEvent::Kind::Footprint && ev[i].actor == f.id(0)) {
            fp = true;
            CHECK(ev[i].rules.a == 2);
            CHECK(ev[i].tile == Vec2i{2, 2});
            CHECK(i > index_of(ev, from, EventKind::Transformed));   // sim events after the step's rules events
        }
    CHECK(fp);
    Command rv;
    rv.request = {f.id(0), ActionKind::Revert};
    rv.request.as_bonus_action = true;
    const std::size_t r0 = f.c->events().size();
    REQUIRE(f.settle(f.c->submit(rv)).ok);
    CHECK(f.c->footprint_size(f.id(0)) == 1);
    CHECK(f.who(0).size == rules5e::Size::Medium);   // revert restores the stat block size
    CHECK(f.c->events().back().kind == SimEvent::Kind::Footprint);
    CHECK(f.c->events().back().rules.a == 1);
    CHECK(f.c->events().size() > r0);
    f.check_replay();
}

TEST_CASE("0 HP in a form: DamageDealt (what the form absorbed), Reverted{b = 2, c = overflow}, DamageDealt (the overflow)") {
    ContentLibrary lib;
    loaded(lib, druid(2));
    bool seen = false;
    for (uint64_t seed = 1; seed < 40 && !seen; ++seed) {
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {3, 2}}}, seed);
        f.advance_to(f.id(0));
        REQUIRE(f.wild_shape(7).ok);   // mouse: 1 hp
        for (int round = 0; round < 12 && f.who(0).form != FormKind::None; ++round) {
            f.advance_to(f.id(1));
            Command a;
            a.request = {f.id(1), ActionKind::Attack, f.id(0)};
            const std::size_t from = f.c->events().size();
            REQUIRE(f.settle(f.c->submit(a)).ok);
            const auto& ev = f.c->events();
            const std::size_t r = index_of(ev, from, EventKind::Reverted);
            if (r == ev.size()) continue;
            seen = true;
            REQUIRE(r >= 1);
            const auto& first = ev[r - 1].rules;
            CHECK(first.kind == EventKind::DamageDealt);
            CHECK(first.target == f.id(0));
            CHECK(first.a == 1);   // all the mouse had
            CHECK(first.a == 1);   // what the form absorbed (mouse hp 1, no temp HP); first.a + Reverted.c = the whole hit
            CHECK(ev[r].rules.b == 2);
            const int overflow = ev[r].rules.c;
            if (overflow > 0) {
                REQUIRE(r + 1 < ev.size());
                CHECK(ev[r + 1].rules.kind == EventKind::DamageDealt);
                CHECK(ev[r + 1].rules.a == overflow);
                CHECK(ev[r + 1].rules.b == 40 - overflow);
                CHECK(f.who(0).hp == 40 - overflow);
            } else {
                CHECK(f.who(0).hp == 40);
            }
            CHECK(f.who(0).form == FormKind::None);
            f.check_replay();
            break;
        }
    }
    CHECK(seen);
}

TEST_CASE("encounter end: teardown Reverted{b = 5} in ascending id, sim events before EncounterEnded (v10 gate)") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, druid(8), 1));   // the goblin has 1 hp
    // two druids (ids ascending by spawn order) transform; the mage's zap ends the fight
    Fight f(lib, {{kDruid, {2, 2}}, {kDruid, {8, 2}}, {"test.character.mage", {5, 8}}, {"test.monster.gob", {5, 14}}});
    for (std::size_t d : {std::size_t(0), std::size_t(1)}) {
        f.advance_to(f.id(d));
        Command u;
        u.request = {f.id(d), ActionKind::UseFeature};
        u.request.feature_id = kWildShape;
        u.request.form_archetype = d == 0 ? 4 : 1;   // bear (Large) and wolf
        REQUIRE(f.settle(f.c->submit(u)).ok);
    }
    f.advance_to(f.id(2));
    Command z;
    z.request = {f.id(2), ActionKind::CastSpell, f.id(3)};
    z.request.spell_id = spell_id_of("test.spell.zap");
    z.request.spell_level = 1;
    z.request.targets = {f.id(3)};
    const std::size_t from = f.c->events().size();
    REQUIRE(f.settle(f.c->submit(z)).ok);
    REQUIRE(f.c->ended());
    const auto& ev = f.c->events();
    REQUIRE(ev.back().kind == SimEvent::Kind::Rules);
    CHECK(ev.back().rules.kind == EventKind::EncounterEnded);   // rule 7: last
    std::vector<CombatantId> reverted;
    std::size_t spell_target = ev.size(), first_teardown = ev.size();
    for (std::size_t i = from; i < ev.size(); ++i) {
        if (ev[i].kind == SimEvent::Kind::SpellTarget) spell_target = i;
        if (ev[i].kind == SimEvent::Kind::Rules && ev[i].rules.kind == EventKind::Reverted) {
            CHECK(ev[i].rules.b == 5);
            CHECK(ev[i].rules.c == 0);
            reverted.push_back(ev[i].rules.actor);
            if (first_teardown == ev.size()) first_teardown = i;
        }
    }
    const CombatantId lo = std::min(f.id(0), f.id(1)), hi = std::max(f.id(0), f.id(1));
    CHECK(reverted == std::vector<CombatantId>{lo, hi});
    REQUIRE(spell_target < ev.size());
    CHECK(spell_target < first_teardown);   // the step's own sim events come before the teardown
    // the bear's shrink is inline: Footprint{a = 1} directly after the bear's Reverted, and before EncounterEnded
    std::size_t bear_rev = ev.size();
    for (std::size_t i = first_teardown; i < ev.size(); ++i)
        if (ev[i].kind == SimEvent::Kind::Rules && ev[i].rules.kind == EventKind::Reverted && ev[i].rules.actor == f.id(0)) bear_rev = i;
    REQUIRE(bear_rev + 1 < ev.size() - 1);
    CHECK(ev[bear_rev + 1].kind == SimEvent::Kind::Footprint);
    CHECK(ev[bear_rev + 1].actor == f.id(0));
    CHECK(ev[bear_rev + 1].rules.a == 1);
    for (std::size_t i = 0; i < 2; ++i) CHECK(f.who(i).form == FormKind::None);
    f.check_replay();
}

TEST_CASE("pre-v10 streams keep today's ending order (sim events after EncounterEnded), no 0xAF / 0xBD") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, druid(8), 1));
    Fight f(lib, {{"test.character.mage", {5, 8}}, {"test.monster.gob", {5, 14}}});
    f.advance_to(f.id(0));
    Command z;
    z.request = {f.id(0), ActionKind::CastSpell, f.id(1)};
    z.request.spell_id = spell_id_of("test.spell.zap");
    z.request.spell_level = 1;
    z.request.targets = {f.id(1)};
    REQUIRE(f.settle(f.c->submit(z)).ok);
    REQUIRE(f.c->ended());
    const auto& ev = f.c->events();
    CHECK(event_stream_version(ev) < 10);
    CHECK(ev.back().kind == SimEvent::Kind::SpellTarget);   // the known exception (SIM_EVENTS.md rule 7), unchanged
    CHECK(ev[ev.size() - 2].rules.kind == EventKind::EncounterEnded);
    CHECK_FALSE(rules5e::has_summon_state(f.c->state()));
    f.check_replay();
}

TEST_CASE("Polymorph choices: rated beasts in id order, capped by the target's CR / level") {
    ContentLibrary lib;
    loaded(lib, druid(2));
    Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
    const auto& forms = f.c->polymorph_forms();
    REQUIRE(forms.size() >= 5);
    for (std::size_t i = 1; i < forms.size(); ++i) CHECK(forms[i - 1]->id < forms[i]->id);
    const uint32_t poly = spell_id_of("test.spell.polymorph");
    for (std::size_t i = 0; i < forms.size(); ++i) {
        CAPTURE(forms[i]->id);
        // the goblin is CR 1/4; the level 2 druid character counts as 2
        CHECK(bool(f.c->build_form(f.who(1), FormKind::Polymorph, poly, uint8_t(i))) == (forms[i]->cr_x8 <= 2));
        CHECK(bool(f.c->build_form(f.who(0), FormKind::Polymorph, poly, uint8_t(i))) == (forms[i]->cr_x8 <= 16));
    }
    CHECK_FALSE(f.c->build_form(f.who(1), FormKind::Polymorph, poly, uint8_t(forms.size())));
}

// AI Wild Shape heuristic (driver.cpp; Director ruling (b): engine-side, no content key): usable (pool, economy), not
// concentrating, and (a visible hostile within 2 tiles or no spell slots left); the highest-CR allowed form, ties by forms[] order.
namespace {
Json caster_druid(int level, int slots, Json extra = Json::object()) {
    Json d = druid(level, std::move(extra));
    d["abilities"]["wis"] = 20;
    d["spellcasting"] = {{"ability", "wis"}, {"caster_level", level}, {"slots", {{"1", slots}}},
                         {"spells", {"test.spell.zap", "srd51.spell.hold_person"}}};
    return d;
}
std::optional<std::string> build_ai(ContentLibrary& lib, const Json& d) {
    if (auto e = lib.add(beast("test.monster.wolf2", "1/4", 11, {{"walk", 40}}), "monster")) return e;
    if (auto e = lib.add_spell(load_json("content/packs/srd51.core/spells/hold_person.json"))) return e;
    return build(lib, d);
}
bool wild_shapes(const Command& cmd) { return cmd.request.kind == ActionKind::UseFeature && cmd.request.feature_id == kWildShape; }
}  // namespace

TEST_CASE("AI Wild Shape: in melee (a visible hostile within 2 tiles), into the highest allowed CR; replay") {
    for (int gx : {4, 5}) {   // 2 tiles: transforms; 3 tiles (slots left): does not
        ContentLibrary lib;
        REQUIRE_FALSE(build_ai(lib, caster_druid(4, 2)));
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {gx, 2}}});
        f.advance_to(f.id(0));
        const Command cmd = next_command(*f.c, f.id(1));
        INFO(gx);
        if (gx == 5) { CHECK_FALSE(wild_shapes(cmd)); continue; }
        REQUIRE(wild_shapes(cmd));
        CHECK(cmd.request.form_archetype == 3);   // croc, CR 1/2 (druid 4 swims; the hawk flies, the bear is CR 1)
        REQUIRE(f.settle(f.c->submit(cmd)).ok);
        CHECK(f.who(0).form == FormKind::WildShape);
        NearestEnemyPolicy pol;   // in the form it does not transform again; no rejected command
        const RunResult rr = run_to_end(*f.c, [&](CombatantId) -> IActorPolicy* { return &pol; }, 400);
        INFO(rr.error);
        CHECK(rr.error.empty());
        f.check_replay();
    }
}

TEST_CASE("AI Wild Shape: far away, it transforms only when its spell slots are spent; replay") {
    {
        ContentLibrary lib;
        REQUIRE_FALSE(build_ai(lib, caster_druid(4, 1)));
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
        f.advance_to(f.id(0));
        CHECK_FALSE(wild_shapes(next_command(*f.c, f.id(1))));   // a slot left, nobody near
    }
    ContentLibrary lib;
    REQUIRE_FALSE(build_ai(lib, caster_druid(4, 1)));
    Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {10, 2}}});   // 8 tiles: in zap range, far from melee
    f.advance_to(f.id(0));
    CHECK_FALSE(wild_shapes(next_command(*f.c, f.id(1))));
    Command z;   // spend the last slot
    z.request = {f.id(0), ActionKind::CastSpell, f.id(1)};
    z.request.spell_id = spell_id_of("test.spell.zap");
    z.request.spell_level = 1;
    z.request.targets = {f.id(1)};
    REQUIRE(f.settle(f.c->submit(z)).ok);
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(f.who(0).spell_slots[1] == 0);
    REQUIRE(f.c->distance_tiles(f.id(0), f.id(1)) > 2);
    const Command cmd = next_command(*f.c, f.id(1));
    REQUIRE(wild_shapes(cmd));
    CHECK(cmd.request.form_archetype == 3);
    REQUIRE(f.settle(f.c->submit(cmd)).ok);
    NearestEnemyPolicy pol;
    const RunResult rr = run_to_end(*f.c, [&](CombatantId) -> IActorPolicy* { return &pol; }, 400);
    INFO(rr.error);
    CHECK(rr.error.empty());
    f.check_replay();
    {   // no spellcasting at all counts as spent
        ContentLibrary l2;
        REQUIRE_FALSE(build_ai(l2, druid(4)));
        Fight g(l2, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
        g.advance_to(g.id(0));
        CHECK(wild_shapes(next_command(*g.c, g.id(1))));
    }
}

TEST_CASE("AI Wild Shape: not with an empty pool, not while concentrating") {
    {   // empty pool, in melee and out of slots: no
        ContentLibrary lib;
        Json d = druid(4);   // no slots
        d["resources"][0]["start"] = 0;
        REQUIRE_FALSE(build_ai(lib, d));
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {3, 2}}});
        f.advance_to(f.id(0));
        CHECK_FALSE(f.c->can_use_feature(f.id(0), kWildShape));
        CHECK_FALSE(wild_shapes(next_command(*f.c, f.id(1))));
    }
    ContentLibrary lib;
    Json cd = caster_druid(4, 1);
    cd["spellcasting"]["slots"] = {{"2", 1}};   // Hold Person is level 2
    REQUIRE_FALSE(build_ai(lib, cd));
    Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {3, 2}}}, 3);
    f.advance_to(f.id(0));
    Command hp;
    hp.request = {f.id(0), ActionKind::CastSpell, f.id(1)};
    hp.request.spell_id = spell_id_of("srd51.spell.hold_person");
    hp.request.spell_level = 2;
    hp.request.targets = {f.id(1)};
    const SubmitResult hr = f.settle(f.c->submit(hp));
    INFO(hr.error);
    REQUIRE(hr.ok);
    f.end_turn();
    f.advance_to(f.id(0));
    if (f.who(0).concentration_spell != 0) {   // concentrating (slots now spent, hostile adjacent): still no
        CHECK(f.who(0).spell_slots[2] == 0);
        CHECK_FALSE(wild_shapes(next_command(*f.c, f.id(1))));
    } else {
        FAIL("hold_person concentration did not hold; pick another seed");
    }
    f.check_replay();
}

TEST_CASE("AI Wild Shape: equal-CR forms tie-break by forms[] order") {
    for (int order = 0; order < 2; ++order) {
        ContentLibrary lib;
        Json forms = order == 0 ? Json::array({"test.monster.wolf", "test.monster.wolf2"}) : Json::array({"test.monster.wolf2", "test.monster.wolf"});
        REQUIRE_FALSE(build_ai(lib, druid(2, {{"forms", forms}})));
        Fight f(lib, {{kDruid, {2, 2}}, {"test.monster.gob", {20, 20}}});
        f.advance_to(f.id(0));
        const Command cmd = next_command(*f.c, f.id(1));
        REQUIRE(wild_shapes(cmd));
        CHECK(cmd.request.form_archetype == 1);   // both CR 1/4: the first listed
        REQUIRE(f.settle(f.c->submit(cmd)).ok);
        CHECK(f.c->attack_archetype(f.id(0))->id == forms[0].get<std::string>());
        f.check_replay();
    }
}

TEST_CASE("shapechanger trait: a listed form changes only the listed statistics, one HP pool, Polymorph refuses it") {
    ContentLibrary lib;
    loaded(lib, druid(2));
    Json w = load_json("content/packs/srd51.core/monsters/goblin.json");
    w["$schema"] = "monster@4";   // shapechanger is live at @4 only (a @3 file lists it inert)
    w["id"] = "test.monster.were";
    w["hit_points"]["average"] = 30;
    w["shapechanger"] = true;
    w["traits"] = Json::array({{{"id", "shape"}, {"name", "Shapechanger"}, {"description", "x"},
                                {"effect", {{"kind", "shapechanger"},
                                            {"forms", Json::array({{{"id", "wolf"}, {"name", "Wolf"}, {"armor_class", 12}, {"speed", {{"walk", 40}}}},
                                                                   {{"id", "hybrid"}, {"name", "Hybrid"}, {"size", "large"}}})}}}}});
    REQUIRE_FALSE(lib.add(w, "monster"));
    const Archetype& a = *lib.find("test.monster.were");
    CHECK(a.base.shapechanger);
    const uint32_t fid = uint32_t(a.features.size());   // traits come after features
    REQUIRE(a.feature_specs.count(fid));
    CHECK(a.feature_specs.at(fid).form == uint8_t(FormKind::Shapechanger));
    CHECK(a.feature_specs.at(fid).form_choices == 2);
    Fight f(lib, {{"test.monster.were", {2, 2}}, {kDruid, {20, 20}}});
    f.advance_to(f.id(0));
    Command u;
    u.request = {f.id(0), ActionKind::UseFeature};
    u.request.feature_id = fid;
    u.request.form_archetype = 1;
    REQUIRE(f.settle(f.c->submit(u)).ok);
    const auto& me = f.who(0);
    CHECK(me.form == FormKind::Shapechanger);
    CHECK(me.ac == 12);
    CHECK(me.speed_ft == 40);
    CHECK(me.hp == 30);
    CHECK(me.attacks.size() == a.base.attacks.size());   // equipment is not transformed
    CHECK(f.c->attack_archetype(f.id(0)) == &a);
    f.check_replay();
}

TEST_CASE("schema fixture: the validator's druid_4.json (common@2 wild_shape) loads; pool uses, short_rest recharge, max CR by level") {
    // tests/validate/fixtures/v4/extra/druid_4.json is validated by test_v4_validator.py (RG12Transforms, CI content_validate_v4):
    // loading the same file here keeps the sim loader and the schema on the same keys.
    const Json doc = load_json("tests/validate/fixtures/v4/extra/druid_4.json");
    const std::string id = doc["id"].get<std::string>();
    auto lib_for = [&](int level) {
        auto lib = std::make_unique<ContentLibrary>();
        for (const char* m : {"jackal", "wolf", "black_bear", "dire_wolf", "goblin"})
            REQUIRE_FALSE(lib->add(load_json(std::string("content/packs/srd51.core/monsters/") + m + ".json"), "monster"));
        Json d = doc;
        d["classes"][0]["level"] = level;
        d["level"] = level;
        const auto e = lib->add(d, "character");
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
        return lib;
    };
    const auto lib = lib_for(4);
    const Archetype& a = *lib->find(id);
    const uint32_t ws = 1, focus = 2;   // features[] order
    const auto& spec = a.feature_specs.at(ws);
    CHECK(spec.form == uint8_t(FormKind::WildShape));
    CHECK(spec.form_choices == 4);
    CHECK(spec.form_rounds == 1200);
    CHECK(spec.pool == spell_id_of("wild_shape"));
    CHECK(spec.pool_cost == 1);
    REQUIRE(a.resources.size() == 1);
    CHECK(a.resources[0].max == 2);
    CHECK(a.resources[0].recharge == uint8_t(rules5e::FeatureRecharge::ShortRest));
    CHECK(a.humanoid_only_features == std::vector<uint32_t>{focus});
    // max CR by level: the last row at or below the druid level (2: 1/4, 4: 1/2, 8: 1)
    CHECK(a.form_features.at(ws).max_cr_x8 == 4);
    CHECK(lib_for(2)->find(id)->form_features.at(ws).max_cr_x8 == 2);
    CHECK(lib_for(7)->find(id)->form_features.at(ws).max_cr_x8 == 4);
    CHECK(lib_for(8)->find(id)->form_features.at(ws).max_cr_x8 == 8);
    CHECK(a.form_features.at(ws).no_fly_below == 8);
    CHECK(a.form_features.at(ws).no_swim_below == 4);

    Fight f(*lib, {{id, {2, 2}}, {"srd51.monster.goblin", {20, 20}}});
    f.advance_to(f.id(0));
    const auto pool = [&] { return rules5e::find_pool(f.c->state(), f.id(0), spell_id_of("wild_shape"))->cur; };
    REQUIRE(rules5e::find_pool(f.c->state(), f.id(0), spell_id_of("wild_shape")));
    CHECK(pool() == 2);
    CHECK_FALSE(f.c->build_form(f.who(0), FormKind::WildShape, ws, 3));   // dire wolf: CR 1 > 1/2 at druid 4
    auto use = [&](uint8_t form) {
        Command u;
        u.request = {f.id(0), ActionKind::UseFeature};
        u.request.feature_id = ws;
        u.request.form_archetype = form;
        return f.settle(f.c->submit(u));
    };
    auto revert = [&] {
        Command rv;
        rv.request = {f.id(0), ActionKind::Revert};
        rv.request.as_bonus_action = true;
        REQUIRE(f.settle(f.c->submit(rv)).ok);
    };
    CHECK_FALSE(use(4).ok);   // the dire wolf is refused, and nothing is spent
    CHECK(pool() == 2);
    REQUIRE(use(3).ok);       // black bear, CR 1/2
    CHECK(f.who(0).form == FormKind::WildShape);
    CHECK(pool() == 1);
    revert();
    f.end_turn();
    f.advance_to(f.id(0));
    REQUIRE(use(2).ok);       // wolf
    CHECK(pool() == 0);
    revert();
    f.end_turn();
    f.advance_to(f.id(0));
    CHECK_FALSE(use(1).ok);   // the pool is empty: no third use
    CHECK(f.who(0).form == FormKind::None);
    f.check_replay();
    // a short rest refills the pool (recharge: short_rest)
    rules5e::EncounterState roster = f.c->state();
    rng::Rng rest_rng(3, 0);
    REQUIRE(sim::short_rest(roster, f.id(0), rest_rng, 0).ok);
    CHECK(rules5e::find_pool(roster, f.id(0), spell_id_of("wild_shape"))->cur == 2);
}

TEST_CASE("shapechanger content: common@2 keys only (speed object, creature_type, actions), the fact is required") {
    ContentLibrary lib;
    loaded(lib, druid(2));
    Json w = load_json("content/packs/srd51.core/monsters/goblin.json");
    w["$schema"] = "monster@4";
    w["id"] = "test.monster.were2";
    w["shapechanger"] = true;
    REQUIRE(w["actions"].size() >= 2);
    const std::string keep = w["actions"][0]["id"].get<std::string>();
    Json form = {{"id", "beast"}, {"name", "Beast"}, {"speed", {{"walk", 50}, {"climb", 20}}}, {"creature_type", "beast"},
                 {"actions", Json::array({keep})}};
    w["traits"] = Json::array({{{"id", "shape"}, {"name", "Shapechanger"}, {"description", "x"},
                                {"effect", {{"kind", "shapechanger"}, {"forms", Json::array({form})}}}}});
    auto err = [&](Json d) {
        ContentLibrary l;
        return l.add(d, "monster").value_or("");
    };
    {
        Json d = w;
        d["traits"][0]["effect"]["forms"][0]["speed_walk"] = 40;
        CHECK(err(d).find("shapechanger form: unknown key 'speed_walk'") != std::string::npos);
        d = w;
        d["traits"][0]["effect"]["uses"] = 1;
        CHECK(err(d).find("shapechanger effect: unknown key 'uses'") != std::string::npos);
        d = w;
        d.erase("shapechanger");
        CHECK(err(d).find("requires shapechanger: true") != std::string::npos);
        d = w;
        d["traits"][0]["effect"]["forms"][0].erase("name");
        CHECK(err(d).find("needs a name") != std::string::npos);
        d = w;
        d["traits"][0]["effect"]["forms"][0]["actions"] = Json::array({"howl"});
        CHECK(err(d).find("names unknown action 'howl'") != std::string::npos);
        d = w;
        d["traits"][0]["effect"]["forms"][0]["creature_type"] = "rock";
        CHECK(err(d).find("bad creature_type") != std::string::npos);
    }
    REQUIRE_FALSE(lib.add(w, "monster"));
    const Archetype& a = *lib.find("test.monster.were2");
    const uint32_t fid = uint32_t(a.features.size());
    const auto& sh = a.form_features.at(fid).shapes.at(0);
    CHECK(sh.speed_ft == 50);
    CHECK(sh.creature_type == rules5e::CreatureType::Beast);
    Fight f(lib, {{"test.monster.were2", {2, 2}}, {kDruid, {20, 20}}});
    f.advance_to(f.id(0));
    const auto fs = f.c->build_form(f.who(0), FormKind::Shapechanger, fid, 0);
    REQUIRE(fs);
    CHECK(fs->speed_ft == 50);
    CHECK(fs->creature_type == rules5e::CreatureType::Beast);
    for (const auto& at : fs->attacks) CHECK(at.id != std::string_view(w["actions"][1]["id"].get<std::string>()));
    bool has_keep = false;
    for (const auto& at : fs->attacks) has_keep = has_keep || at.id == keep;
    CHECK(has_keep);
}

TEST_CASE("version gate: wild_shape / shapechanger in pre-@4 files are inert (listed, never applied), like extra_attack") {
    // character@3 druid: the feature is listed inert, no FeatureSpec, no form features; the same file at @4 applies it
    for (const char* v : {"character@3", "character@2", "character@1"}) {
        ContentLibrary lib;
        Json d = druid(4);
        d["$schema"] = v;
        const auto e = build(lib, d);
        INFO(v << ": " << e.value_or(""));
        REQUIRE_FALSE(e);
        const Archetype& a = *lib.find(kDruid);
        CHECK(a.feature_specs.count(kWildShape) == 0);
        CHECK(a.form_features.count(kWildShape) == 0);
        bool listed = false;
        for (const auto& s : a.inert_features) listed = listed || s.find("ws") == 0;
        CHECK(listed);
    }
    {   // character@3 with keys @4 would refuse still loads (inert: never parsed)
        ContentLibrary lib;
        Json d = druid(4, {{"uses", 2}});
        d["$schema"] = "character@3";
        CHECK_FALSE(build(lib, d));
    }
    // monster@3 / @2 shapechanger: inert, no form swap feature; the creature fact is still read at @4 only as before
    for (const char* v : {"monster@3", "monster@2", "monster@1"}) {
        ContentLibrary lib;
        Json w = load_json("content/packs/srd51.core/monsters/goblin.json");
        w["$schema"] = v;
        w["id"] = "test.monster.oldwere";
        w["traits"] = Json::array({{{"id", "shape"}, {"name", "Shapechanger"}, {"description", "x"},
                                    {"effect", {{"kind", "shapechanger"}, {"forms", Json::array({{{"id", "wolf"}, {"name", "Wolf"}}})}}}}});
        const auto e = lib.add(w, "monster");
        INFO(v << ": " << e.value_or(""));
        REQUIRE_FALSE(e);   // no "requires shapechanger: true" error either: the trait is not parsed
        const Archetype& a = *lib.find("test.monster.oldwere");
        CHECK(a.feature_specs.empty());
        CHECK(a.form_features.empty());
        bool listed = false;
        for (const auto& s : a.inert_features) listed = listed || s.find("shape") == 0;
        CHECK(listed);
    }
}
