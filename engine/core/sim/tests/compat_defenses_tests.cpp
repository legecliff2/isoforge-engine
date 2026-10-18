// Compat behaviour golden for the defenses / multi-damage gate (monster@3 / character@3).
//
// POLICY: the pinned digests below NEVER change (docs/CONTENT_COMPAT.md §2). Pre-@3 stat blocks
// may carry damage_resistances / damage_immunities / damage_vulnerabilities / condition_immunities
// and several damage[] entries per attack, but those were descriptive text (the engine read none of
// them). They must stay inert: a fight between such "decorated" pre-@3 stat blocks must be
// bit-identical to the undecorated frozen tests/compat/fl1 fight. The digests here are therefore
// the fl1 goblin golden pins (compat_fl1_goblin_tests.cpp), computed on main 6012567 BEFORE the
// gate existed, where the engine ignored the decoration entirely. Same fixed melee-only command
// script, same digest recipe as compat_fl1_goblin_tests.cpp.
//
// The decorated copies are derived at test time from the frozen fl1 files (tests/compat is never
// edited): fl1 is copied to a temp dir and the stat blocks get the decoration below.
#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "base/canonical_hash.hpp"
#include "sim/combat.hpp"
#include "world/handcrafted_provider.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
const std::string kFl1 = std::string(ENGINE_SOURCE_ROOT) + "/tests/compat/fl1/sample.vertical_slice";
const std::string kGoblinId = "srd51.monster.goblin";

Json read_json(const fs::path& p) {
    std::ifstream f(p);
    REQUIRE(f.good());
    return Json::parse(f);
}
void write_json(const fs::path& p, const Json& j) {
    std::ofstream f(p);
    f << j.dump(2) << "\n";
}

// Copies frozen fl1 to a temp dir and decorates both stat blocks. `monster_schema` / `character_schema`
// replace the declared $schema ("monster@1" is the original; "@2" is a pure version relabel).
struct Decorated {
    fs::path root, dir;
    explicit Decorated(const std::string& tag, const char* monster_schema, const char* character_schema) {
        // Private parent: PackSet::from_pack_dir treats the pack's parent as the pack root.
        root = fs::temp_directory_path() / ("isoforge_compat_defenses_" + tag);
        fs::remove_all(root);
        dir = root / "sample.vertical_slice";
        fs::create_directories(root);
        fs::copy(kFl1, dir, fs::copy_options::recursive);
        Json g = read_json(dir / "monsters/goblin.json");
        g["$schema"] = monster_schema;
        g["damage_resistances"] = Json::array({"slashing", "piercing"});
        g["damage_immunities"] = Json::array({"slashing"});
        g["damage_vulnerabilities"] = Json::array({"fire"});
        g["condition_immunities"] = Json::array({"frightened", "poisoned"});
        for (auto& a : g["actions"]) a["damage"].push_back({{"dice", "1d4"}, {"type", "fire"}});
        for (auto& a : g["actions"]) a["qualifiers"] = Json::array({"silvered", "magical"});   // inert before @3
        g["traits"].push_back({{"id", "ghostly"}, {"name", "Ghostly"}, {"description", "x"},
                               {"effect", {{"kind", "damage_mitigation"},
                                           {"resistances", {{"nonmagical", Json::array({"slashing"})}}}}}});
        g["traits"].push_back({{"id", "arcane_blows"}, {"name", "Arcane Blows"}, {"description", "x"},
                               {"effect", {{"kind", "magic_weapons"}}}});
        g["type"] = "undead";   // creature type is mechanical only at @3
        g["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"slots", {{"1", 4}, {"3", 2}}},
                             {"spells", Json::array({"srd51.spell.fire_bolt", "srd51.spell.hold_person"})}};
        write_json(dir / "monsters/goblin.json", g);
        Json f = read_json(dir / "characters/fighter_1.json");
        f["$schema"] = character_schema;
        f["damage_resistances"] = Json::array({"slashing"});
        f["damage_immunities"] = Json::array({"piercing"});
        f["damage_vulnerabilities"] = Json::array({"fire"});
        f["condition_immunities"] = Json::array({"charmed"});
        for (auto& a : f["actions"]) a["damage"].push_back({{"dice", "2d6"}, {"type", "fire"}});
        for (auto& a : f["actions"]) a["qualifiers"] = Json::array({"adamantine"});
        f["spellcasting"] = {{"ability", "wis"}, {"caster_level", 3}, {"slots", {{"2", 3}}},
                             {"pact", {{"level", 1}, {"slots", 2}}}, {"spells", Json::array({"srd51.spell.cure_wounds"})}};
        write_json(dir / "characters/fighter_1.json", f);
    }
    ~Decorated() { fs::remove_all(root); }
};

const rules5e::Combatant* first_foe(const Combat& c, CombatantId self) {
    for (const auto& k : c.state().combatants)
        if (k.id != self && c.hostile(self, k.id) && !k.dead && rules5e::is_conscious(k)) return &k;
    return nullptr;
}
Command end_turn(CombatantId self) {
    Command e;
    e.request = {self, ActionKind::EndTurn};
    return e;
}
Command approach(const Combat& c, CombatantId self, Vec2i fp) {
    const auto& s = c.state();
    std::vector<Vec2i> goals;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) goals.push_back({fp.x + dx, fp.y + dy});
    const auto path = c.grid().path(c.pos(self), goals, c.occupied_except(self));
    if (path.empty() || c.grid().step_cost_ft(path[0]) > s.budget.movement_ft) return end_turn(self);
    Command st;
    st.kind = CommandKind::Step;
    st.to = path[0];
    return st;
}
// Same frozen script as compat_fl1_goblin_tests.cpp. Do not change.
Command scripted(const Combat& c, Command* fallback) {
    const auto& s = c.state();
    const CombatantId self = rules5e::current_actor(s);
    const auto* foe = first_foe(c, self);
    const auto* me = rules5e::find(s, self);
    *fallback = end_turn(self);
    if (!foe || !me || !rules5e::is_conscious(*me)) return end_turn(self);
    const Vec2i fp = c.pos(foe->id);
    const bool can_attack = s.budget.action || s.budget.attacks_remaining > 0;
    if (chebyshev(c.pos(self), fp) <= 1) {
        if (!can_attack) return end_turn(self);
        Command a;
        a.request = {self, ActionKind::Attack, foe->id};
        a.request.attack_index = 0;
        return a;
    }
    return approach(c, self, fp);
}

uint64_t run_digest(const std::string& pack_dir, uint64_t seed) {
    ContentLibrary lib;
    auto errs = lib.load_pack(pack_dir);
    REQUIRE_MESSAGE(errs.empty(), (errs.empty() ? "" : errs[0]));
    world::FileChunkProvider p({{world::ChunkCoord{0, 0, 0}, pack_dir + "/chunks/0_0_0.json"}});
    auto r = p.load(world::ChunkRequest{world::ChunkCoord{0, 0, 0}, 0, 1});
    REQUIRE_MESSAGE(r.chunk, r.error);
    ecs::Registry reg;
    auto c = Combat::create(reg, *r.chunk, lib, seed);
    REQUIRE(c);
    uint32_t commands = 0;
    while (!c->ended() && commands < 2000) {
        Command fb;
        auto res = c->submit(scripted(*c, &fb));
        if (!res.ok) res = c->submit(fb);
        REQUIRE_MESSAGE(res.ok, res.error);
        while (res.needs) res = c->decide({res.needs->window_seq, true, 0});
        ++commands;
    }
    REQUIRE(c->ended());
    CanonicalHasher h;
    for (const auto& e : c->events()) {
        if (e.kind != SimEvent::Kind::Rules) continue;
        const auto k = e.rules.kind;
        if (k != EventKind::InitiativeRolled && k != EventKind::TurnStarted && k != EventKind::Moved &&
            k != EventKind::AttackResolved && k != EventKind::DamageDealt && k != EventKind::CombatantDowned &&
            k != EventKind::CombatantDied && k != EventKind::DeathSaveRolled && k != EventKind::EncounterEnded)
            continue;
        h.byte(uint8_t(k));
        h.u32(e.rules.actor);
        h.u32(e.rules.target);
        h.i32(e.rules.a);
        h.i32(e.rules.b);
        h.i32(e.rules.c);
    }
    for (const auto& k : c->state().combatants) {
        h.u32(k.id);
        h.i32(k.hp);
        h.byte(k.dead ? 1 : 0);
        const Vec2i p2 = c->pos(k.id);
        h.i32(p2.x);
        h.i32(p2.y);
    }
    return h.value();
}
} // namespace

TEST_CASE("compat defenses: decorated pre-@3 stat blocks load with no defenses and single-damage attacks") {
    for (const char* ms : {"monster@1", "monster@2"}) {
        Decorated d(std::string("sem_") + ms, ms, ms[8] == '1' ? "character@1" : "character@2");
        ContentLibrary lib;
        REQUIRE(lib.load_pack(d.dir.string()).empty());
        const Archetype* g = lib.find(kGoblinId);
        const Archetype* f = lib.find("srd51.character.fighter_1");
        REQUIRE(g);
        REQUIRE(f);
        for (const Archetype* a : {g, f}) {
            CHECK(a->base.resistances == 0);
            CHECK(a->base.immunities == 0);
            CHECK(a->base.vulnerabilities == 0);
            CHECK(a->base.condition_immunities == 0);
            for (const auto& at : a->base.attacks) CHECK(at.extra_damage.empty());
            CHECK_FALSE(a->inert_mechanics.empty());   // reported, never applied
            CHECK(a->base.creature_type == rules5e::CreatureType::Humanoid);   // goblin "undead" is not read pre-@3
            CHECK(a->base.spells_known.empty());
            CHECK(a->base.caster_level == 0);
            CHECK(a->base.spell_slots_max == std::array<uint8_t, 10>{});
            CHECK(a->base.pact_slots_max == 0);
            CHECK(a->spell_ids.empty());
            const auto& rq = a->base.resist_qualified;
            CHECK(rq.nonmagical == 0);
            CHECK(rq.nonmagical_not_silvered == 0);
            CHECK(rq.nonmagical_not_adamantine == 0);
            CHECK(a->base.immune_qualified.nonmagical == 0);
            for (const auto& at : a->base.attacks) CHECK(at.qualifiers == 0);   // action qualifiers and magic_weapons are inert
        }
        CHECK(g->inert_mechanics.size() == 8);   // 4 defenses, damage[1..], attack_qualifiers, spellcasting, creature_type
        CHECK(f->inert_mechanics.size() == 8);   // + equipment (3 -> 4 marker: the fl1 equipment list stays inert)
        CHECK(g->base.attacks[0].damage == rules5e::DiceExpr{1, 6, 2});
        CHECK(f->base.attacks[0].damage == rules5e::DiceExpr{1, 8, 5});
    }
}

TEST_CASE("compat defenses: decorated pre-@3 fights reproduce the frozen fl1 digests (NEVER update these)") {
    // Digests == compat_fl1_goblin_tests.cpp pins (undecorated fl1), computed on main 6012567.
    struct Pin { uint64_t seed, digest; };
    const Pin pins[] = {
        {1, 17752322533980382510ull},
        {2, 5403282979316985951ull},
        {3, 15477144339721690572ull},
        {7, 7905605568869462007ull},
        {2026, 5399669302866641243ull},
    };
    const std::pair<const char*, const char*> versions[] = {{"monster@1", "character@1"}, {"monster@2", "character@2"}};
    for (const auto& [ms, cs] : versions) {
        Decorated d(std::string("fight_") + ms, ms, cs);
        for (const Pin& p : pins) {
            CAPTURE(ms);
            CAPTURE(p.seed);
            CHECK(run_digest(d.dir.string(), p.seed) == p.digest);
        }
    }
}
