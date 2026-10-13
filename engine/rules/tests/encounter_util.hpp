#pragma once
#include "test_util.hpp"

namespace enc {
using namespace rules5e;
inline const Attack kLongsword{"longsword", 5, {1, 8, 3}, DamageType::Slashing, 5, false, false};
inline const Attack kScimitar{"scimitar", 4, {1, 6, 2}, DamageType::Slashing, 5, false, true};

inline Combatant pc(CombatantId id, int hp = 28) {
    auto c = make_combatant(id, 14, 16, hp);
    c.team = 0; c.speed_ft = 30; c.proficiency_bonus = 2;
    c.uses_death_saves = true; c.is_pc = true;
    c.attacks = {kLongsword};
    return c;
}
inline Combatant monster(CombatantId id) {
    auto c = make_combatant(id, 14, 15, 7);
    c.team = 1; c.speed_ft = 30;
    c.bonus_action_grants = bit(ActionKind::Disengage) | bit(ActionKind::Hide);
    c.attacks = {kScimitar};
    return c;
}
inline Combatant& mut(EncounterState& s, CombatantId id) {
    for (auto& c : s.combatants) if (c.id == id) return c;
    return s.combatants.front();
}
inline void end_turns_until(EncounterState& s, CombatantId id) {
    for (int i = 0; i < 20 && current_actor(s) != id; ++i) (void)step(s, {current_actor(s), ActionKind::EndTurn});
}
inline void make_dying(Combatant& c) {
    c.hp = 0; c.death = {};
    add_condition(c, Condition::Unconscious); add_condition(c, Condition::Prone);
}
inline int count(const std::vector<Event>& ev, EventKind k) {
    int n = 0; for (const auto& e : ev) n += e.kind == k; return n;
}
} // namespace enc
