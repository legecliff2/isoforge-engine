// VERSION_BUMP_BATCH 7.2w: grab and contain (SRD 5.1 gelatinous cube Engulf, giant toad Swallow, Net; Web is EC #174's
// escape_check + destructible object, reused here by Net). Containment rides the existing escape / object / delayed-damage
// machinery: no new hash tag (damage_when 3 folds under 0xAC with the other damage-carrying effects).
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kEngulf = spell_id_of("test.cube#engulf");
const uint32_t kSwallow = spell_id_of("test.toad#swallow");
const uint32_t kNet = spell_id_of("test.net");

std::vector<SpellDef> defs() {
    SpellDef en; en.id = kEngulf; en.content_id = "test.cube#engulf"; en.level = 0; en.range_ft = 5; en.kind = SpellEffectKind::Save;
    en.save_ability = Ability::Dex; en.fixed_dc = 30; en.on_save = OnSave::None; en.dice = {3, 6, 0}; en.damage_type = DamageType::Acid;
    en.applies_condition = true; en.condition = Condition::Restrained; en.escape_dc = 12; en.targets = 4; en.is_spell = false;
    en.magical = false; en.components = 0; en.contain = 1; en.contain_dice = {6, 6, 0}; en.contain_damage_type = DamageType::Acid;
    en.condition_max_size = int8_t(Size::Large);

    SpellDef sw; sw.id = kSwallow; sw.content_id = "test.toad#swallow"; sw.level = 0; sw.range_ft = 5; sw.kind = SpellEffectKind::SpellAttack;
    sw.melee = true; sw.fixed_attack = 30; sw.dice = {1, 10, 2}; sw.damage_type = DamageType::Piercing; sw.targets = 1;
    sw.is_spell = false; sw.magical = false; sw.components = 0;
    SpellEffect in; in.kind = SpellEffectKind::ApplyCondition; in.applies_condition = true; in.condition = Condition::Restrained;
    in.affected_only = true; in.contain = 2; in.contain_dice = {3, 6, 0}; in.contain_damage_type = DamageType::Acid;
    in.condition_max_size = int8_t(Size::Medium);
    sw.extra_effects.push_back(in);

    SpellDef net; net.id = kNet; net.content_id = "test.net"; net.level = 0; net.range_ft = 5; net.kind = SpellEffectKind::SpellAttack;
    net.melee = false; net.fixed_attack = 30; net.targets = 1; net.is_spell = false; net.magical = false; net.components = 0;
    SpellEffect r; r.kind = SpellEffectKind::ApplyCondition; r.applies_condition = true; r.condition = Condition::Restrained;
    r.affected_only = true; r.escape_dc = 10; r.object_ac = 10; r.object_hp = 5; r.condition_max_size = int8_t(Size::Large);
    r.object_immune = ~(1u << unsigned(DamageType::Slashing));   // SRD: 5 slashing damage to the net
    r.excludes_formless = true;
    net.extra_effects.push_back(r);
    return {en, sw, net};
}

EncounterHooks hooks() {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    for (const auto& d : defs()) REQUIRE(b->add(d));
    h.spells = b;
    return h;
}

Combatant user(Size sz = Size::Large) {
    auto c = wizard(1);
    c.spells_known.insert(c.spells_known.end(), {kEngulf, kSwallow, kNet});
    c.abilities.score[1] = 30; c.size = sz; c.hp = c.hp_max = 80;
    return c;
}
Combatant victim(CombatantId id, Size sz = Size::Medium) {
    auto c = pc(id, 200); c.team = 1; c.size = sz; c.abilities.score[1] = 1;
    return c;
}

EncounterState start(std::vector<Combatant> cs, uint64_t seed = 4) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}

const ActiveSpellEffect* held(const EncounterState& s, CombatantId t) {
    for (const auto& e : s.effects) if (e.target == t && e.condition == Condition::Restrained) return &e;
    return nullptr;
}

// An attack-roll effect on a fresh encounter, retried over seeds until it hits (fixed_attack 30: only a natural 1 misses).
EncounterState hit_with(uint32_t spell, std::vector<Combatant> cs, CombatantId t = 2) {
    for (uint64_t seed = 1;; ++seed) {
        auto s = start(cs, seed);
        auto r = step(s, cast(1, spell, t, 0));
        REQUIRE(r.ok);
        if (held(s, t) || count(r.events, EventKind::AttackResolved) == 0 || seed > 40) return s;
        bool hit = false;
        for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved && (e.c & 1)) hit = true;
        if (hit) return s;
    }
}

}  // namespace

TEST_CASE("Engulf: a failed Dex save: acid, restrained (escape DC 12), contained; acid again at the start of the cube's turn") {
    auto s = start({user(), victim(2), victim(3, Size::Huge)});
    auto r = cast(1, kEngulf, 2, 0); r.targets = {2, 3};
    auto res = step(s, r);
    REQUIRE(res.ok);
    const auto* e = held(s, 2);
    REQUIRE(e);
    CHECK(e->contain == 1); CHECK(e->escape_check); CHECK(e->dc == 12); CHECK(e->damage_when == 3);
    CHECK(container_of(s, 2) == 1);
    CHECK(find(s, 2)->hp < 200);                        // 3d6 on the failed save
    CHECK(held(s, 3) == nullptr);                       // Huge: no save, no effect
    CHECK(find(s, 3)->hp == 200);
    CHECK_FALSE(find(s, 2)->has(Condition::Blinded));   // engulfed: no total cover, not blinded
    const int16_t hp = find(s, 2)->hp;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(find(s, 2)->hp < hp);                         // 6d6 at the start of the cube's turn
    CHECK(find(s, 3)->hp == 200);
}

TEST_CASE("Engulf: escape with an action, a Strength check vs 12 (special 4)") {
    for (uint64_t seed = 1; seed < 30; ++seed) {
        auto s = start({user(), victim(2)}, seed);
        auto r = cast(1, kEngulf, 2, 0);
        REQUIRE(step(s, r).ok);
        REQUIRE(held(s, 2));
        mut(s, 2).abilities.score[0] = 30;   // Str +10: 11+ on the d20 roll... any roll >= 2 escapes
        end_turns_until(s, 2);
        ActionRequest esc{2, ActionKind::Attack}; esc.special = 4;
        REQUIRE(step(s, esc).ok);
        if (held(s, 2) == nullptr) { CHECK(container_of(s, 2) == 0); CHECK_FALSE(find(s, 2)->has(Condition::Restrained)); return; }
    }
    FAIL("never escaped");
}

TEST_CASE("Swallow: blinded and restrained, total cover from outside, acid each toad turn, freed when the toad dies") {
    auto s = hit_with(kSwallow, {user(Size::Large), victim(2), pc(3)});
    const auto* e = held(s, 2);
    REQUIRE(e);
    CHECK(e->contain == 2); CHECK_FALSE(e->escape_check);
    CHECK(find(s, 2)->has(Condition::Blinded));
    CHECK(container_of(s, 2) == 1);
    // total cover from anything outside the toad: an attack by creature 3 is refused; the toad itself is not blocked
    end_turns_until(s, 3);
    ActionRequest atk{3, ActionKind::Attack, 2}; atk.target_within_5ft = true;
    CHECK(step(s, atk).error == "target has total cover");
    ActionRequest esc{2, ActionKind::Attack}; esc.special = 4;
    end_turns_until(s, 2);
    CHECK_FALSE(step(s, esc).ok);   // no escape check: only the toad's death frees it
    const int16_t hp = find(s, 2)->hp;
    end_turns_until(s, 1);
    CHECK(find(s, 2)->hp < hp);
    // the toad dies: freed (not restrained, not blinded, no container) when the next turn starts
    mut(s, 1).hp = 0; mut(s, 1).dead = true;
    (void)step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(find(s, 1)->dead);
    CHECK(container_of(s, 2) == 0);
    CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
    CHECK_FALSE(find(s, 2)->has(Condition::Blinded));
}

TEST_CASE("Swallow: a creature larger than Medium is not swallowed; an already blinded one keeps its own blindness") {
    auto s = hit_with(kSwallow, {user(Size::Large), victim(2, Size::Large)});
    CHECK(held(s, 2) == nullptr);
    auto b = victim(2); add_condition(b, Condition::Blinded);
    auto t = hit_with(kSwallow, {user(Size::Large), b});
    REQUIRE(held(t, 2));
    CHECK_FALSE(held(t, 2)->has_also);   // not its own
}

TEST_CASE("Net: a hit restrains a Large or smaller creature; escape DC 10; 5 slashing damage to the net (AC 10) frees it") {
    auto s = hit_with(kNet, {user(Size::Medium), victim(2)});
    const auto* e = held(s, 2);
    REQUIRE(e);
    CHECK(e->escape_check); CHECK(e->dc == 10); CHECK(e->object_ac == 10); CHECK(e->object_hp == 5); CHECK(e->contain == 0);
    auto huge = hit_with(kNet, {user(Size::Medium), victim(2, Size::Huge)});
    CHECK(held(huge, 2) == nullptr);
    // only slashing damages the net: a bludgeoning weapon does nothing to it
    auto s2 = s;
    end_turns_until(s2, 2);
    mut(s2, 2).attacks = {Attack{"club", 30, {1, 4, 10}, DamageType::Bludgeoning, 5, false, false}};
    ActionRequest cut{2, ActionKind::Attack, 2, 0}; cut.special = 6; cut.target_within_5ft = true;
    REQUIRE(step(s2, cut).ok);
    REQUIRE(held(s2, 2)); CHECK(held(s2, 2)->object_hp == 5);
    // a slashing blade (1d8 + 10): the 5 HP net falls on a hit
    end_turns_until(s, 2);
    mut(s, 2).attacks = {Attack{"blade", 30, {1, 8, 10}, DamageType::Slashing, 5, false, false}};
    for (int i = 0; i < 5 && held(s, 2); ++i) {
        end_turns_until(s, 2);
        REQUIRE(step(s, cut).ok);
        (void)step(s, {2, ActionKind::EndTurn});
    }
    CHECK(held(s, 2) == nullptr);
    CHECK_FALSE(find(s, 2)->has(Condition::Restrained));
}

TEST_CASE("contain: replay of an engulf and the cube's turn-start acid matches") {
    auto make = [] { return start({user(), victim(2)}, 9); };
    auto a = make(), b = make();
    ActionLog log;
    REQUIRE(step_logged(a, cast(1, kEngulf, 2, 0), log).ok);
    REQUIRE(step_logged(a, {1, ActionKind::EndTurn}, log).ok);
    while (current_actor(a) != 1) REQUIRE(step_logged(a, {current_actor(a), ActionKind::EndTurn}, log).ok);
    for (const auto& ls : log) { auto r = replay_step(b, ls); REQUIRE(r.ok); CHECK_FALSE(r.desync); }
    CHECK(a.combatants[1].hp == b.combatants[1].hp);
    CHECK(container_of(b, 2) == 1);
    CHECK(timed_effects_digest(a) == timed_effects_digest(b));
}

// ---- EC review of #189 ----
namespace {
const uint32_t kWorm = spell_id_of("test.worm#swallow");
EncounterHooks worm_hooks() {
    EncounterHooks h = hooks();
    auto b = std::make_shared<SpellBook>(*h.spells);
    SpellDef w = defs()[1]; w.id = kWorm; w.content_id = "test.worm#swallow";
    w.extra_effects[0].regurgitate_dc = 30; w.extra_effects[0].regurgitate_damage = 30;   // DC 30: the save fails
    REQUIRE(b->add(w));
    h.spells = b;
    return h;
}
}  // namespace

TEST_CASE("Swallow: one creature at a time (a second hit doesn't swallow)") {
    auto s = hit_with(kSwallow, {user(Size::Large), victim(2), victim(3)});
    REQUIRE(held(s, 2));
    for (int i = 0; i < 5; ++i) {
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
        end_turns_until(s, 1);
        REQUIRE(step(s, cast(1, kSwallow, 3, 0)).ok);
    }
    CHECK(held(s, 3) == nullptr);
    CHECK(container_of(s, 3) == 0);
}

TEST_CASE("Swallow: released when the toad dies: prone, and a contain_exits marker (kind 1)") {
    auto s = hit_with(kSwallow, {user(Size::Large), victim(2)});
    REQUIRE(held(s, 2));
    mut(s, 1).hp = 0; mut(s, 1).dead = true;
    REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK(container_of(s, 2) == 0);
    CHECK(find(s, 2)->has(Condition::Prone));
    REQUIRE(s.contain_exits.size() == 1);
    CHECK(s.contain_exits[0] == ContainExit{1, 2, 1});
}

TEST_CASE("Engulf: a successful save leaves no effect and marks the push (contain_exits kind 3)") {
    // DC 30 with Dex 30 (+10) and no proficiency: a natural 20 is needed; search seeds for a success
    for (uint64_t seed = 1; seed < 200; ++seed) {
        auto t = start({user(), [] { auto v = victim(2); v.abilities.score[1] = 30; return v; }()}, seed);
        auto r = step(t, cast(1, kEngulf, 2, 0));
        REQUIRE(r.ok);
        if (held(t, 2)) { CHECK(t.contain_exits.empty()); continue; }
        CHECK(container_of(t, 2) == 0);
        REQUIRE(t.contain_exits.size() == 1);
        CHECK(t.contain_exits[0] == ContainExit{1, 2, 3});
        CHECK(find(t, 2)->hp == 200);
        return;
    }
    FAIL("no successful save found");
}

TEST_CASE("Swallow regurgitation: 30+ damage from inside in one turn, a failed Con save: exits prone (kind 2)") {
    for (uint64_t seed = 1;; ++seed) {
        auto s = begin_encounter({[] { auto c = user(Size::Huge); c.spells_known.push_back(kWorm); return c; }(), victim(2)}, seed,
                                 nullptr, worm_hooks());
        end_turns_until(s, 1);
        REQUIRE(step(s, cast(1, kWorm, 2, 0)).ok);
        if (!held(s, 2)) { REQUIRE(seed < 40); continue; }
        REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
        end_turns_until(s, 2);
        mut(s, 2).attacks = {Attack{"blade", 30, {1, 1, 39}, DamageType::Slashing, 5, false, false}};   // 40 a hit
        mut(s, 1).reaction_used = true;   // no Shield prompt
        ActionRequest atk{2, ActionKind::Attack, 1}; atk.target_within_5ft = true;
        auto a = step(s, atk);
        REQUIRE(a.ok);
        REQUIRE(a.status == StepStatus::Done);
        if (s.effects.empty() || held(s, 2)->container_damage < 30) { REQUIRE(seed < 40); continue; }   // missed: try again
        auto e = step(s, {2, ActionKind::EndTurn});
        REQUIRE(e.ok);
        CHECK(container_of(s, 2) == 0);
        CHECK(find(s, 2)->has(Condition::Prone));
        REQUIRE(s.contain_exits.size() == 1);
        CHECK(s.contain_exits[0] == ContainExit{1, 2, 2});
        return;
    }
}

TEST_CASE("Swallow regurgitation: below the threshold, no save; the counter resets at the end of the turn") {
    auto s = begin_encounter({[] { auto c = user(Size::Huge); c.spells_known.push_back(kWorm); return c; }(), victim(2)}, 2,
                             nullptr, worm_hooks());
    end_turns_until(s, 1);
    for (uint64_t i = 0; i < 40 && !held(s, 2); ++i) { (void)step(s, cast(1, kWorm, 2, 0)); if (!held(s, 2)) { (void)step(s, {1, ActionKind::EndTurn}); end_turns_until(s, 1); } }
    REQUIRE(held(s, 2));
    for (auto& e : s.effects) if (e.contain == 2) e.container_damage = 29;
    end_turns_until(s, 2);
    auto e = step(s, {2, ActionKind::EndTurn});
    CHECK(count(e.events, EventKind::SavingThrowRolled) == 0);
    REQUIRE(held(s, 2));
    CHECK(held(s, 2)->container_damage == 0);
}

TEST_CASE("Net: no effect on a formless creature") {
    auto v = victim(2); v.formless = true;
    auto s = hit_with(kNet, {user(Size::Medium), v});
    CHECK(held(s, 2) == nullptr);
}
