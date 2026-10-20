// R-G12 summons, sim side (VERSION_BUMP_BATCH 7.2r, SUMMONS_TRANSFORMS.md §3): the summon counter seed, build_summon /
// summon_choice_ok (D1 CR bands), placement on summon_tiles, TokenSpawned / TokenRemoved (v10), teardown order (D15), 0xBC /
// 0xAF, the turn key that survives an insertion before the current actor, and replay.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>

#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "sim/events.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::spell_id_of;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kWizard = "test.character.wizard";
const uint32_t kConjure = spell_id_of("test.conjure_animals");
const uint32_t kFamiliar = spell_id_of("test.find_familiar");
const uint32_t kAnimate = spell_id_of("test.animate_dead");

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json beast(const std::string& id, const char* cr, int hp = 11, const char* size = "medium") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["type"] = "beast";
    m["size"] = size;
    m["challenge"]["rating"] = cr;
    m["hit_points"]["average"] = hp;
    m["traits"] = Json::array();
    return m;
}
rules5e::SpellDef summon_def(const char* cid, uint8_t level, uint16_t range, rules5e::SummonSpec spec, bool conc) {
    rules5e::SpellDef d;
    d.content_id = cid;
    d.id = spell_id_of(cid);
    d.level = level;
    d.range_ft = range;
    d.concentration = conc;
    d.kind = rules5e::SpellEffectKind::Summon;
    d.summon = spec;
    return d;
}
ContentLibrary make_lib(int goblin_hp = 60, int wolf_hp = 11, std::optional<uint8_t> conjure_cap = std::nullopt) {
    ContentLibrary lib;
    for (const char* n : {"fire_bolt", "hold_person"})
        REQUIRE_FALSE(lib.add_spell(load_json(std::string("content/packs/srd51.core/spells/") + n + ".json")));
    REQUIRE_FALSE(lib.add(beast("t.wolf", "1/4", wolf_hp), "monster"));
    REQUIRE_FALSE(lib.add(beast("t.bear", "1", 30), "monster"));
    REQUIRE_FALSE(lib.add(beast("t.dragon", "3", 40), "monster"));
    REQUIRE_FALSE(lib.add(beast("t.elk", "2", 20, "large"), "monster"));
    using rules5e::SummonControl;
    using rules5e::SummonInitiative;
    REQUIRE_FALSE(lib.add_summon_spell(summon_def("test.conjure_animals", 3, 60, {8, SummonInitiative::Group, SummonControl::Verbal, true}, true),
                                       {{"t.wolf", "t.bear", "t.dragon", "t.elk"}, SummonOptions::Table::CrBand, conjure_cap}));
    REQUIRE_FALSE(lib.add_summon_spell(summon_def("test.find_familiar", 1, 10, {1, SummonInitiative::Own, SummonControl::Telepathic, true}, false),
                                       {{"t.wolf"}, SummonOptions::Table::Any, uint8_t(1)}));
    REQUIRE_FALSE(lib.add_summon_spell(summon_def("test.animate_dead", 3, 10, {4, SummonInitiative::Group, SummonControl::BonusAction, false}, false),
                                       {{"t.wolf", "t.bear"}, SummonOptions::Table::Any, uint8_t(4)}));
    Json w = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    w["id"] = kWizard;
    w["abilities"]["int"] = 16;
    w["hit_points"]["average"] = 300;
    w["spellcasting"] = {{"ability", "int"}, {"caster_level", 9},
                         {"slots", {{"1", 4}, {"2", 3}, {"3", 3}, {"4", 3}, {"5", 2}}},
                         {"spells", Json::array({"srd51.spell.fire_bolt", "srd51.spell.hold_person", "test.conjure_animals",
                                                 "test.find_familiar", "test.animate_dead"})}};
    const auto we = lib.add(w, "character");
    REQUIRE_MESSAGE(!we, we.value_or(""));
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["hit_points"]["average"] = goblin_hp;
    REQUIRE_FALSE(lib.add(g, "monster"));
    return lib;
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns, uint32_t extra_id = 0) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    if (extra_id) c.spawns.push_back({"prop.not_a_creature", Vec2i{0, 0}, extra_id, ""});   // a non-combatant spawn id
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void end_turn(Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    const auto r = c.submit(e);
    REQUIRE_MESSAGE(r.ok, r.error);
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 40 && rules5e::current_actor(c.state()) != who; ++i) end_turn(c);
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
SubmitResult submit(Combat& c, const rules5e::ActionRequest& q) {
    Command x;
    x.request = q;
    auto r = c.submit(x);
    if (r.ok) r = c.settle(nullptr);
    return r;
}
rules5e::ActionRequest summon(CombatantId a, uint32_t spell, std::vector<uint8_t> ar, uint8_t lvl = 3) {
    rules5e::ActionRequest r{a, ActionKind::CastSpell, a};
    r.spell_id = spell;
    r.spell_level = lvl;
    r.summon_archetypes = std::move(ar);
    return r;
}
void same_on_replay(const Combat& c, const world::Chunk& ch, const ContentLibrary& lib, uint64_t seed) {
    ecs::Registry reg;
    std::string err;
    auto r = replay(reg, ch, lib, seed, c.log(), &err, c.surprised_at_start());
    REQUIRE_MESSAGE(r, err);
    CHECK(r->hash() == c.hash());
    CHECK(r->log_hash() == c.log_hash());
    CHECK(event_stream_hash(r->events()) == event_stream_hash(c.events()));
}
std::vector<CombatantId> summons_of(const Combat& c) {
    std::vector<CombatantId> out;
    for (const auto& k : c.state().combatants) if (k.summoner != 0) out.push_back(k.id);
    return out;
}
}  // namespace

TEST_CASE("summon counter: seeded above every spawn id (non-combatants too); a seeded counter alone folds nothing") {
    auto lib = make_lib();
    const auto ch = arena({{kWizard, Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{12, 5}}}, 0xFFFF0000u);
    REQUIRE(ch.spawns[0].stable_id < 0xFFFF0000u);
    REQUIRE(ch.spawns[1].stable_id < 0xFFFF0000u);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 1);
    REQUIRE(c);
    CHECK(c->state().next_summon_id == 0xFFFF0001u);
    CHECK_FALSE(rules5e::has_summon_state(c->state()));
    CHECK(event_stream_version(c->events()) < 10);
    // the CR from challenge.rating
    CHECK(lib.find("t.wolf")->cr_eighths == 2);
    CHECK(lib.find("t.bear")->cr_eighths == 8);
    CHECK(lib.find("t.dragon")->cr_eighths == 24);
    // add_summon_spell refusals
    CHECK(lib.add_summon_spell(summon_def("test.x", 1, 10, {}, false), {{"t.nope"}, SummonOptions::Table::Any, std::nullopt}).value_or("").find("unknown summon archetype") != std::string::npos);
    rules5e::SpellDef notsum;
    notsum.content_id = "test.y";
    notsum.id = spell_id_of("test.y");
    CHECK(lib.add_summon_spell(notsum, {{"t.wolf"}, SummonOptions::Table::Any, std::nullopt}).value_or("").find("not a Summon") != std::string::npos);
}

TEST_CASE("conjure: CR bands (D1), deterministic tiles, Summoned -> TokenSpawned, ids from the counter, 0xBC / 0xAF, replay") {
    auto lib = make_lib();
    const auto ch = arena({{kWizard, Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{14, 5}}});
    const CombatantId w = ch.spawns[0].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 2);
    REQUIRE(c);
    to_actor(*c, w);
    const std::size_t logged0 = c->log().size();
    // refusals (nothing spent): 3 creatures, CR 3 alone, 4 of CR 1, 9 wolves at level 3, ids given by the caller
    for (std::vector<uint8_t> bad : {std::vector<uint8_t>{0, 0, 0}, std::vector<uint8_t>{2}, std::vector<uint8_t>{1, 1, 1, 1}}) {
        const auto r = submit(*c, summon(w, kConjure, bad));
        CHECK_FALSE(r.ok);
        CHECK(r.error == "summon: choice not allowed");
    }
    {
        auto q = summon(w, kConjure, {1, 1});
        q.summon_ids = {999};
        CHECK(submit(*c, q).error == "summon_ids: assigned by rules (leave empty)");
        q = summon(w, kConjure, {1, 1});
        q.summon_tiles = {{5, 5}, {6, 5}};   // the wizard's own tile
        CHECK(submit(*c, q).error == "summon_tiles: illegal tile");
        q.summon_tiles = {{40, 40}, {6, 5}};   // out of the grid / range
        CHECK_FALSE(submit(*c, q).ok);
    }
    CHECK(c->log().size() == logged0);
    const uint32_t first = c->state().next_summon_id;
    const std::size_t n0 = c->events().size();
    auto r = submit(*c, summon(w, kConjure, {0, 0, 0, 0, 0, 0, 0, 0}));   // 8 wolves (CR 1/4)
    REQUIRE_MESSAGE(r.ok, r.error);
    const auto& lq = c->log().back().step.request;
    REQUIRE(lq.summon_ids.size() == 8);
    REQUIRE(lq.summon_tiles.size() == 8);
    for (std::size_t k = 0; k < 8; ++k) CHECK(lq.summon_ids[k] == first + k);
    // deterministic: nearest to the caster, row-major: the 8 neighbours of (5,5) in (y, x) order
    const Vec2i expect[8] = {{4, 4}, {5, 4}, {6, 4}, {4, 5}, {6, 5}, {4, 6}, {5, 6}, {6, 6}};
    for (std::size_t k = 0; k < 8; ++k) {
        CHECK(lq.summon_tiles[k].x == expect[k].x);
        CHECK(lq.summon_tiles[k].y == expect[k].y);
        CHECK(c->pos(lq.summon_ids[k]) == expect[k]);
        CHECK(c->summoned(lq.summon_ids[k]));
    }
    // each Summoned is followed at once by its TokenSpawned; all before the cast's SpellTarget
    int spawned = 0;
    std::size_t last_token = 0, spell_target = 0;
    for (std::size_t i = n0; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Summoned) {
            REQUIRE(i + 1 < c->events().size());
            const auto& t = c->events()[i + 1];
            CHECK(t.kind == SimEvent::Kind::TokenSpawned);
            CHECK(t.actor == e.rules.target);
            CHECK(t.rules.a == 1);
            CHECK(t.rules.b == int32_t(w));
            CHECK(t.tile == c->pos(e.rules.target));
            ++spawned;
            last_token = i + 1;
        }
        if (e.kind == SimEvent::Kind::SpellTarget) spell_target = i;
    }
    CHECK(spawned == 8);
    CHECK(spell_target > last_token);
    CHECK(event_stream_version(c->events()) == 10);
    CHECK(rules5e::has_summon_state(c->state()));
    same_on_replay(*c, ch, lib, 2);
    // caller tiles are folded (0xBC): the same cast on other legal tiles is another log
    ecs::Registry r2;
    auto d = Combat::create(r2, ch, lib, 2);
    to_actor(*d, w);
    auto q = summon(w, kConjure, {0, 0, 0, 0, 0, 0, 0, 0});
    for (int k = 0; k < 8; ++k) q.summon_tiles.push_back({5 + k, 8});
    r = submit(*d, q);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(d->log_hash() != c->log_hash());
    CHECK(d->hash() != c->hash());
    same_on_replay(*d, ch, lib, 2);
    // bands: 1 Large CR 2 elk (footprint 2, its TokenSpawned carries the side); 2 bears at level 3; 4 bears need level 5
    for (const auto& [ar, lvl, ok] : {std::tuple<std::vector<uint8_t>, int, bool>{{3}, 3, true}, {{1, 1}, 3, true}, {{1, 1, 1, 1}, 5, true},
                                      {{0, 0, 0, 0, 0, 0, 0, 0, 0}, 3, false}}) {
        ecs::Registry r3;
        auto e = Combat::create(r3, ch, lib, 2);
        to_actor(*e, w);
        const auto rr = submit(*e, summon(w, kConjure, ar, uint8_t(lvl)));
        CHECK(rr.ok == ok);
        if (rr.ok && ar.size() == 1)
            for (const auto& ev : e->events()) if (ev.kind == SimEvent::Kind::TokenSpawned) CHECK(ev.rules.a == 2);
    }
}

TEST_CASE("summons end: concentration (b 1) and 0 HP (b 3) -> TokenRemoved after the step's events; gone slots; replay") {
    auto lib = make_lib(60, 1);   // wolves with 1 HP
    const auto ch = arena({{kWizard, Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{8, 5}}});
    const CombatantId w = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    to_actor(*c, w);
    REQUIRE(submit(*c, summon(w, kConjure, {0, 0})).ok);
    const auto ids = summons_of(*c);
    REQUIRE(ids.size() == 2);
    end_turn(*c);
    // the goblin attacks the wolf next to it until one drops: SummonEnded b 3, then its TokenRemoved
    CombatantId victim = 0;
    for (int t = 0; t < 10 && !victim; ++t) {
        to_actor(*c, g);
        const CombatantId tgt = c->distance_tiles(g, ids[0]) <= c->distance_tiles(g, ids[1]) ? ids[0] : ids[1];
        // walk next to it, then swing
        for (int s = 0; s < 6 && c->distance_tiles(g, tgt) > 1; ++s) {
            const Vec2i gp = c->pos(g), tp = c->pos(tgt);
            Command st;
            st.kind = CommandKind::Step;
            st.to = {gp.x + (tp.x > gp.x) - (tp.x < gp.x), gp.y + (tp.y > gp.y) - (tp.y < gp.y)};
            if (!c->submit(st).ok) break;
            c->settle(nullptr);
        }
        if (c->distance_tiles(g, tgt) <= 1) submit(*c, {g, ActionKind::Attack, tgt, 0});
        if (c->gone(tgt)) victim = tgt;
        else end_turn(*c);
    }
    REQUIRE(victim);
    {
        bool seen_end = false, removed_after = false;
        for (const auto& e : c->events()) {
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SummonEnded && e.rules.target == victim) { seen_end = true; CHECK(e.rules.b == 3); }
            if (e.kind == SimEvent::Kind::TokenRemoved && e.actor == victim) { removed_after = seen_end; CHECK(e.rules.a == 3); }
        }
        CHECK(removed_after);
        CHECK_FALSE(c->registry().find(victim));
        CHECK(rules5e::find(c->state(), victim)->gone);
    }
    end_turn(*c);
    // concentration ends (Hold Person replaces it): the other wolf ends with b 1
    to_actor(*c, w);
    rules5e::ActionRequest hp{w, ActionKind::CastSpell, g};
    hp.spell_id = spell_id_of("srd51.spell.hold_person");
    hp.spell_level = 2;
    const std::size_t n0 = c->events().size();
    REQUIRE(submit(*c, hp).ok);
    const CombatantId other = victim == ids[0] ? ids[1] : ids[0];
    CHECK(c->gone(other));
    std::size_t end_i = 0, tok_i = 0, last_rules = 0;
    for (std::size_t i = n0; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind == SimEvent::Kind::Rules) last_rules = i;
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SummonEnded) { end_i = i; CHECK(e.rules.b == 1); }
        if (e.kind == SimEvent::Kind::TokenRemoved) tok_i = i;
    }
    CHECK(end_i > 0);
    CHECK(tok_i > last_rules);   // after the step's rules events (and its SpellTarget)
    // a gone summon blocks nothing and keeps its last tile
    const auto occ = c->occupied_except(0);
    CHECK(std::find(occ.begin(), occ.end(), c->pos(other)) == occ.end());
    same_on_replay(*c, ch, lib, 4);
}

TEST_CASE("encounter end teardown (D15): SummonEnded b 5 + inline TokenRemoved, sim events before it, EncounterEnded last; replay") {
    auto lib = make_lib(1);   // a 1 HP goblin
    const auto ch = arena({{kWizard, Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{9, 5}}});
    const CombatantId w = ch.spawns[0].stable_id, g = ch.spawns[1].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 6);
    to_actor(*c, w);
    REQUIRE(submit(*c, summon(w, kFamiliar, {0}, 1)).ok);
    end_turn(*c);
    for (int t = 0; t < 30 && !c->ended(); ++t) {
        to_actor(*c, w);
        rules5e::ActionRequest fb{w, ActionKind::CastSpell, g};
        fb.spell_id = spell_id_of("srd51.spell.fire_bolt");
        const auto r = submit(*c, fb);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (!c->ended()) end_turn(*c);
    }
    REQUIRE(c->ended());
    const auto& ev = c->events();
    CHECK(ev.back().kind == SimEvent::Kind::Rules);
    CHECK(ev.back().rules.kind == EventKind::EncounterEnded);
    std::size_t end_i = 0, target_i = 0;
    for (std::size_t i = 0; i < ev.size(); ++i) {
        if (ev[i].kind == SimEvent::Kind::Rules && ev[i].rules.kind == EventKind::SummonEnded && ev[i].rules.b == 5) {
            end_i = i;
            REQUIRE(i + 1 < ev.size());
            CHECK(ev[i + 1].kind == SimEvent::Kind::TokenRemoved);
            CHECK(ev[i + 1].rules.a == 5);
        }
        if (ev[i].kind == SimEvent::Kind::SpellTarget) target_i = i;
    }
    REQUIRE(end_i > 0);
    CHECK(target_i < end_i);   // the ending step's SpellTarget comes before the teardown (v10)
    for (CombatantId id : summons_of(*c)) CHECK(c->gone(id));
    same_on_replay(*c, ch, lib, 6);
}

TEST_CASE("turn key: a summon joining before the current actor shifts turn_index, not the key; run_to_end with summons; replay") {
    auto lib = make_lib(40);
    bool shifted = false;
    for (uint64_t seed = 1; seed <= 60 && !shifted; ++seed) {
        const auto ch = arena({{kWizard, Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{12, 5}}});
        const CombatantId w = ch.spawns[0].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, seed);
        to_actor(*c, w);
        const auto k0 = c->turn_key();
        const auto ti = c->state().turn_index;
        REQUIRE(submit(*c, summon(w, kFamiliar, {0}, 1)).ok);   // own initiative
        CHECK(c->turn_key() == k0);
        if (c->state().turn_index != ti) shifted = true;
    }
    CHECK(shifted);
    // a whole fight driven by run_to_end: commanded conjured wolves and an Animate Dead group (bonus-action commands)
    const auto ch = arena({{kWizard, Vec2i{5, 5}}, {"srd51.monster.goblin", Vec2i{10, 5}}});
    const CombatantId w = ch.spawns[0].stable_id;
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 9);
    to_actor(*c, w);
    REQUIRE(submit(*c, summon(w, kAnimate, {0, 1})).ok);
    end_turn(*c);
    NearestEnemyPolicy pol;
    const auto rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &pol; }, 600);
    CHECK_MESSAGE(rr.error.empty(), rr.error);
    CHECK(c->ended());
    int commanded = 0;
    for (const auto& e : c->events()) commanded += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::Commanded;
    CHECK(commanded >= 1);
    same_on_replay(*c, ch, lib, 9);
}

TEST_CASE("summon max_count: absent = no cap (SRD multipliers in full); a present cap that clips a band warns, unhashed") {
    {
        auto lib = make_lib();
        CHECK(lib.warnings().empty());
        const auto* d = lib.spells()->find(kConjure);
        REQUIRE(d);
        CHECK(d->summon.max_count == 255);
        const auto ch = arena({{kWizard, Vec2i{10, 10}}, {"srd51.monster.goblin", Vec2i{25, 10}}});
        const CombatantId w = ch.spawns[0].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        CHECK(d->summon.max_count >= 32);   // slot 9: 8 x 4 CR 1/4 creatures fit under "no cap"
        to_actor(*c, w);
        auto r = submit(*c, summon(w, kConjure, std::vector<uint8_t>(16, 0), 5));   // slot 5: 16 wolves (8 x 2)
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(summons_of(*c).size() == 16);
        same_on_replay(*c, ch, lib, 3);
    }
    {
        auto lib = make_lib(60, 11, uint8_t(8));
        const auto* d = lib.spells()->find(kConjure);
        REQUIRE(d);
        CHECK(d->summon.max_count == 8);
        const auto& wv = lib.warnings();
        REQUIRE_FALSE(wv.empty());
        CHECK(std::find(wv.begin(), wv.end(), "test.conjure_animals: summon max_count 8 clips the CR band count 16 at slot level 5") != wv.end());
        for (const auto& line : wv) CHECK(line.find("band count 8 ") == std::string::npos);   // 8 itself is not clipped
        const auto ch = arena({{kWizard, Vec2i{10, 10}}, {"srd51.monster.goblin", Vec2i{25, 10}}});
        const CombatantId w = ch.spawns[0].stable_id;
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 3);
        REQUIRE(c);
        to_actor(*c, w);
        CHECK_FALSE(submit(*c, summon(w, kConjure, std::vector<uint8_t>(16, 0), 5)).ok);
        // the same encounter hashes the same with or without the warnings (they are not hashed)
        auto lib2 = make_lib();
        ecs::Registry r1, r2;
        auto a = Combat::create(r1, ch, lib, 3);
        auto b = Combat::create(r2, ch, lib2, 3);
        REQUIRE(a);
        REQUIRE(b);
        CHECK(a->hash() == b->hash());
    }
}
