#include "sim/rest.hpp"

#include <array>
#include <span>

namespace eng::sim {

namespace {
rules5e::Combatant* member(rules5e::EncounterState& roster, rules5e::CombatantId id) {
    for (auto& k : roster.combatants)
        if (k.id == id) return &k;
    return nullptr;
}
}  // namespace

rng::Rng make_rest_rng(uint64_t seed) noexcept {
    const std::array<int64_t, 1> keys{kRestSeedKey};
    return rng::Rng(rng::derive_seed(seed, std::span<const int64_t>(keys)), kRestStream);
}

rules5e::RestTables rest_tables(rules5e::EncounterState& roster) noexcept {
    return rules5e::RestTables{&roster.features, &roster.pools, &roster.action_uses};
}

rules5e::RestResult short_rest(rules5e::EncounterState& roster, rules5e::CombatantId id, rng::Rng& rest_rng,
                               uint8_t hit_dice_to_spend) {
    rules5e::Combatant* k = id != 0 ? member(roster, id) : nullptr;
    if (!k) return {};
    return rules5e::short_rest(*k, rest_rng, hit_dice_to_spend, rest_tables(roster));
}

rules5e::RestResult long_rest(rules5e::EncounterState& roster, rules5e::CombatantId id, bool food_and_drink) {
    rules5e::Combatant* k = id != 0 ? member(roster, id) : nullptr;
    if (!k) return {};
    return rules5e::long_rest(*k, rest_tables(roster), food_and_drink);
}

}  // namespace eng::sim
