#pragma once
#include "rng/rng.hpp"
#include "rules5e/abilities.hpp"
#include "rules5e/d20.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

struct CheckResult { D20Roll roll; bool success; };

// Ability modifier + proficiency bonus if proficient in the skill.
constexpr int skill_modifier(const Combatant& c, Skill k) {
    return modifier(c.abilities, skill_ability(k)) +
           ((c.skill_proficiencies & bit(k)) ? c.proficiency_bonus : (c.half_proficiency & 1) ? c.proficiency_bonus / 2 : 0);
}

// R-G16: passive score = 10 + skill modifier + `bonus` (the caller passes +5 for advantage, -5 for disadvantage, SRD 5.1).
constexpr int passive_score(const Combatant& c, Skill k, int bonus = 0) { return 10 + skill_modifier(c, k) + bonus; }
// Tier-A §25: a printed override (Combatant::passive_perception_override >= 0) replaces 10 + Perception, shifted by the
// effective-Wis modifier delta (current Wis vs Wis + ability_reduction), so a Wis reduction moves both the same way.
constexpr int passive_perception(const Combatant& c, int bonus = 0) {
    if (c.passive_perception_override < 0) return passive_score(c, Skill::Perception, bonus);
    const int wis = c.abilities[Ability::Wis];
    const int delta = modifier(wis) - modifier(wis + int(c.ability_reduction[std::size_t(Ability::Wis)]));
    return int(c.passive_perception_override) + delta + bonus;
}

// Skill check vs DC (success if total >= DC). Mode merged with check_roll_mode
// (Poisoned/Frightened/exhaustion). Natural 20/1 have no special meaning (SRD 5.1).
CheckResult skill_check(rng::Rng&, const Combatant&, Skill, int dc, RollMode = RollMode::Normal);

// Keen Senses (CONTENT_CONVENTIONS §30). `keen` is Combatant::keen_senses. `relies_on` is the senses this Wisdom
// (Perception) check relies on (the same bits). `sight_penalty` is 0 or -5 from passive_sight_penalty, and matters only
// when the check relies on sight. `sight_works` is true when the viewer perceives the subject with Sense::Sight: Keen
// Sight's advantage applies only then. Advantage and a sight penalty on that same sight check cancel. A keen hearing or
// smell path is +5 with no light penalty, and the better path wins (never a sum, never +10). keen == 0 leaves the sight
// penalty unchanged and adds nothing.
struct KeenPerception {
    int passive_bonus = 0;                 // -5, 0, or +5
    RollMode mode = RollMode::Normal;
};
constexpr KeenPerception keen_perception(uint8_t keen, uint8_t relies_on, int sight_penalty, bool sight_works) {
    constexpr uint8_t kNonSight = uint8_t(kKeenHearing | kKeenSmell);
    const bool want_sight = (relies_on & kKeenSight) != 0;
    const bool keen_nonsight = (keen & relies_on & kNonSight) != 0;
    const bool keen_sight = want_sight && sight_works && (keen & kKeenSight) != 0;
    const bool disadvantaged = want_sight && sight_penalty < 0;
    int sight_path = 0;
    if (want_sight) {
        if (keen_sight && disadvantaged) sight_path = 0;
        else if (keen_sight) sight_path = 5;
        else sight_path = sight_penalty < 0 ? sight_penalty : 0;
    }
    if (keen_nonsight && (!want_sight || 5 >= sight_path)) return KeenPerception{5, RollMode::Advantage};
    if (!want_sight) return {};
    if (sight_path >= 5) return KeenPerception{5, RollMode::Advantage};
    if (sight_path <= -5) return KeenPerception{sight_path, RollMode::Disadvantage};
    return {};
}
// Hide, once the viewer cannot see the subject (so Keen Sight does not apply). hearing_ok: audible and not Deafened
// (Sight::perceives on that path). Smell is always offered (no range model). A creature with no keen senses gets
// sight_penalty and nothing else.
constexpr int unseen_perception_bonus(uint8_t keen, int sight_penalty, bool hearing_ok) {
    uint8_t relies = uint8_t(kKeenSight | kKeenSmell);
    if (hearing_ok) relies = uint8_t(relies | kKeenHearing);
    return keen_perception(keen, relies, sight_penalty, false).passive_bonus;
}
// Surprise has no per-pair light or audible fact. Keen Hearing (unless Deafened) and Keen Smell add +5, not stacked.
// Keen Sight does not.
constexpr int keen_surprise_bonus(uint8_t keen, bool deafened) {
    uint8_t relies = kKeenSmell;
    if (!deafened) relies = uint8_t(relies | kKeenHearing);
    return keen_perception(keen, relies, 0, false).passive_bonus;
}

} // namespace rules5e
