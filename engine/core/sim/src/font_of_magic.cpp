#include "sim/font_of_magic.hpp"

namespace eng::sim {

rules5e::FlexibleOutcome flexible_casting(rules5e::EncounterState& roster, rules5e::CombatantId id, uint8_t flexible,
                                          uint8_t level) {
    if (id != 0)
        for (auto& k : roster.combatants)
            if (k.id == id) return rules5e::flexible_casting(k, roster.pools, flexible, level);
    rules5e::FlexibleOutcome none;
    none.result = rules5e::FlexibleResult::NoPool;
    return none;
}

}  // namespace eng::sim
