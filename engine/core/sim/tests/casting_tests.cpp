// CastSpell plumbing (event schema v5): monster@3 / character@3 spellcasting blocks reach the sim
// through ContentLibrary, and a content-authored caster casts through sim::Combat using the rules5e
// spell hooks. Pre-@3 inertness is pinned by compat_defenses_tests.cpp.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "assets/content_migrations.hpp"
#include "base/canonical_hash.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::spell_id_of;
using Json = assets::Json;
namespace fs = std::filesystem;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kWizard = "test.character.wizard";
const char* kGoblin = "srd51.monster.goblin";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
const char* kSpellFiles[] = {"counterspell", "cure_wounds", "fire_bolt", "hold_person", "magic_missile", "shield"};

Json wizard_doc(Json spells = Json::array({"srd51.spell.fire_bolt", "srd51.spell.magic_missile", "srd51.spell.hold_person",
                                           "srd51.spell.cure_wounds", "srd51.spell.shield", "srd51.spell.counterspell"})) {
    Json w = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    w["id"] = kWizard;
    w["abilities"]["int"] = 16;
    w["hit_points"]["average"] = 30;
    w["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"slots", {{"1", 4}, {"2", 3}, {"3", 2}}}, {"spells", spells}};
    return w;
}

void load_spells(ContentLibrary& lib) {
    for (const char* n : kSpellFiles)
        REQUIRE_FALSE(lib.add_spell(load_json(std::string("content/packs/srd51.core/spells/") + n + ".json")));
}

// Library with the six core spells, a @3 wizard and a goblin (hp raised so fights last).
ContentLibrary make_lib(const Json& wizard = wizard_doc(), const Json* goblin = nullptr) {
    ContentLibrary lib;
    load_spells(lib);
    REQUIRE_FALSE(lib.add(wizard, "character"));
    Json g = goblin ? *goblin : load_json("content/packs/srd51.core/monsters/goblin.json");
    g["hit_points"]["average"] = 60;
    REQUIRE_FALSE(lib.add(g, "monster"));
    return lib;
}

struct Spawn { std::string archetype; int x, y; };
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> sight_walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : sight_walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}

struct Duel {
    ecs::Registry reg;
    world::Chunk chunk;
    std::optional<Combat> c;
    CombatantId wiz = 0, gob = 0;
    Duel(const ContentLibrary& lib, std::vector<Spawn> spawns, uint64_t seed = 1, std::vector<Vec2i> walls = {}) {
        chunk = arena(spawns, walls);
        c = Combat::create(reg, chunk, lib, seed);
        REQUIRE(c);
        wiz = chunk.spawns[0].stable_id;
        gob = chunk.spawns[1].stable_id;
        advance_to(wiz);
    }
    SubmitResult settle(SubmitResult r) {   // decline every reaction
        while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void advance_to(CombatantId who) {
        for (int i = 0; i < 6 && rules5e::current_actor(c->state()) != who; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(settle(c->submit(e)).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == who);
    }
    SubmitResult cast(const char* spell, uint8_t level, CombatantId target, std::vector<CombatantId> targets = {}) {
        Command cmd;
        cmd.request = {rules5e::current_actor(c->state()), ActionKind::CastSpell, target};
        cmd.request.spell_id = spell_id_of(std::string("srd51.spell.") + spell);
        cmd.request.spell_level = level;
        cmd.request.targets = std::move(targets);
        return settle(c->submit(cmd));
    }
    int count(EventKind k) const {
        int n = 0;
        for (const auto& e : c->events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == k;
        return n;
    }
    int spell_targets() const {
        int n = 0;
        for (const auto& e : c->events()) n += e.kind == SimEvent::Kind::SpellTarget;
        return n;
    }
    const rules5e::Combatant& me() const { return *rules5e::find(c->state(), wiz); }
    const rules5e::Combatant& foe() const { return *rules5e::find(c->state(), gob); }
};
} // namespace

TEST_CASE("casting content: character@3 spellcasting block, creature type and size load") {
    auto lib = make_lib();
    const Archetype* w = lib.find(kWizard);
    REQUIRE(w);
    const auto& b = w->base;
    CHECK(b.spellcasting_ability == rules5e::Ability::Int);
    CHECK(b.caster_level == 5);
    CHECK(b.spell_slots[1] == 4);
    CHECK(b.spell_slots[2] == 3);
    CHECK(b.spell_slots[3] == 2);
    CHECK(b.spell_slots[4] == 0);
    CHECK(b.spell_slots_max == b.spell_slots);
    CHECK(b.spells_known.size() == 6);
    CHECK(b.spells_known[0] == spell_id_of("srd51.spell.fire_bolt"));
    CHECK(w->spell_ids.size() == 6);
    CHECK(b.creature_type == rules5e::CreatureType::Humanoid);
    CHECK(w->size == CreatureSize::Medium);
    CHECK(w->inert_mechanics.empty());
    // Absent block: nothing cast-related.
    const Archetype* g = lib.find(kGoblin);
    REQUIRE(g);
    CHECK(g->base.spells_known.empty());
    CHECK(g->base.caster_level == 0);
    CHECK(g->size == CreatureSize::Small);
    // Pact Magic and a non-humanoid monster@3.
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["id"] = "test.monster.lich_ish";
    m["type"] = "undead";
    m["size"] = "large";
    m["spellcasting"] = {{"ability", "cha"}, {"caster_level", 9}, {"pact", {{"level", 3}, {"slots", 2}}},
                         {"spells", Json::array({"srd51.spell.fire_bolt"})}};
    REQUIRE_FALSE(lib.add(m, "monster"));
    const Archetype* l = lib.find("test.monster.lich_ish");
    CHECK(l->base.creature_type == rules5e::CreatureType::Undead);
    CHECK(l->size == CreatureSize::Large);
    CHECK(l->base.pact_slot_level == 3);
    CHECK(l->base.pact_slots == 2);
    CHECK(l->base.pact_slots_max == 2);
    CHECK(l->base.spellcasting_ability == rules5e::Ability::Cha);
}

TEST_CASE("casting content: malformed spellcasting blocks are load errors") {
    auto bad = [](const std::function<void(Json&)>& f, const char* needle) {
        Json w = wizard_doc();
        f(w);
        ContentLibrary lib;
        auto e = lib.add(w, "character");
        REQUIRE(e);
        CHECK(e->find(needle) != std::string::npos);
    };
    bad([](Json& w) { w["spellcasting"]["ability"] = "luck"; }, "ability");
    bad([](Json& w) { w["spellcasting"]["caster_level"] = 0; }, "caster_level");
    bad([](Json& w) { w["spellcasting"]["slots"]["10"] = 1; }, "slots");
    bad([](Json& w) { w["spellcasting"]["pact"] = {{"level", 9}, {"slots", 1}}; }, "pact");
    bad([](Json& w) { w["type"] = "robot"; }, "creature type");
}

TEST_CASE("casting content: the 2 -> 3 migration marks pre-@3 spellcasting / non-humanoid type inert") {
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@2";
    m["type"] = "undead";
    m["spellcasting"] = wizard_doc()["spellcasting"];
    auto r = reg.load(m);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(r.data["inert_mechanics"] == Json::array({"spellcasting", "creature_type"}));
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add(r.data, "monster"));
    const Archetype* a = lib.find(kGoblin);
    CHECK(a->base.creature_type == rules5e::CreatureType::Humanoid);
    CHECK(a->base.spells_known.empty());
    CHECK(a->base.caster_level == 0);
    // A plain humanoid pre-@3 file gets no marker at all.
    Json h = load_json("content/packs/srd51.core/monsters/goblin.json");
    h["$schema"] = "monster@2";
    auto rh = reg.load(h);
    REQUIRE(rh.ok);
    CHECK_FALSE(rh.data.contains("inert_mechanics"));
}

TEST_CASE("casting content: PackSet load builds the SpellBook and rejects unresolved spellcasting.spells") {
    const fs::path root = fs::temp_directory_path() / "isoforge_casting_pack";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path dir = root / "sample.vertical_slice";
    fs::copy(kRoot + "/tests/compat/fl1/sample.vertical_slice", dir, fs::copy_options::recursive);
    Json f = load_json("tests/compat/fl1/sample.vertical_slice/characters/fighter_1.json");
    f["$schema"] = "character@3";
    f["spellcasting"] = {{"ability", "wis"}, {"caster_level", 3}, {"spells", Json::array({"test.spell.nope"})}};
    std::ofstream(dir / "characters/fighter_1.json") << f.dump(2);
    ContentLibrary lib;
    auto errs = lib.load_pack(dir.string());
    fs::remove_all(root);
    REQUIRE_FALSE(errs.empty());
    bool found = false;
    for (const auto& e : errs) found = found || e.find("not a loaded spell@1") != std::string::npos;
    CHECK(found);
    // The live sample set (srd51.core in depends) carries the six core spells.
    ContentLibrary live;
    const std::filesystem::path roots[] = {std::filesystem::path(kRoot) / "content/packs"};
    const std::string req[] = {"sample.vertical_slice"};
    const auto set = assets::PackSet::resolve(roots, req);
    REQUIRE(live.load(set).empty());
    CHECK(live.spells()->all().size() == 6);
}

TEST_CASE("cast: Fire Bolt spends no slot, rolls a spell attack, emits SpellTarget (stream v5)") {
    auto lib = make_lib();
    Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 20, 10}});   // 50 ft
    CHECK(event_stream_version(d.c->events()) == 4);       // no cast yet: v4 stream
    auto r = d.cast("fire_bolt", 0, d.gob);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(d.count(EventKind::SpellCast) == 1);
    CHECK(d.count(EventKind::AttackResolved) == 1);
    CHECK(d.count(EventKind::SpellResolved) == 1);
    CHECK(d.me().spell_slots[1] == 4);
    CHECK(d.spell_targets() == 1);
    CHECK(event_stream_version(d.c->events()) == 5);
    bool saw = false;
    for (const auto& e : d.c->events())
        if (e.kind == SimEvent::Kind::SpellTarget) {
            saw = true;
            CHECK(e.actor == d.wiz);
            CHECK(e.rules.target == d.gob);
            CHECK(e.rules.a == int32_t(spell_id_of("srd51.spell.fire_bolt")));
            CHECK(e.tile == Vec2i{20, 10});
        }
    CHECK(saw);
    // Action spent: a second cast is refused.
    CHECK_FALSE(d.cast("fire_bolt", 0, d.gob).ok);
}

TEST_CASE("cast: Magic Missile spends a slot and always damages; upcast spends the chosen slot") {
    auto lib = make_lib();
    Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 14, 10}});
    const int hp = d.foe().hp;
    auto r = d.cast("magic_missile", 2, d.gob);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(d.me().spell_slots[2] == 2);
    CHECK(d.me().spell_slots[1] == 4);
    CHECK(d.foe().hp < hp);
    CHECK(d.count(EventKind::DamageDealt) >= 4);   // 3 darts + 1 upcast dart
}

TEST_CASE("cast: rejections (unknown, not known, no slot, range, line of sight, components)") {
    auto lib = make_lib(wizard_doc(Json::array({"srd51.spell.fire_bolt", "srd51.spell.magic_missile"})));
    {
        Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 20, 10}});
        Command bogus;
        bogus.request = {d.wiz, ActionKind::CastSpell, d.gob};
        bogus.request.spell_id = spell_id_of("test.spell.bogus");
        auto r = d.c->submit(bogus);
        CHECK_FALSE(r.ok);
        CHECK(r.error == "unknown spell");
        CHECK(d.cast("hold_person", 2, d.gob).error == "spell not known");
        CHECK(d.cast("magic_missile", 5, d.gob).error == "no spell slot of that level");
        CHECK(d.cast("fire_bolt", 1, d.gob).error == "cantrips use no slot");
        CHECK(d.count(EventKind::SpellCast) == 0);   // nothing appended by rejected casts
        CHECK(d.me().spell_slots[2] == 3);
    }
    {
        Duel d(lib, {{kWizard, 2, 10}, {kGoblin, 28, 10}});   // 130 ft > Fire Bolt's 120
        CHECK(d.cast("fire_bolt", 0, d.gob).error == "invalid spell target");
    }
    {
        // A sight-blocking wall between them.
        Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 14, 10}}, 1, {{12, 10}});
        CHECK(d.cast("fire_bolt", 0, d.gob).error == "invalid spell target");
        CHECK(d.cast("magic_missile", 1, d.gob).error == "invalid spell target");
    }
    {
        // A material cost nobody can pay yet: refused (no inventory).
        Json costly = load_json("content/packs/srd51.core/spells/fire_bolt.json");
        costly["id"] = "srd51.spell.costly_bolt";
        costly["components"]["m"] = {{"text", "a diamond"}, {"cost_cp", 5000}, {"consumed", true}};
        ContentLibrary l2;
        load_spells(l2);
        auto e = l2.add_spell(costly);
        if (!e) {
            Json w = wizard_doc(Json::array({"srd51.spell.costly_bolt"}));
            REQUIRE_FALSE(l2.add(w, "character"));
            Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
            REQUIRE_FALSE(l2.add(g, "monster"));
            Duel d(l2, {{kWizard, 10, 10}, {kGoblin, 14, 10}});
            Command cmd;
            cmd.request = {d.wiz, ActionKind::CastSpell, d.gob};
            cmd.request.spell_id = spell_id_of("srd51.spell.costly_bolt");
            CHECK(d.c->submit(cmd).error == "components unavailable");
        }
    }
}

TEST_CASE("cast: Hold Person: humanoid only, condition immunity and concentration") {
    auto lib = make_lib();
    {   // Humanoid goblin (@3): paralysed on a failed save for some seed; slot always spent.
        bool held = false;
        for (uint64_t seed = 1; seed < 60 && !held; ++seed) {
            Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 13, 10}}, seed);
            auto r = d.cast("hold_person", 2, d.gob);
            REQUIRE_MESSAGE(r.ok, r.error);
            CHECK(d.me().spell_slots[2] == 2);
            CHECK(d.count(EventKind::SavingThrowRolled) == 1);
            if (d.foe().has(rules5e::Condition::Paralyzed)) {
                held = true;
                CHECK(d.me().concentration_spell == spell_id_of("srd51.spell.hold_person"));
                CHECK(d.count(EventKind::ConditionAdded) == 1);
            }
        }
        CHECK(held);
    }
    {   // Undead (@3): invalid creature type.
        Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
        g["type"] = "undead";
        auto lib2 = make_lib(wizard_doc(), &g);
        Duel d(lib2, {{kWizard, 10, 10}, {kGoblin, 13, 10}});
        CHECK(d.cast("hold_person", 2, d.gob).error == "invalid creature type for this spell");
        CHECK(d.me().spell_slots[2] == 3);
    }
    {   // Condition immunity (@3): never paralysed, no ConditionAdded, no lasting effect, concentration not taken.
        Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
        g["condition_immunities"] = Json::array({"paralyzed"});
        auto lib3 = make_lib(wizard_doc(), &g);
        for (uint64_t seed = 1; seed < 40; ++seed) {
            Duel d(lib3, {{kWizard, 10, 10}, {kGoblin, 13, 10}}, seed);
            REQUIRE(d.cast("hold_person", 2, d.gob).ok);
            CHECK_FALSE(d.foe().has(rules5e::Condition::Paralyzed));
            CHECK(d.count(EventKind::ConditionAdded) == 0);
            CHECK(d.c->state().effects.empty());
            CHECK(d.me().concentration_spell == 0);
        }
    }
}

TEST_CASE("cast: Cure Wounds is touch range and skips undead") {
    Json ally = load_json("content/packs/srd51.core/monsters/goblin.json");
    ally["id"] = "test.monster.ally";
    auto lib = make_lib();
    REQUIRE_FALSE(lib.add(ally, "monster"));
    {   // teams: wizard party, goblin monsters; the cure target is the goblin here (same-team rule is not enforced by rules5e).
        Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 13, 10}});
        d.c->state();   // (adjacent test below)
        CHECK(d.cast("cure_wounds", 1, d.gob).error == "invalid spell target");   // 15 ft: not touch
    }
    {
        Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 11, 10}});
        const_cast<rules5e::Combatant&>(d.foe()).hp = 10;
        REQUIRE(d.cast("cure_wounds", 1, d.gob).ok);
        CHECK(d.foe().hp > 10);
        CHECK(d.count(EventKind::Healed) == 1);
    }
    {
        Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
        g["type"] = "undead";
        auto lib2 = make_lib(wizard_doc(), &g);
        Duel d(lib2, {{kWizard, 10, 10}, {kGoblin, 11, 10}});
        const_cast<rules5e::Combatant&>(d.foe()).hp = 10;
        REQUIRE(d.cast("cure_wounds", 1, d.gob).ok);
        CHECK(d.foe().hp == 10);   // no effect on undead
        CHECK(d.count(EventKind::Healed) == 0);
    }
}

TEST_CASE("cast: Shield is offered when the caster is hit (BeingHit reach = the creature hit)") {
    auto lib = make_lib();
    bool offered = false;
    for (uint64_t seed = 1; seed < 80 && !offered; ++seed) {
        Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 11, 10}}, seed);
        d.advance_to(d.gob);
        Command atk;
        atk.request = {d.gob, ActionKind::Attack, d.wiz};
        atk.request.attack_index = 0;
        auto r = d.c->submit(atk);
        REQUIRE_MESSAGE(r.ok, r.error);
        if (!r.needs) continue;   // missed: no window
        CHECK(r.needs->trigger == rules5e::ReactionTrigger::BeingHit);
        CHECK(r.needs->reactor == d.wiz);
        REQUIRE_FALSE(r.needs->options.empty());
        CHECK(r.needs->options[0].kind == rules5e::ReactionOptionKind::AcBonus);
        CHECK(r.needs->options[0].option_id == spell_id_of("srd51.spell.shield"));
        CHECK(r.needs->options[0].slot_level == 1);
        offered = true;
        auto r2 = d.c->decide({r.needs->window_seq, true, 0});
        while (r2.needs) r2 = d.c->decide({r2.needs->window_seq, false, 0});
        CHECK(d.me().spell_slots[1] == 3);   // Shield spent a 1st-level slot
        CHECK(d.count(EventKind::AcBonus) == 1);
    }
    CHECK(offered);
}

TEST_CASE("cast: Counterspell reaches a caster within 60 ft with line of sight") {
    // Goblin-side counterspeller (monster@3 with the spell) vs the wizard casting Magic Missile.
    Json g = load_json("content/packs/srd51.core/monsters/goblin.json");
    g["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"slots", {{"3", 2}}},
                         {"spells", Json::array({"srd51.spell.counterspell"})}};
    auto lib = make_lib(wizard_doc(), &g);
    {
        Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 15, 10}});   // 25 ft
        auto r = d.c->submit([&] {
            Command cmd;
            cmd.request = {d.wiz, ActionKind::CastSpell, d.gob};
            cmd.request.spell_id = spell_id_of("srd51.spell.magic_missile");
            cmd.request.spell_level = 1;
            return cmd;
        }());
        REQUIRE_MESSAGE(r.ok, r.error);
        REQUIRE(r.needs);
        CHECK(r.needs->trigger == rules5e::ReactionTrigger::SpellCast);
        CHECK(r.needs->reactor == d.gob);
        CHECK(r.needs->options[0].kind == rules5e::ReactionOptionKind::Counter);
        auto r2 = d.c->decide({r.needs->window_seq, true, 0});
        while (r2.needs) r2 = d.c->decide({r2.needs->window_seq, false, 0});
        CHECK(d.count(EventKind::SpellCountered) == 1);
        CHECK(d.foe().hp == 60);   // countered: no darts
    }
    {
        Duel d(lib, {{kWizard, 2, 10}, {kGoblin, 20, 10}});   // 90 ft: out of Counterspell's 60 ft
        // Magic Missile is 120 ft, so the cast is legal but nobody can counter it.
        Command cmd;
        cmd.request = {d.wiz, ActionKind::CastSpell, d.gob};
        cmd.request.spell_id = spell_id_of("srd51.spell.magic_missile");
        cmd.request.spell_level = 1;
        auto r = d.c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK_FALSE(r.needs);
        CHECK(d.count(EventKind::SpellCountered) == 0);
    }
}

TEST_CASE("cast: a spell fight replays from its command log bit-identically") {
    auto lib = make_lib();
    Duel d(lib, {{kWizard, 10, 10}, {kGoblin, 15, 10}}, 7);
    REQUIRE(d.cast("magic_missile", 1, d.gob).ok);
    REQUIRE(d.cast("fire_bolt", 0, d.gob).error != "");   // action spent
    {
        Command e;
        e.request = {d.wiz, ActionKind::EndTurn};
        REQUIRE(d.settle(d.c->submit(e)).ok);
    }
    d.advance_to(d.wiz);
    REQUIRE(d.cast("fire_bolt", 0, d.gob).ok);
    ecs::Registry reg2;
    std::string err;
    auto rp = replay(reg2, d.chunk, lib, 7, d.c->log(), &err);
    REQUIRE_MESSAGE(rp, err);
    CHECK(rp->hash() == d.c->hash());
    CHECK(event_stream_hash(rp->events()) == event_stream_hash(d.c->events()));
    CHECK(event_stream_version(d.c->events()) == 5);
}

// ---- Caster behaviour golden: pinned once on this PR's engine (no earlier engine could cast) ----
namespace {
struct Caster { uint64_t digest = 0; uint32_t commands = 0, casts = 0, slots_left = 0; int32_t foe_hp = 0, wiz_hp = 0; bool ended = false; };

// Frozen script: the wizard casts, in order of preference, Hold Person (2nd) on a standing goblin it
// has not held, Magic Missile (1st), then Fire Bolt; goblins step toward the wizard and attack.
Caster run_caster(uint64_t seed) {
    auto lib = make_lib();
    ecs::Registry reg;
    auto chunk = arena({{kWizard, 8, 10}, {kGoblin, 16, 10}, {kGoblin, 17, 12}});
    auto c = Combat::create(reg, chunk, lib, seed);
    REQUIRE(c);
    const CombatantId wiz = chunk.spawns[0].stable_id;
    Caster out;
    auto settle = [&](SubmitResult r) {
        while (r.needs) r = c->decide({r.needs->window_seq, true, 0});   // always react
        return r;
    };
    while (!c->ended() && out.commands < 400) {
        const CombatantId self = rules5e::current_actor(c->state());
        Command cmd;
        cmd.request = {self, ActionKind::EndTurn};
        const rules5e::Combatant* foe = nullptr;
        for (const auto& k : c->state().combatants)
            if (k.id != self && c->hostile(self, k.id) && !k.dead && rules5e::is_conscious(k)) { foe = &k; break; }
        if (foe) {
            const bool can_act = c->state().budget.action;
            if (self == wiz && can_act) {
                const auto& me = *rules5e::find(c->state(), wiz);
                cmd.request.kind = ActionKind::CastSpell;
                cmd.request.target = foe->id;
                if (me.spell_slots[2] > 0 && !foe->has(rules5e::Condition::Paralyzed) && me.concentration_spell == 0) {
                    cmd.request.spell_id = spell_id_of("srd51.spell.hold_person");
                    cmd.request.spell_level = 2;
                } else if (me.spell_slots[1] > 0) {
                    cmd.request.spell_id = spell_id_of("srd51.spell.magic_missile");
                    cmd.request.spell_level = 1;
                } else {
                    cmd.request.spell_id = spell_id_of("srd51.spell.fire_bolt");
                    cmd.request.spell_level = 0;
                }
                auto r = c->submit(cmd);
                if (r.ok) { ++out.casts; ++out.commands; settle(r); continue; }
                cmd.request = {self, ActionKind::EndTurn};
            } else if (self != wiz) {
                const Vec2i mp = c->pos(self), tp = c->pos(wiz);
                if (chebyshev(mp, tp) <= 1) {
                    if (c->state().budget.action) {
                        cmd.request = {self, ActionKind::Attack, wiz};
                        cmd.request.attack_index = 0;
                    }
                } else {
                    const auto path = c->grid().path(mp, {{tp.x + 1, tp.y}, {tp.x + 1, tp.y + 1}, {tp.x + 1, tp.y - 1}, {tp.x, tp.y + 1}, {tp.x, tp.y - 1}},
                                                     c->occupied_except(self));
                    if (!path.empty() && c->grid().step_cost_ft(path[0]) <= c->state().budget.movement_ft) {
                        cmd.kind = CommandKind::Step;
                        cmd.to = path[0];
                    }
                }
            }
        }
        auto r = c->submit(cmd);
        if (!r.ok) { cmd = Command{}; cmd.request = {self, ActionKind::EndTurn}; r = c->submit(cmd); }
        REQUIRE_MESSAGE(r.ok, r.error);
        settle(r);
        ++out.commands;
    }
    out.ended = c->ended();
    CanonicalHasher h;
    h.u32(event_stream_hash(c->events()) & 0xffffffffu);
    h.u32(uint32_t(event_stream_hash(c->events()) >> 32));
    for (const auto& k : c->state().combatants) {
        h.u32(k.id); h.i32(k.hp); h.byte(k.dead ? 1 : 0);
        for (int l = 1; l <= 9; ++l) h.byte(k.spell_slots[std::size_t(l)]);
        h.u32(k.concentration_spell);
        if (k.id == wiz) { out.wiz_hp = k.hp; for (int l = 1; l <= 9; ++l) out.slots_left += k.spell_slots[std::size_t(l)]; }
        else out.foe_hp += k.hp;
    }
    out.digest = h.value();
    return out;
}
} // namespace

TEST_CASE("caster golden: scripted wizard vs two goblins reproduces the frozen outcome (NEVER update these)") {
    struct Pin { uint64_t seed, digest; uint32_t commands, casts, slots_left; int32_t foe_hp, wiz_hp; };
    const Pin pins[] = {
        {1, 16205878797153991482ull, 102, 18, 0, 0, 2},
        {2, 6871533781989545959ull, 93, 13, 0, 66, 0},
        {3, 1449657030873780432ull, 70, 10, 0, 69, 0},
        {7, 18304345294248569576ull, 69, 10, 0, 71, 0},
        {2026, 2916962307613800546ull, 89, 16, 0, 0, 4},
    };
    for (const Pin& p : pins) {
        CAPTURE(p.seed);
        const auto r = run_caster(p.seed);
        MESSAGE("{" << p.seed << ", " << r.digest << "ull, " << r.commands << ", " << r.casts << ", " << r.slots_left << ", "
                    << r.foe_hp << ", " << r.wiz_hp << "},");
        CHECK(r.ended);
        CHECK(r.casts >= 1);
        CHECK(r.digest == p.digest);
        CHECK(r.commands == p.commands);
        CHECK(r.casts == p.casts);
        CHECK(r.slots_left == p.slots_left);
        CHECK(r.foe_hp == p.foe_hp);
        CHECK(r.wiz_hp == p.wiz_hp);
    }
}
