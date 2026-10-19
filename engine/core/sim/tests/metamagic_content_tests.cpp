// R-G14 sim wiring (rules #109, VERSION_BUMP_BATCH 7.2i hand-off a / b): the open feature kinds metamagic {options, pool} and
// sculpt_spells {} compile into rules5e Combatant facts; spell@1 school evocation sets SpellDef::evocation; ActionRequest::
// metamagic flows from sim::Combat to rules5e (sorcery points from the resources[] pool), Distant widens the aim range, and
// casts with options replay (log_hash 0xB1).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kSorc = "test.character.sorcerer";

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
Json sorcerer(Json features, int points = 5) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = kSorc;
    c["abilities"]["cha"] = 16;
    c["hit_points"]["average"] = 200;
    c["resources"] = Json::array({{{"id", "sorcery_points"}, {"max", points}, {"recharge", "long_rest"}}});
    c["features"] = std::move(features);
    c["spellcasting"] = {{"ability", "cha"}, {"caster_level", 5}, {"slots", {{"1", 4}, {"2", 3}}},
                         {"spells", {"test.spell.bolt", "test.spell.burst"}}};
    return c;
}
Json default_features() {
    return Json::array({feature("metamagic", {{"kind", "metamagic"}, {"options", {"quickened", "twinned", "distant"}}, {"pool", "sorcery_points"}}),
                        feature("sculpt", {{"kind", "sculpt_spells"}})});
}
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
}  // namespace

TEST_CASE("R-G14 content: metamagic / sculpt_spells features and the evocation school compile") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, sorcerer(default_features())));
    Fight f(lib, {{kSorc, {5, 5}}, {"test.monster.gob", {9, 5}}});
    const auto& s = *rules5e::find(f.c->state(), f.id(0));
    CHECK(s.metamagic_known == (rules5e::kMetaQuickened | rules5e::kMetaTwinned | rules5e::kMetaDistant));
    CHECK(s.sorcery_pool == spell_id_of("sorcery_points"));
    CHECK(s.sculpt_spells);
    CHECK(f.points(f.id(0)) == 5);
    auto r = rules5e_content::spell_from_json(bolt_doc());
    REQUIRE(r.def);
    CHECK(r.def->evocation);
    Json abj = bolt_doc(); abj["school"] = "abjuration";
    CHECK_FALSE(rules5e_content::spell_from_json(abj).def->evocation);

    // bad declarations are load errors
    for (const Json& bad : {Json{{"kind", "metamagic"}, {"options", {"quickened"}}, {"pool", "nope"}},
                            Json{{"kind", "metamagic"}, {"options", {"loud"}}, {"pool", "sorcery_points"}},
                            Json{{"kind", "metamagic"}, {"options", Json::array()}, {"pool", "sorcery_points"}}}) {
        ContentLibrary l2;
        CHECK(build(l2, sorcerer(Json::array({feature("mm", bad)}))));
    }
}

TEST_CASE("R-G14 sim: Quickened and Twinned spend sorcery points; refusals change nothing; replay") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, sorcerer(default_features())));
    Fight f(lib, {{kSorc, {5, 5}}, {"test.monster.gob", {9, 5}}, {"test.monster.gob", {9, 7}}});
    const CombatantId me = f.id(0), g1 = f.id(1), g2 = f.id(2);
    f.advance_to(me);
    f.refused(f.cast_cmd("test.spell.bolt", g1, rules5e::kMetaCareful));                 // not known
    f.refused(f.cast_cmd("test.spell.bolt", g1, rules5e::kMetaTwinned, {g1}));           // Twinned needs two targets
    f.refused(f.cast_cmd("test.spell.bolt", g1, rules5e::kMetaTwinned | rules5e::kMetaQuickened, {g1, g2}));   // two options
    CHECK(f.points(me) == 5);
    REQUIRE(f.settle(f.c->submit(f.cast_cmd("test.spell.bolt", g1, rules5e::kMetaQuickened))).ok);
    CHECK(f.points(me) == 3);
    CHECK_FALSE(f.c->state().budget.bonus_action);
    CHECK(f.c->state().budget.action);   // Quickened: a bonus action, the action is left
    REQUIRE(f.settle(f.c->submit(f.cast_cmd("test.spell.bolt", g1, rules5e::kMetaTwinned, {g1, g2}))).ok);
    CHECK(f.points(me) == 2);   // Twinned costs the slot level
    f.check_replay();
    // log_hash folds the option (0xB1): the same log without it hashes differently
    auto log = f.c->log();
    for (auto& e : log) e.step.request.metamagic = 0;
    ecs::Registry r2;
    auto plain = replay(r2, f.chunk, lib, 1, log);
    if (plain) CHECK(plain->log_hash() != f.c->log_hash());
}

TEST_CASE("R-G14 sim: not enough sorcery points is refused") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, sorcerer(default_features(), 1)));
    Fight f(lib, {{kSorc, {5, 5}}, {"test.monster.gob", {9, 5}}});
    f.advance_to(f.id(0));
    f.refused(f.cast_cmd("test.spell.bolt", f.id(1), rules5e::kMetaQuickened));   // costs 2, has 1
    CHECK(f.points(f.id(0)) == 1);
}

TEST_CASE("R-G14 sim: Distant Spell doubles the range for a direct target and an aimed area; replay") {
    ContentLibrary lib;
    REQUIRE_FALSE(build(lib, sorcerer(default_features())));
    Fight f(lib, {{kSorc, {2, 5}}, {"test.monster.gob", {22, 5}}});   // 100 ft: beyond 60, within 120
    const CombatantId me = f.id(0), g = f.id(1);
    f.advance_to(me);
    f.refused(f.cast_cmd("test.spell.bolt", g, 0));
    Command aim = f.cast_cmd("test.spell.burst", 0, 0);
    aim.to = Vec2i{22, 5};
    aim.aim = true;
    f.refused(aim);
    aim.request.metamagic = rules5e::kMetaDistant;
    REQUIRE(f.settle(f.c->submit(aim)).ok);
    CHECK(f.points(me) == 4);
    f.end_turn();
    f.advance_to(me);
    REQUIRE(f.settle(f.c->submit(f.cast_cmd("test.spell.bolt", g, rules5e::kMetaDistant))).ok);
    CHECK(f.points(me) == 3);
    f.check_replay();
}
