// Font of Magic sim wiring (rules #147, VERSION_BUMP_BATCH 7.2o hand-off): font_of_magic {pool} feature kind, the sorcery
// pool resolved to sorcery_points_max(level), sim::flexible_casting out of combat, log_hash 0xBA, replay.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e/font_of_magic.hpp"
#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/font_of_magic.hpp"
#include "sim/rest.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::FlexibleResult;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kSorc = "test.character.fom";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json spell(const std::string& name, int level, Json effects, Json extra = Json::object()) {
    Json j = {{"$schema", "spell@1"}, {"id", "test.spell." + name}, {"name", name}, {"level", level}, {"school", "evocation"},
              {"casting_time", {{"kind", "action"}}}, {"range", {{"kind", "ranged"}, {"feet", 60}}},
              {"components", {{"v", true}, {"s", true}}}, {"duration", {{"kind", "instantaneous"}}}, {"effects", std::move(effects)}};
    for (auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    return j;
}
Json bolt_doc() {
    return spell("bolt", 1, Json::array({{{"kind", "spell_attack"}, {"attack", "ranged"}, {"damage", {{"dice", "1d10"}, {"type", "fire"}}}}}));
}
Json burst_doc() {   // an aimed sphere for Distant
    return spell("burst", 1, Json::array({{{"kind", "save"}, {"ability", "dex"}, {"on_success", "half"}, {"damage", {{"dice", "2d6"}, {"type", "fire"}}}}}),
                 {{"area", {{"shape", "sphere"}, {"size_ft", 5}}}});
}
Json feature(const std::string& id, Json effect) { return {{"id", id}, {"name", id}, {"description", "x"}, {"effect", std::move(effect)}}; }
Json sorcerer(Json features, int level = 5, Json resources = Json::array()) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = kSorc;
    c["classes"] = Json::array({{{"class", "sorcerer"}, {"level", level}, {"hit_die", "d6"}}});
    c["abilities"]["cha"] = 16;
    c["hit_points"]["average"] = 200;
    c["resources"] = std::move(resources);
    c["features"] = std::move(features);
    c["spellcasting"] = {{"ability", "cha"}, {"caster_level", level}, {"slots", {{"1", 4}, {"2", 3}, {"3", 2}}},
                         {"spells", {"test.spell.bolt", "test.spell.burst"}}};
    return c;
}
Json fom(const std::string& pool = "sorcery_points") { return feature("fom", {{"kind", "font_of_magic"}, {"pool", pool}}); }
std::optional<std::string> build_raw(ContentLibrary& lib, Json sorc);
std::optional<std::string> build(ContentLibrary& lib, Json sorc) {
    auto e = build_raw(lib, std::move(sorc));
    if (e) MESSAGE(*e);
    return e;
}
std::optional<std::string> build_raw(ContentLibrary& lib, Json sorc) {
    for (const Json& d : {bolt_doc(), burst_doc()}) if (auto e = lib.add_spell(d)) return e;
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = "test.monster.gob";
    g["hit_points"]["average"] = 300;
    if (auto e = lib.add(g, "monster")) return e;
    return lib.add(sorc, "character");
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
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns) : lib(&l) {
        chunk = arena(spawns);
        c = Combat::create(reg, chunk, l, 1);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    SubmitResult settle(SubmitResult r) {
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
    Command cast_cmd(const char* sp, CombatantId target, uint8_t meta, std::vector<CombatantId> targets = {}) const {
        Command cmd;
        cmd.request = {rules5e::current_actor(c->state()), ActionKind::CastSpell, target};
        cmd.request.spell_id = spell_id_of(sp);
        cmd.request.spell_level = 1;
        cmd.request.metamagic = meta;
        cmd.request.targets = std::move(targets);
        return cmd;
    }
    int points(CombatantId who) const {
        const auto* p = rules5e::find_pool(c->state(), who, spell_id_of("sorcery_points"));
        return p ? int(p->cur) : -1;
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, 1, c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
    }
    // a refused request changes nothing
    void refused(const Command& cmd) {
        const uint64_t h = c->hash(), lh = c->log_hash();
        const std::size_t n = c->log().size();
        CHECK_FALSE(settle(c->submit(cmd)).ok);
        CHECK(c->hash() == h);
        CHECK(c->log_hash() == lh);
        CHECK(c->log().size() == n);
    }
};
Command flex(CombatantId who, uint8_t dir, uint8_t level) {
    Command cmd;
    cmd.request = {who, ActionKind::FlexibleCasting};
    cmd.request.as_bonus_action = true;
    cmd.request.flexible = dir;
    cmd.request.flexible_level = level;
    return cmd;
}
const Archetype* loaded(ContentLibrary& lib, const Json& doc) {
    REQUIRE_FALSE(build(lib, doc));
    const Archetype* a = lib.find(kSorc);
    REQUIRE(a);
    return a;
}
const Archetype::Resource* pool_of(const Archetype& a) {
    for (const auto& r : a.resources) if (r.id == "sorcery_points") return &r;
    return nullptr;
}
}  // namespace

TEST_CASE("Font of Magic content: the sorcery pool max follows the sorcerer level, long-rest recharge, no Metamagic needed") {
    for (int lvl : {2, 3, 5, 11, 20}) {
        CAPTURE(lvl);
        ContentLibrary lib;
        const Archetype& a = *loaded(lib, sorcerer(Json::array({fom()}), lvl));
        const auto* r = pool_of(a);
        REQUIRE(r);
        CHECK(int(r->max) == rules5e::sorcery_points_max(lvl));
        CHECK(int(r->max) == lvl);
        CHECK(r->start == r->max);
        CHECK(r->recharge == uint8_t(rules5e::FeatureRecharge::LongRest));
        CHECK(a.base.sorcery_pool == spell_id_of("sorcery_points"));
        CHECK(a.base.metamagic_known == 0);
    }
    {   // a declared resources[] pool is resolved to the level (max 9 in content -> 5), recharge forced to long rest
        ContentLibrary lib;
        const Archetype& a = *loaded(lib, sorcerer(Json::array({fom()}), 5,
                                                  Json::array({{{"id", "sorcery_points"}, {"max", 9}, {"recharge", "short_rest"}}})));
        REQUIRE(a.resources.size() == 1);
        CHECK(a.resources[0].max == 5);
        CHECK(a.resources[0].recharge == uint8_t(rules5e::FeatureRecharge::LongRest));
    }
    {   // with Metamagic on the same pool, either order
        const Json res = Json::array({{{"id", "sorcery_points"}, {"max", 5}, {"recharge", "long_rest"}}});
        const Json mm = feature("mm", {{"kind", "metamagic"}, {"options", {"quickened"}}, {"pool", "sorcery_points"}});
        for (const Json& feats : {Json::array({fom(), mm}), Json::array({mm, fom()})}) {
            ContentLibrary lib;
            const Archetype& a = *loaded(lib, sorcerer(feats, 6, res));
            CHECK(pool_of(a)->max == 6);
            CHECK(a.base.metamagic_known == rules5e::kMetaQuickened);
        }
    }
    // load errors: no pool, level 1, no sorcerer class, a metamagic pool that differs
    {
        ContentLibrary lib;
        CHECK(build_raw(lib, sorcerer(Json::array({feature("fom", {{"kind", "font_of_magic"}})}))));
    }
    {
        ContentLibrary lib;
        CHECK(build_raw(lib, sorcerer(Json::array({fom()}), 1)));
    }
    {
        ContentLibrary lib;
        Json doc = sorcerer(Json::array({fom()}));
        doc["classes"][0]["class"] = "wizard";
        CHECK(build_raw(lib, doc));
    }
    {
        ContentLibrary lib;
        const Json res = Json::array({{{"id", "other"}, {"max", 5}, {"recharge", "long_rest"}}});
        CHECK(build_raw(lib, sorcerer(Json::array({feature("mm", {{"kind", "metamagic"}, {"options", {"quickened"}}, {"pool", "other"}}), fom()}), 5, res)));
    }
}

TEST_CASE("Font of Magic out of combat: conversions both ways on a roster; refusals change nothing; long rest") {
    ContentLibrary lib;
    const Archetype& a = *loaded(lib, sorcerer(Json::array({fom()}), 5));
    rules5e::EncounterState roster;
    rules5e::Combatant k = a.base;
    k.id = 1;
    roster.combatants.push_back(k);
    rules5e::register_pool(roster, 1, pool_of(a)->pool_id, pool_of(a)->max, pool_of(a)->start, rules5e::FeatureRecharge::LongRest);
    auto pts = [&] { return int(rules5e::find_pool(roster, 1, spell_id_of("sorcery_points"))->cur); };
    auto& me = roster.combatants[0];
    CHECK(pts() == 5);
    // points -> slot: a 2nd-level slot costs 3
    auto o = flexible_casting(roster, 1, 2, 2);
    CHECK(o.result == FlexibleResult::Ok);
    CHECK(pts() == 2);
    CHECK(me.spell_slots[2] == 4);   // may exceed the maximum
    // slot -> points: a 1st-level slot for 1 point
    o = flexible_casting(roster, 1, 1, 1);
    CHECK(o.result == FlexibleResult::Ok);
    CHECK(pts() == 3);
    CHECK(me.spell_slots[1] == 3);
    // refusals: each leaves slots and points as they were
    struct R { uint8_t dir, level; FlexibleResult want; rules5e::CombatantId id; };
    const R refusals[] = {{2, 3, FlexibleResult::NotEnoughPoints, 1}, {2, 6, FlexibleResult::BadLevel, 1}, {1, 0, FlexibleResult::BadLevel, 1},
                          {3, 1, FlexibleResult::BadDirection, 1}, {1, 4, FlexibleResult::NoSlot, 1}, {1, 3, FlexibleResult::OverMax, 1},
                          {1, 1, FlexibleResult::NoPool, 9}};
    for (const R& r : refusals) {
        CAPTURE(int(r.dir));
        CAPTURE(int(r.level));
        const auto slots = me.spell_slots;
        const int before = pts();
        CHECK(flexible_casting(roster, r.id, r.dir, r.level).result == r.want);
        CHECK(me.spell_slots == slots);
        CHECK(pts() == before);
    }
    REQUIRE(sim::long_rest(roster, 1).ok);
    CHECK(pts() == 5);
    CHECK(me.spell_slots == me.spell_slots_max);   // the created slot is gone
}

TEST_CASE("Font of Magic in combat: FlexibleCasting bonus action, refusals change nothing, replay with flexible set") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, sorcerer(Json::array({fom()}), 5)));
    Fight f(lib, {{kSorc, {5, 5}}, {"test.monster.gob", {9, 5}}});
    const CombatantId me = f.id(0);
    f.advance_to(me);
    CHECK(f.points(me) == 5);
    f.refused(flex(me, 1, 1));   // over the maximum
    f.refused(flex(me, 2, 6));   // bad level
    f.refused(flex(me, 2, 5));   // costs 7
    const uint64_t lh0 = f.c->log_hash();
    REQUIRE(f.settle(f.c->submit(flex(me, 2, 1))).ok);
    CHECK(f.points(me) == 3);
    CHECK(rules5e::find(f.c->state(), me)->spell_slots[1] == 5);
    CHECK(f.c->log_hash() != lh0);
    f.refused(flex(me, 1, 1));   // the bonus action is spent
    f.end_turn();
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(flex(me, 1, 2))).ok);
    CHECK(f.points(me) == 5);
    CHECK(rules5e::find(f.c->state(), me)->spell_slots[2] == 2);
    f.check_replay();
    REQUIRE(f.c->log().size() >= 2);
}
