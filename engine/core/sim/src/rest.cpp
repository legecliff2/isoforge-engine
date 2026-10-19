#include "sim/rest.hpp"

#include <array>
#include <span>

#include "sim/content.hpp"

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

std::size_t register_slot_recovery(rules5e::EncounterState& roster, rules5e::CombatantId id, const Archetype& archetype) {
    std::size_t added = 0;
    if (id == 0 || !member(roster, id)) return 0;
    for (const auto& spec : archetype.slot_recovery) {
        bool present = false;
        for (const auto& row : roster.features) present = present || (row.actor == id && row.feature_id == spec.feature_id);
        if (present) continue;
        roster.features.push_back(rules5e::slot_recovery_uses(id, spec));
        ++added;
    }
    return added;
}

rules5e::SlotRecoveryResult apply_slot_recovery(rules5e::EncounterState& roster, rules5e::CombatantId id,
                                                const rules5e::SlotRecoverySpec& spec, const std::array<uint8_t, 10>& add) {
    rules5e::Combatant* k = id != 0 ? member(roster, id) : nullptr;
    if (!k) return rules5e::SlotRecoveryResult::NoFeature;
    return rules5e::apply_slot_recovery(*k, spec, roster.features, add);
}

ShortRestRecovery short_rest_with_recovery(rules5e::EncounterState& roster, rules5e::CombatantId id, rng::Rng& rest_rng,
                                           uint8_t hit_dice_to_spend, const rules5e::SlotRecoverySpec* spec,
                                           const std::array<uint8_t, 10>& add) {
    ShortRestRecovery out;
    out.rest = short_rest(roster, id, rest_rng, hit_dice_to_spend);
    if (out.rest.ok && spec) out.recovery = apply_slot_recovery(roster, id, *spec, add);
    return out;
}

}  // namespace eng::sim
