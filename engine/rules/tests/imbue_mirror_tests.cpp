// 7.2zf: Shillelagh / Magic Weapon (weapon_imbue) and Mirror Image (duplicates): the imbued attack, the refusals, the duplicate
// d20 (drawn only when it applies), destruction on a hit and the unaffected attackers.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kShil = spell_id_of("test.imbue.shillelagh");
const uint32_t kMagicW = spell_id_of("test.imbue.magic_weapon");
const uint32_t kMirror = spell_id_of("test.imbue.mirror_image");

SpellDef base(uint32_t id, const char* cid, uint8_t level) {
    SpellDef d; d.id = id; d.content_id = cid; d.level = level; d.range_ft = 0;
    d.duration = SpellDuration::Rounds; d.expiry = 1;
    return d;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef sh = base(kShil, "test.imbue.shillelagh", 0); sh.casting_time = CastingTime::BonusAction;
    sh.kind = SpellEffectKind::WeaponImbue; sh.duration_amount = 10; sh.duration_rounds = 10;
    sh.imbue.weapons = {"srd51.item.club", "srd51.item.quarterstaff"}; sh.imbue.spellcasting_ability = true; sh.imbue.die = 8;
    sh.imbue.magical = true; sh.imbue.ends_on_release = true;
    book->add(sh);
    SpellDef mw = base(kMagicW, "test.imbue.magic_weapon", 2); mw.casting_time = CastingTime::BonusAction; mw.concentration = true;
    mw.kind = SpellEffectKind::WeaponImbue; mw.duration_amount = 600; mw.duration_rounds = 600;
    mw.imbue.magical = true; mw.imbue.requires_nonmagical = true; mw.imbue.bonus_base = 1; mw.imbue.bonus_at_slot = {{uint8_t{4}, int8_t{2}}, {uint8_t{6}, int8_t{3}}};
    book->add(mw);
    SpellDef mi = base(kMirror, "test.imbue.mirror_image", 2);
    mi.kind = SpellEffectKind::Duplicates; mi.duration_amount = 10; mi.duration_rounds = 10;
    mi.duplicates.count = 3;
    book->add(mi);
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id) {
    auto c = wizard(id, 0);
    c.hp = c.hp_max = 60;
    c.abilities.score[std::size_t(Ability::Str)] = 10;   // Str +0, Int +3, Dex +2 below
    c.abilities.score[std::size_t(Ability::Dex)] = 14;
    c.attacks = {Attack{"club", 2, {1, 4, 0}, DamageType::Bludgeoning, 5, false, false},
                 Attack{"longbow", 4, {1, 8, 2}, DamageType::Piercing, 150, true, false}};
    c.spell_slots_max = {0, 4, 3, 3, 3, 2, 1, 0, 0, 0}; c.spell_slots = c.spell_slots_max;
    c.spells_known.insert(c.spells_known.end(), {kShil, kMagicW, kMirror});
    never_react(c);
    return c;
}
Combatant brute(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 500; m.ac = 1;
    m.attacks = {Attack{"slam", 30, {1, 1, 0}, DamageType::Bludgeoning, 5, false, false}};
    never_react(m);
    return m;
}
EncounterState start(std::vector<Combatant> cs, CombatantId first) {
    auto s = begin_encounter(std::move(cs), 7, nullptr, hooks());
    end_turns_until(s, first);
    return s;
}
ActionRequest self_cast(CombatantId a, uint32_t spell, uint8_t lvl) { ActionRequest r{a, ActionKind::CastSpell, a}; r.spell_id = spell; r.spell_level = lvl; return r; }
void to_turn(EncounterState& s, CombatantId id) { (void)step(s, ActionRequest{current_actor(s), ActionKind::EndTurn}); end_turns_until(s, id); }

} // namespace

TEST_CASE("weapon_imbue: Shillelagh imbues the club (spellcasting ability, d8, magical); recast replaces it") {
    auto s = start({caster(1), brute(2)}, 1);
    REQUIRE(step(s, self_cast(1, kShil, 0)).ok);
    const Attack club = effective_attack(s, 1, 0);
    CHECK(club.to_hit == 2 + 3);                       // Str +0 -> Int +3
    CHECK(club.damage == DiceExpr{1, 8, 3});
    CHECK((club.qualifiers & kAttackMagical) != 0);
    CHECK(effective_attack(s, 1, 1).to_hit == 4);       // the longbow is not imbued
    ActionRequest atk{1, ActionKind::Attack, 2}; atk.attack_index = 0;
    auto r = step(s, atk);
    REQUIRE(r.ok);
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) CHECK(e.b - e.a == 5);   // total - natural = to-hit
    to_turn(s, 1);
    REQUIRE(step(s, self_cast(1, kShil, 0)).ok);
    int n = 0; for (const auto& e : s.effects) n += e.target == 1 && e.spell_id == kShil;
    CHECK(n == 1);
    auto noclub = caster(3); noclub.attacks = {Attack{"dagger", 2, {1, 4, 0}, DamageType::Piercing, 5, false, true}};
    auto s2 = start({noclub, brute(2)}, 3);
    CHECK(step(s2, self_cast(3, kShil, 0)).error == "weapon_imbue: no weapon to imbue");
}

TEST_CASE("weapon_imbue: Magic Weapon's bonus by slot; an already magical weapon is refused") {
    WeaponImbueSpec w; w.bonus_base = 1; w.bonus_at_slot = {{uint8_t{4}, int8_t{2}}, {uint8_t{6}, int8_t{3}}};
    CHECK(imbue_bonus(w, 2) == 1); CHECK(imbue_bonus(w, 3) == 1); CHECK(imbue_bonus(w, 4) == 2);
    CHECK(imbue_bonus(w, 5) == 2); CHECK(imbue_bonus(w, 6) == 3); CHECK(imbue_bonus(w, 9) == 3);
    CHECK(imbue_bonus(WeaponImbueSpec{}, 6) == 0);
    auto s = start({caster(1), brute(2)}, 1);
    REQUIRE(step(s, self_cast(1, kMagicW, 4)).ok);
    const Attack club = effective_attack(s, 1, 0);
    CHECK(club.to_hit == 4); CHECK(club.damage == DiceExpr{1, 4, 2}); CHECK((club.qualifiers & kAttackMagical) != 0);
    auto magic = caster(3); magic.attacks[0].qualifiers = kAttackMagical;
    auto s2 = start({magic, brute(2)}, 3);
    CHECK(step(s2, self_cast(3, kMagicW, 2)).error == "magic_weapon: the weapon is already magical");
    ActionRequest other{3, ActionKind::CastSpell, 2}; other.spell_id = kMagicW; other.spell_level = 2;
    CHECK(step(s2, other).error == "weapon_imbue: self only");
}

TEST_CASE("duplicates: thresholds by remaining count, a hit destroys one with no damage, the effect ends at 0") {
    auto s = start({caster(1), brute(2)}, 1);
    REQUIRE(step(s, self_cast(1, kMirror, 2)).ok);
    CHECK(duplicates_left(s, 1) == 3);
    ActionRequest other{1, ActionKind::CastSpell, 2}; other.spell_id = kMirror; other.spell_level = 2;
    int rolls = 0, redirects = 0;
    for (int round = 0; round < 80 && duplicates_left(s, 1) > 0; ++round) {
        to_turn(s, 2);
        const int before = duplicates_left(s, 1);
        const int hp = mut(s, 1).hp;
        ActionRequest atk{2, ActionKind::Attack, 1}; atk.attack_index = 0;
        auto r = step(s, atk);
        REQUIRE(r.ok);
        bool redirected = false;
        for (const auto& e : r.events) {
            if (e.kind == EventKind::FeatureTriggered && e.a == int32_t(kMirror)) {
                ++rolls;
                const int need = before == 3 ? 6 : before == 2 ? 8 : 11;
                CHECK(e.c == int32_t(e.b >= need));
                redirected = e.c == 1;
            }
        }
        if (redirected) {
            ++redirects;
            CHECK(mut(s, 1).hp == hp);   // a duplicate takes the attack: no damage to the caster
            for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) {
                CHECK((e.c & 4) != 0);
                if (e.c & 1) CHECK(duplicates_left(s, 1) == before - 1);
            }
        }
        mut(s, 1).hp = mut(s, 1).hp_max;
    }
    CHECK(rolls > 0); CHECK(redirects > 0);
    CHECK(duplicates_left(s, 1) == 0);
    for (const auto& e : s.effects) CHECK(e.spell_id != kMirror);
    auto s2 = start({caster(1), brute(2)}, 1);
    CHECK(step(s2, other).error == "duplicates: self only");
}

TEST_CASE("duplicates: blinded, blindsight and truesight attackers are unaffected; no d20 without duplicates") {
    for (int k = 0; k < 3; ++k) {
        auto m = brute(2);
        if (k == 1) m.blindsight_ft = 30;
        if (k == 2) m.truesight_ft = 60;
        auto s = start({caster(1), m}, 1);
        REQUIRE(step(s, self_cast(1, kMirror, 2)).ok);
        to_turn(s, 2);
        if (k == 0) mut(s, 2).conditions |= bit(Condition::Blinded);
        ActionRequest atk{2, ActionKind::Attack, 1}; atk.attack_index = 0;
        auto r = step(s, atk);
        REQUIRE(r.ok);
        for (const auto& e : r.events) CHECK_FALSE((e.kind == EventKind::FeatureTriggered && e.a == int32_t(kMirror)));
        CHECK(duplicates_left(s, 1) == 3);
    }
    // no duplicates: no extra d20 (the attack's own roll is the first draw, as before)
    auto a = start({caster(1), brute(2)}, 2);
    ActionRequest atk{2, ActionKind::Attack, 1}; atk.attack_index = 0;
    for (const auto& e : step(a, atk).events) CHECK(e.kind != EventKind::FeatureTriggered);
}
