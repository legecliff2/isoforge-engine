// Priced spell material components (CONTENT_CONVENTIONS §7): spell@1 components.m.item + character@4 equipment value_gp.
// The real Revivify (300 gp diamond, consumed) casts from a combatant's combat-local equipment; refusals change no state;
// Combat::hash folds the consumed materials under 0xAD only once something was consumed; replay reproduces both hashes.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "rules5e_content/spell_loader.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::spell_id_of;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

const char* kRevivify = "test.kinds.spell.revivify";   // the real Revivify fixture (300 gp diamond, consumed)
const char* kDiamond = "srd51.item.diamond";

Json revivify() { return load_json("tests/validate/fixtures/spell_kinds/spells/revivify.json"); }

// A cleric-ish caster (fighter_1 + spellcasting) carrying `equipment`.
Json caster(const std::string& id, Json equipment) {
    Json w = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    w["id"] = id;
    w["abilities"]["wis"] = 16;
    w["hit_points"]["average"] = 40;
    w["spellcasting"] = {{"ability", "wis"}, {"caster_level", 5}, {"slots", {{"1", 4}, {"2", 3}, {"3", 3}}},
                         {"spells", {kRevivify, "test.mat.spell.revivify_noitem", "srd51.spell.magic_missile"}}};
    w["equipment"] = std::move(equipment);
    return w;
}

ContentLibrary make_lib() {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add_spell(revivify()));
    Json noitem = revivify();
    noitem["id"] = "test.mat.spell.revivify_noitem";
    noitem["components"]["m"].erase("item");
    REQUIRE_FALSE(lib.add_spell(noitem));
    REQUIRE_FALSE(lib.add_spell(load_json("content/packs/srd51.core/spells/magic_missile.json")));
    const Json d300 = {{"item", kDiamond}, {"value_gp", 300}};
    REQUIRE_FALSE(lib.add(caster("test.character.rich", Json::array({d300, d300})), "character"));
    REQUIRE_FALSE(lib.add(caster("test.character.cheap", Json::array({{{"item", kDiamond}, {"value_gp", 299}}, kDiamond})), "character"));
    // First-match order: a too-cheap diamond, another item, then two good diamonds of different values.
    REQUIRE_FALSE(lib.add(caster("test.character.mixed", Json::array({{{"item", kDiamond}, {"value_gp", 100}},
                                                                     {{"id", "srd51.item.ruby"}, {"value_gp", 900}},
                                                                     {{"item", "x.srd51.item.diamond"}, {"value_gp", 900}},
                                                                     {{"item", kDiamond}, {"value_gp", 500}},
                                                                     {{"id", kDiamond}, {"value_gp", 300}}})),
                          "character"));
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["id"] = "test.monster.gob1";
    g["hit_points"]["average"] = 1;
    REQUIRE_FALSE(lib.add(g, "monster"));
    g["id"] = "test.monster.gob60";
    g["hit_points"]["average"] = 60;
    REQUIRE_FALSE(lib.add(g, "monster"));
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

// Kill the adjacent 1 hp goblin with Magic Missile and come back on the caster's next turn.
void kill_and_wait(Fight& f, CombatantId wiz, CombatantId gob) {
    f.advance_to(wiz);
    REQUIRE(f.cast("srd51.spell.magic_missile", 1, gob).ok);
    REQUIRE(f.who(gob).dead);
    f.next_turn_of(wiz);
}
}  // namespace

TEST_CASE("priced materials loader: components.m.item and character@4 equipment value_gp") {
    auto r = rules5e_content::spell_from_json(revivify());
    REQUIRE(r.def);
    CHECK(r.def->material_item == kDiamond);
    CHECK(r.def->material_cost_cp == 30000);
    CHECK(r.def->material_consumed);
    Json noitem = revivify();
    noitem["components"]["m"].erase("item");
    auto n = rules5e_content::spell_from_json(noitem);
    REQUIRE(n.def);
    CHECK(n.def->material_item.empty());
    Json bad = revivify();
    bad["components"]["m"]["item"] = 5;
    CHECK_FALSE(rules5e_content::spell_from_json(bad).def);

    const auto lib = make_lib();
    const Archetype* m = lib.find("test.character.mixed");
    REQUIRE(m);
    REQUIRE(m->equipment.size() == 5);
    CHECK(m->equipment[0].id == kDiamond); CHECK(m->equipment[0].value_gp == 100);
    CHECK(m->equipment[1].id == "srd51.item.ruby"); CHECK(m->equipment[1].value_gp == 900);
    CHECK(m->equipment[4].id == kDiamond); CHECK(m->equipment[4].value_gp == 300);   // {"id"} form
    const Archetype* c = lib.find("test.character.cheap");
    REQUIRE(c);
    REQUIRE(c->equipment.size() == 2);
    CHECK(c->equipment[1].id == kDiamond); CHECK(c->equipment[1].value_gp == 0);     // bare string: 0 gp
    ContentLibrary l2;
    Json neg = caster("test.character.neg", Json::array({{{"item", kDiamond}, {"value_gp", -1}}}));
    CHECK(l2.add(neg, "character"));
    Json flt = caster("test.character.flt", Json::array({{{"item", kDiamond}, {"value_gp", Json::parse("1.5")}}}));
    CHECK(l2.add(flt, "character"));
}

TEST_CASE("priced materials: Revivify with a 300 gp diamond works and uses it up; a second Revivify needs the next one") {
    const auto lib = make_lib();
    Fight f(lib, {{"test.character.rich", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob1", 4, 5}, {"test.monster.gob60", 20, 20}});
    const CombatantId wiz = f.id(0), a = f.id(1), b = f.id(2);
    REQUIRE(f.c->equipment(wiz).size() == 2);
    f.advance_to(wiz);
    REQUIRE(f.cast("srd51.spell.magic_missile", 1, a).ok);
    f.next_turn_of(wiz);
    REQUIRE(f.cast("srd51.spell.magic_missile", 1, b).ok);
    REQUIRE(f.who(a).dead); REQUIRE(f.who(b).dead);
    f.next_turn_of(wiz);
    CHECK(f.c->materials_consumed().empty());
    auto ok = f.cast(kRevivify, 3, a);
    INFO(ok.error);
    REQUIRE(ok.ok);
    CHECK_FALSE(f.who(a).dead);
    CHECK(f.c->equipment(wiz).size() == 1);
    REQUIRE(f.c->materials_consumed().size() == 1);
    CHECK(f.c->materials_consumed()[0].caster == wiz);
    CHECK(f.c->materials_consumed()[0].spell_id == spell_id_of(kRevivify));
    CHECK(f.c->materials_consumed()[0].entry == 0);
    f.check_replay();
    f.next_turn_of(wiz);
    REQUIRE(f.cast(kRevivify, 3, b).ok);   // the second diamond
    CHECK(f.c->equipment(wiz).empty());
    CHECK(f.c->materials_consumed().size() == 2);
    f.check_replay();
}

TEST_CASE("priced materials: the first matching equipment entry (array order, exact id, enough value) is the one consumed") {
    const auto lib = make_lib();
    Fight f(lib, {{"test.character.mixed", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob1", 4, 5}, {"test.monster.gob60", 20, 20}});
    const CombatantId wiz = f.id(0), a = f.id(1), b = f.id(2);
    f.advance_to(wiz);
    REQUIRE(f.cast("srd51.spell.magic_missile", 1, a).ok);
    f.next_turn_of(wiz);
    REQUIRE(f.cast("srd51.spell.magic_missile", 1, b).ok);
    f.next_turn_of(wiz);
    // First match in array order: entry 0 is too cheap, 1 is another id, 2 only ends with the id; entry 3 (500 gp) is used,
    // not the exact-value 300 gp entry 4.
    REQUIRE(f.cast(kRevivify, 3, a).ok);
    REQUIRE(f.c->materials_consumed().size() == 1);
    CHECK(f.c->materials_consumed()[0].entry == 3);
    CHECK(f.c->materials_consumed()[0].value_gp == 500);
    REQUIRE(f.c->equipment(wiz).size() == 4);
    CHECK(f.c->equipment(wiz)[3].value_gp == 300);
    f.next_turn_of(wiz);
    REQUIRE(f.cast(kRevivify, 3, b).ok);   // the 300 gp entry (now index 3)
    CHECK(f.c->materials_consumed()[1].entry == 3);
    CHECK(f.c->equipment(wiz).size() == 3);
    f.check_replay();
}

TEST_CASE("priced materials: the diamond is gone after Revivify, so a second Revivify is refused") {
    const auto lib = make_lib();
    // `cheap` has nothing good enough; give a one-diamond caster through `rich` minus one by casting twice is covered above,
    // so here: rich casts twice successfully, then a third time is refused.
    Fight f(lib, {{"test.character.rich", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob1", 4, 5}, {"test.monster.gob1", 5, 6},
                  {"test.monster.gob60", 20, 20}});
    const CombatantId wiz = f.id(0);
    const CombatantId g[3] = {f.id(1), f.id(2), f.id(3)};
    f.advance_to(wiz);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(f.cast("srd51.spell.magic_missile", 1, g[i]).ok);
        f.next_turn_of(wiz);
    }
    REQUIRE(f.cast(kRevivify, 3, g[0]).ok);
    f.next_turn_of(wiz);
    REQUIRE(f.cast(kRevivify, 3, g[1]).ok);
    f.next_turn_of(wiz);
    REQUIRE(f.c->equipment(wiz).empty());
    const uint64_t h = f.c->hash(), lh = f.c->log_hash();
    const auto refused = f.cast(kRevivify, 3, g[2]);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error == "components unavailable");
    CHECK(f.who(g[2]).dead);
    CHECK(f.c->hash() == h);            // a refusal changes no state
    CHECK(f.c->log_hash() == lh);
    f.check_replay();
}

TEST_CASE("priced materials: a too-cheap diamond and a priced material without an item are refused") {
    const auto lib = make_lib();
    Fight f(lib, {{"test.character.cheap", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob60", 20, 20}});
    const CombatantId wiz = f.id(0), gob = f.id(1);
    kill_and_wait(f, wiz, gob);
    const uint64_t h = f.c->hash();
    const auto cheap = f.cast(kRevivify, 3, gob);   // 299 gp, and a bare-string diamond (0 gp)
    CHECK_FALSE(cheap.ok);
    CHECK(cheap.error == "components unavailable");
    CHECK(f.c->equipment(wiz).size() == 2);
    CHECK(f.c->hash() == h);
    f.check_replay();

    Fight r(lib, {{"test.character.rich", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob60", 20, 20}});
    kill_and_wait(r, r.id(0), r.id(1));
    const auto noitem = r.cast("test.mat.spell.revivify_noitem", 3, r.id(1));   // rule 3: no item key, even with diamonds
    CHECK_FALSE(noitem.ok);
    CHECK(noitem.error == "components unavailable");
    CHECK(r.c->equipment(r.id(0)).size() == 2);
    CHECK(r.c->materials_consumed().empty());
}

TEST_CASE("priced materials: 0xAD is folded only after a consumption, and replay reproduces both hashes") {
    const auto lib = make_lib();
    Fight f(lib, {{"test.character.rich", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob60", 20, 20}});
    Fight same(lib, {{"test.character.cheap", 5, 5}, {"test.monster.gob1", 6, 5}, {"test.monster.gob60", 20, 20}});
    // Equipment alone (and its value_gp) is not hashed: different equipment, same everything else, same hash.
    CHECK(f.c->hash() == same.c->hash());
    kill_and_wait(f, f.id(0), f.id(1));
    kill_and_wait(same, same.id(0), same.id(1));
    CHECK(f.c->hash() == same.c->hash());
    CHECK(f.c->materials_consumed().empty());
    REQUIRE(f.cast(kRevivify, 3, f.id(1)).ok);
    REQUIRE_FALSE(f.c->materials_consumed().empty());
    f.check_replay();
    ecs::Registry r2;
    std::string err;
    auto again = replay(r2, f.chunk, lib, f.seed, f.c->log(), &err);
    INFO(err);
    REQUIRE(again);
    CHECK(again->equipment(f.id(0)).size() == 1);
    REQUIRE(again->materials_consumed().size() == 1);
    CHECK(again->materials_consumed()[0].entry == f.c->materials_consumed()[0].entry);
}
