// AI / sim follow-ups of Rules #222 (7.2zc generic mark: mark_ability, move_mark) and #228 (7.2zd Sanctuary ward_alternate,
// Preserve Life heal_split): the driver's deterministic choices, the validator-retry loop, and that rules accept the result.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
    static std::vector<Combat::MaterialUse>& materials(Combat& c) { return c.materials_used_; }
};
}  // namespace eng::sim

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::HealShare;
using rules5e::HealSplitCheck;
using rules5e::HealSplitReason;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kCleric = "srd51.character.cleric_level_2";
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";
const char* kMarker = "test.character.marker";
const char* kChosenMark = "test.spell.chosen_mark";
const char* kMarkedDamage = "test.spell.marked_damage";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json chosen_mark_doc() {
    return Json::parse(R"J({"$schema":"spell@1","id":"test.spell.chosen_mark","level":1,"name":"Chosen Mark","school":"enchantment",
        "classes":["warlock"],"source":{"ref":"SRD 5.1","license":"CC-BY-4.0"},"description":"x",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"ranged","feet":90},"components":{"v":true,"s":true},
        "duration":{"kind":"hours","amount":1,"concentration":true},"move_mark":{"action":"bonus_action"},
        "effects":[{"kind":"apply_modifier","scope":{"target":"marked"},
                    "modifier":{"stat":"damage","die":"1d6","damage_type":"necrotic","applies_to":"self","duration":{"kind":"concentration"}}},
                   {"kind":"apply_modifier","scope":{"target":"marked"},
                    "modifier":{"stat":"ability_check","mode":"disadvantage","ability":"chosen","applies_to":"target","duration":{"kind":"concentration"}}}]})J");
}
Json marked_damage_doc() {
    return Json::parse(R"J({"$schema":"spell@1","id":"test.spell.marked_damage","level":1,"name":"Marked Damage","school":"divination",
        "classes":["ranger"],"source":{"ref":"SRD 5.1","license":"CC-BY-4.0"},"description":"x",
        "casting_time":{"kind":"bonus_action"},"range":{"kind":"ranged","feet":90},"components":{"v":true},
        "duration":{"kind":"hours","amount":1,"concentration":true},"move_mark":{"action":"bonus_action"},
        "effects":[{"kind":"apply_modifier","scope":{"target":"marked"},
                    "modifier":{"stat":"damage","die":"1d6","applies_to":"self","when":[{"kind":"weapon_attack"}],"duration":{"kind":"concentration"}}}]})J");
}
const ContentLibrary& lib() {
    static const ContentLibrary l = [] {
        ContentLibrary x;
        REQUIRE(x.load(sample_set()).empty());
        REQUIRE_FALSE(x.add_spell(chosen_mark_doc()));
        REQUIRE_FALSE(x.add_spell(marked_damage_doc()));
        Json h = load_json("content/packs/srd51.core/characters/cleric_level_2.json");
        h["id"] = kMarker;
        h["spellcasting"]["spells"] = Json::array({kChosenMark, kMarkedDamage, "srd51.spell.sacred_flame"});
        h["spellcasting"]["slots"] = {{"1", 4}};
        auto e = x.add(h, "character");
        REQUIRE_MESSAGE(!e, (e ? *e : ""));
        return x;
    }();
    return l;
}

struct Spawn { std::string archetype; int x, y; };
struct Fight {
    ecs::Registry reg;
    world::Chunk chunk{};
    std::optional<Combat> c;
    std::vector<CombatantId> id;
    explicit Fight(const std::vector<Spawn>& spawns, uint64_t seed = 3) {
        chunk.coord = {5, 5, 0};
        chunk.origin = world::Chunk::Origin::Handcrafted;
        chunk.palette = {"", "floor"};
        for (auto& cell : chunk.layers[0]) cell.tile = 1;
        for (std::size_t i = 0; i < spawns.size(); ++i)
            chunk.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(chunk.coord, uint32_t(i)), ""});
        chunk.content_hash = world::compute_content_hash(chunk);
        c = Combat::create(reg, chunk, lib(), seed);
        REQUIRE(c);
        for (const auto& sp : chunk.spawns) id.push_back(sp.stable_id);
    }
    rules5e::EncounterState& st() { return CombatTestAccess::state(*c); }
    rules5e::Combatant& who(std::size_t i) {
        for (auto& k : st().combatants) if (k.id == id[i]) return k;
        FAIL("no such combatant");
        return st().combatants.front();
    }
    SubmitResult settle(SubmitResult r) {
        while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    SubmitResult submit(const Command& x) { return settle(c->submit(x)); }
    void advance_to(std::size_t i) {
        for (int k = 0; k < 12 && rules5e::current_actor(c->state()) != id[i]; ++k) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == id[i]);
    }
    void ward(std::size_t i, int16_t dc = 30) {   // a Sanctuary ward on spawn i (what the cast leaves behind)
        rules5e::ActiveSpellEffect e{};
        e.spell_id = rules5e::spell_id_of("srd51.spell.sanctuary");
        e.caster = id[i];
        e.target = id[i];
        e.condition = Condition::Warded;
        e.save_ability = rules5e::Ability::Wis;
        e.dc = dc;
        st().effects.push_back(e);
        who(i).conditions |= rules5e::bit(Condition::Warded);
    }
    int count(rules5e::EventKind k, CombatantId target = 0) const {
        int n = 0;
        for (const auto& e : c->events())
            n += e.kind == SimEvent::Kind::Rules && e.rules.kind == k && (target == 0 || e.rules.target == target);
        return n;
    }
};
using Flat = std::vector<std::pair<CombatantId, int>>;
Flat flat(const std::vector<HealShare>& v) {
    Flat o;
    for (const auto& x : v) o.push_back({x.target, int(x.amount)});
    return o;
}
uint32_t preserve_life_id(const Combat& c, CombatantId cleric) {
    for (const auto& f : c.archetype(cleric)->active) if (f.id == "preserve_life") return f.feature_id;
    return 0;
}
const rules5e::FeatureSpec& preserve_spec(const Combat& c, CombatantId cleric) {
    return c.archetype(cleric)->feature_specs.at(preserve_life_id(c, cleric));
}
}  // namespace

// ---------------------------------------------------------------- 7.2zd Sanctuary

TEST_CASE("AI ward: a failed ward save retargets an attack to the nearest conscious hostile alternate, never an ally") {
    // 0 goblin (attacker), 1 fighter (warded, the policy's target), 2 cleric 4 tiles away, 3 fighter 2 tiles away, 4 goblin adjacent.
    Fight f({{kGoblin, 10, 10}, {kFighter, 11, 10}, {kCleric, 14, 10}, {kFighter, 12, 10}, {kGoblin, 9, 10}});
    f.ward(1);
    f.advance_to(0);
    rules5e::ActionRequest q{f.id[0], ActionKind::Attack, f.id[1]};
    q.attack_index = 1;   // shortbow: every creature is in range
    CHECK(ward_target_of(*f.c, q) == f.id[1]);
    CHECK(choose_ward_alternate(*f.c, q) == f.id[3]);
    // The driver fills it: the melee attack on the adjacent warded fighter carries the scimitar's alternate (fighter 3 is out of
    // the scimitar's reach, the cleric too; the adjacent goblin is an ally): none, so the attack would be lost.
    const Command melee = next_command(*f.c, f.id[1]);
    REQUIRE(melee.request.kind == ActionKind::Attack);
    CHECK(melee.request.attack_index == 0);
    CHECK(melee.request.ward_alternate == 0);
    // Shortbow request completed by the driver and accepted by rules: the failed save (DC 30) sends it to fighter 3.
    Command shot;
    shot.request = q;
    complete_request(*f.c, shot.request);
    CHECK(shot.request.ward_alternate == f.id[3]);
    const auto r = f.submit(shot);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(f.c->log().back().step.request.ward_alternate == f.id[3]);
    CHECK(f.count(rules5e::EventKind::AttackResolved) == 1);
}

TEST_CASE("AI ward: equally near alternates go to the lowest id") {
    Fight f({{kGoblin, 10, 10}, {kFighter, 11, 10}, {kFighter, 10, 12}, {kCleric, 12, 10}});
    f.ward(1);
    f.advance_to(0);
    rules5e::ActionRequest q{f.id[0], ActionKind::Attack, f.id[1]};
    q.attack_index = 1;
    CHECK(choose_ward_alternate(*f.c, q) == std::min(f.id[2], f.id[3]));   // both 2 tiles away
}

TEST_CASE("AI ward: no legal alternate: ward_alternate stays 0, the attack is lost and the action is not refunded") {
    Fight f({{kGoblin, 10, 10}, {kFighter, 11, 10}, {kCleric, 13, 10}});
    f.ward(1);
    f.ward(2);   // the only other hostile is warded too: ward_alternate_targets excludes it
    f.advance_to(0);
    const auto hp_before = f.who(1).hp;
    const Command a = next_command(*f.c, f.id[1]);
    REQUIRE(a.request.kind == ActionKind::Attack);
    CHECK(a.request.ward_alternate == 0);
    const auto r = f.submit(a);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(f.count(rules5e::EventKind::AttackResolved) == 0);   // lost on the failed save
    CHECK(f.who(1).hp == hp_before);
    CHECK_FALSE(f.c->state().budget.action);                  // spent
}

TEST_CASE("AI ward: an unwarded target leaves the request alone (nothing folded under 0xCD)") {
    Fight f({{kGoblin, 10, 10}, {kFighter, 11, 10}, {kCleric, 12, 10}});
    f.advance_to(0);
    const Command a = next_command(*f.c, f.id[1]);
    REQUIRE(a.request.kind == ActionKind::Attack);
    CHECK(a.request.ward_alternate == 0);
    CHECK(ward_target_of(*f.c, a.request) == 0);
}

TEST_CASE("AI ward: a targeted spell's alternate skips the spell's own targets; rules accept it") {
    // 0 cleric casts Sacred Flame at the warded goblin 1; goblins 2 (3 tiles) and 3 (2 tiles); fighter 4 is an ally.
    Fight f({{kCleric, 10, 10}, {kGoblin, 14, 10}, {kGoblin, 13, 10}, {kGoblin, 12, 10}, {kFighter, 11, 10}});
    f.ward(1);
    f.advance_to(0);
    Command cast;
    cast.request = {f.id[0], ActionKind::CastSpell, f.id[1]};
    cast.request.spell_id = rules5e::spell_id_of("srd51.spell.sacred_flame");
    cast.request.targets = {f.id[1]};
    CHECK(ward_target_of(*f.c, cast.request) == f.id[1]);
    complete_request(*f.c, cast.request);
    CHECK(cast.request.ward_alternate == f.id[3]);
    cast.request.targets = {f.id[1], f.id[3]};   // were goblin 3 already a target, the next one is picked
    CHECK(choose_ward_alternate(*f.c, cast.request) == f.id[2]);
    cast.request.targets = {f.id[1]};
    const auto r = f.submit(cast);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(f.c->log().back().step.request.ward_alternate == f.id[3]);
}

// ---------------------------------------------------------------- 7.2zd Preserve Life

TEST_CASE("AI Preserve Life: lowest hp first, ties to the lower id, capped at half max, pool 5 x cleric level") {
    // 0 cleric (pool 10), 1-3 fighters within 30 ft, 4 goblin.
    Fight f({{kCleric, 10, 10}, {kFighter, 11, 10}, {kFighter, 12, 10}, {kFighter, 13, 10}, {kGoblin, 20, 20}});
    for (std::size_t i : {1u, 2u, 3u}) f.who(i).hp_max = 20;
    f.who(1).hp = 6;   // room 4
    f.who(2).hp = 2;   // room 8
    f.who(3).hp = 2;   // room 8
    const auto& spec = preserve_spec(*f.c, f.id[0]);
    REQUIRE(spec.split_pool == 10);
    const auto split = plan_heal_split(*f.c, f.id[0], spec);
    const CombatantId lo = std::min(f.id[2], f.id[3]), hi = std::max(f.id[2], f.id[3]);
    REQUIRE(split.size() == 2);
    CHECK(split[0].target == lo);
    CHECK(split[0].amount == 8);
    CHECK(split[1].target == hi);
    CHECK(split[1].amount == 2);
    // The AI uses it (plan >= half the pool) and rules accept the split.
    f.advance_to(0);
    const Command u = next_command(*f.c, f.id[4]);
    REQUIRE(u.request.kind == ActionKind::UseFeature);
    CHECK(u.request.feature_id == preserve_life_id(*f.c, f.id[0]));
    CHECK(u.request.heal_split.size() == 2);
    const auto r = f.submit(u);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(f.who(2).hp + f.who(3).hp == 14);
    CHECK(f.who(1).hp == 6);
}

TEST_CASE("AI Preserve Life: a downed ally triggers it; a small top-up does not") {
    Fight f({{kCleric, 10, 10}, {kFighter, 11, 10}, {kGoblin, 20, 20}});
    f.who(1).hp_max = 20;
    f.who(1).hp = 8;   // room 2 < half the pool, nobody at 0: keep Channel Divinity
    f.advance_to(0);
    CHECK(next_command(*f.c, f.id[2]).request.kind != ActionKind::UseFeature);
    f.who(1).hp = 0;
    f.who(1).conditions |= rules5e::bit(Condition::Unconscious);
    const Command u = next_command(*f.c, f.id[2]);
    REQUIRE(u.request.kind == ActionKind::UseFeature);
    REQUIRE(u.request.heal_split.size() == 1);
    CHECK(u.request.heal_split[0].amount == 10);
    REQUIRE(f.submit(u).ok);
    CHECK(f.who(1).hp == 10);
}

TEST_CASE("AI Preserve Life: rules' validator rejections (out of range, excluded type) drop the ally and re-split") {
    // 1 out of range (lowest hp), 2 undead-typed (next lowest), 3 in range.
    Fight f({{kCleric, 10, 10}, {kFighter, 25, 25}, {kFighter, 11, 10}, {kFighter, 12, 10}, {kGoblin, 20, 10}});
    for (std::size_t i : {1u, 2u, 3u}) f.who(i).hp_max = 30;
    f.who(1).hp = 1;
    f.who(2).hp = 2;
    f.who(2).creature_type = rules5e::CreatureType::Undead;
    f.who(3).hp = 3;
    const auto& spec = preserve_spec(*f.c, f.id[0]);
    std::vector<HealSplitCheck> seen;
    const HealSplitValidator spy = [&](const std::vector<HealShare>& s) {
        seen.push_back(f.c->heal_split_check(f.id[0], spec, s));
        return seen.back();
    };
    const auto split = plan_heal_split(*f.c, f.id[0], spec, spy);
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].reason == HealSplitReason::OutOfRange);
    CHECK(seen[0].index == 0);
    CHECK(seen[1].reason == HealSplitReason::ExcludedType);
    CHECK(seen[2].index == -1);
    REQUIRE(split.size() == 1);
    CHECK(split[0].target == f.id[3]);
    CHECK(split[0].amount == 10);
    CHECK(flat(plan_heal_split(*f.c, f.id[0], spec)) == flat(split));   // the default validator gives the same split
    f.advance_to(0);
    const Command u = next_command(*f.c, f.id[4]);
    REQUIRE(u.request.kind == ActionKind::UseFeature);
    REQUIRE(f.submit(u).ok);
    CHECK(f.who(3).hp == 13);
}

TEST_CASE("AI Preserve Life: OverCap / OverPool / BadTarget rejections adjust or drop deterministically") {
    Fight f({{kCleric, 10, 10}, {kFighter, 11, 10}, {kFighter, 12, 10}, {kGoblin, 20, 20}});
    for (std::size_t i : {1u, 2u}) f.who(i).hp_max = 20;
    f.who(1).hp = 2;   // room 8
    f.who(2).hp = 4;   // room 6
    const auto& spec = preserve_spec(*f.c, f.id[0]);
    const CombatantId a = f.id[1], b = f.id[2];
    SUBCASE("OverCap lowers that ally's cap below the refused amount; the rest flows on") {
        int calls = 0;
        const auto split = plan_heal_split(*f.c, f.id[0], spec, [&](const std::vector<HealShare>& s) {
            ++calls;
            for (std::size_t i = 0; i < s.size(); ++i) if (s[i].target == a && s[i].amount > 5) return HealSplitCheck{int(i), HealSplitReason::OverCap};
            return HealSplitCheck{};
        });
        CHECK(calls == 4);   // 8 -> 7 -> 6 -> 5
        CHECK(flat(split) == Flat{{a, 5}, {b, 5}});
    }
    SUBCASE("OverPool lowers the budget below the running total") {
        const auto split = plan_heal_split(*f.c, f.id[0], spec, [&](const std::vector<HealShare>& s) {
            int t = 0;
            for (std::size_t i = 0; i < s.size(); ++i) { t += s[i].amount; if (t > 9) return HealSplitCheck{int(i), HealSplitReason::OverPool}; }
            return HealSplitCheck{};
        });
        CHECK(flat(split) == Flat{{a, 8}, {b, 1}});
    }
    SUBCASE("BadTarget drops the entry") {
        const auto split = plan_heal_split(*f.c, f.id[0], spec, [&](const std::vector<HealShare>& s) {
            for (std::size_t i = 0; i < s.size(); ++i) if (s[i].target == a) return HealSplitCheck{int(i), HealSplitReason::BadTarget};
            return HealSplitCheck{};
        });
        CHECK(flat(split) == Flat{{b, 6}});
    }
    SUBCASE("a validator that rejects everything ends with no split") {
        const auto split = plan_heal_split(*f.c, f.id[0], spec, [](const std::vector<HealShare>&) {
            return HealSplitCheck{0, HealSplitReason::OverCap};
        });
        CHECK(split.empty());
    }
    SUBCASE("nobody below half: empty, and the AI does not use the feature") {
        f.who(1).hp = 20;
        f.who(2).hp = 20;
        CHECK(plan_heal_split(*f.c, f.id[0], spec).empty());
        f.advance_to(0);
        CHECK(next_command(*f.c, f.id[3]).request.kind != ActionKind::UseFeature);
    }
}

// ---------------------------------------------------------------- 7.2zc mark_ability / move_mark

TEST_CASE("AI mark: mark_ability is the target's weakest ability, ties to the earlier one; Str by default") {
    Fight f({{kMarker, 10, 10}, {kGoblin, 12, 10}, {kGoblin, 13, 10}});
    const auto* chosen = f.c->spell_def(rules5e::spell_id_of(kChosenMark));
    REQUIRE(chosen);
    CHECK(spell_needs_mark_ability(*chosen));
    CHECK_FALSE(spell_needs_mark_ability(*f.c->spell_def(rules5e::spell_id_of(kMarkedDamage))));
    CHECK(choose_mark_ability(*f.c, f.id[1]) == 1);   // goblin: Str 8 = Wis 8 = Cha 8, Str first
    f.who(2).abilities.score[3] = 3;                   // Int 3
    CHECK(choose_mark_ability(*f.c, f.id[2]) == 4);
    CHECK(choose_mark_ability(*f.c, 0) == 1);          // no target: the fixed default
    f.advance_to(0);
    Command cast;
    cast.request = {f.id[0], ActionKind::CastSpell, f.id[2]};
    cast.request.spell_id = rules5e::spell_id_of(kChosenMark);
    cast.request.spell_level = 1;
    cast.request.targets = {f.id[2]};
    complete_request(*f.c, cast.request);
    CHECK(cast.request.mark_ability == 4);
    const auto r = f.submit(cast);
    REQUIRE_MESSAGE(r.ok, r.error);
    bool row = false;
    for (const auto& m : f.c->state().modifiers)
        row = row || (m.spec.chosen_ability && m.spec.has_ability && m.spec.ability == rules5e::Ability::Int && m.target == f.id[2]);
    CHECK(row);
    // A spell without a chosen row gets no mark_ability (rules would refuse one).
    Command hm;
    hm.request = {f.id[0], ActionKind::CastSpell, f.id[1]};
    hm.request.spell_id = rules5e::spell_id_of(kMarkedDamage);
    complete_request(*f.c, hm.request);
    CHECK(hm.request.mark_ability == 0);
}

TEST_CASE("AI mark: move_mark is offered once the marked creature drops to 0 hp, as a bonus action") {
    Fight f({{kMarker, 10, 10}, {kGoblin, 12, 10}, {kGoblin, 14, 10}, {kGoblin, 13, 12}});
    f.advance_to(0);
    Command cast;
    cast.request = {f.id[0], ActionKind::CastSpell, f.id[1]};
    cast.request.spell_id = rules5e::spell_id_of(kChosenMark);
    cast.request.spell_level = 1;
    cast.request.targets = {f.id[1]};
    complete_request(*f.c, cast.request);
    REQUIRE(f.submit(cast).ok);
    CHECK_FALSE(move_mark_command(*f.c, f.id[0]));   // the mark is alive (and the bonus action is spent)
    Command end;
    end.request = {f.id[0], ActionKind::EndTurn};
    REQUIRE(f.submit(end).ok);
    f.advance_to(0);
    CHECK_FALSE(move_mark_command(*f.c, f.id[0]));   // still alive
    f.who(1).hp = 0;
    f.who(1).dead = true;
    // Preferred target (the policy's) wins when it is a conscious hostile; else the nearest, ties to the lowest id.
    auto mm = move_mark_command(*f.c, f.id[0], f.id[3]);
    REQUIRE(mm);
    CHECK(mm->request.move_mark);
    CHECK(mm->request.target == f.id[3]);
    mm = move_mark_command(*f.c, f.id[0], f.id[1]);   // the dead old mark is never the new one
    REQUIRE(mm);
    CHECK(mm->request.target == std::min(f.id[2], f.id[3]));   // both 2 tiles away
    const Command nc = next_command(*f.c, f.id[2]);   // the driver offers it first
    REQUIRE(nc.request.kind == ActionKind::CastSpell);
    CHECK(nc.request.move_mark);
    CHECK(nc.request.target == f.id[2]);
    CHECK(nc.request.mark_ability == 0);   // a move is not a cast
    const auto r = f.submit(nc);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK_FALSE(f.c->state().budget.bonus_action);
    for (const auto& m : f.c->state().modifiers) if (m.spec.mark_scope != 0) CHECK(m.mark == f.id[2]);
    CHECK_FALSE(move_mark_command(*f.c, f.id[0]));   // the new mark is alive and the bonus action is gone
}
