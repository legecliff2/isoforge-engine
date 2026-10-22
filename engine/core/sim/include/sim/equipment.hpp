#pragma once
// docs/proposals/EQUIPMENT.md 9 (Engine Core): the item definition table (item@N -> rules5e::ItemDef), loadout building and
// derived-stat recomputation. Integer math only; std::map storage (stable iteration). Used by Combat only when
// CombatOptions::equipment is on.
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/schema_registry.hpp"
#include "rules5e/encounter.hpp"
#include "rules5e/item.hpp"
#include "rules5e/loadout.hpp"

namespace eng::sim {

// Def id of an item content id ("srd51.item.dagger"): FNV-1a 32 of the id bytes; never 0 (0 maps to 1). Collisions are a load
// error (ItemDefTable::add).
uint32_t item_def_id(std::string_view content_id);

// HeldItem::flags bits 4..6 (sim-owned, like kHeldRelight): the light state index (0 = the top-level radii, k = item@2
// light.states[k - 1], e.g. a hooded lantern's "hooded"). Non-zero only after a state change; folded with the flags (0xD0 /
// 0xD2), so the default state hashes as before.
inline constexpr uint8_t kHeldStateShift = 4, kHeldStateMask = 0x70;
constexpr uint8_t held_light_state(uint8_t flags) { return uint8_t((flags & kHeldStateMask) >> kHeldStateShift); }

// item@N definitions keyed by def id. Owns the strings WeaponDef::id views into (not copyable: share it by pointer).
class ItemDefTable {
public:
    ItemDefTable() = default;
    ItemDefTable(const ItemDefTable&) = delete;
    ItemDefTable& operator=(const ItemDefTable&) = delete;
    // One already-migrated item@1 document (weapon / armor / shield / valuable). Error message or nullopt.
    // item@2 `gear` with a `light` block becomes ItemKind::Light (LightDef: radii, shape, duration, lit_by_default,
    // extra_states = states.size()); `torch_attack` adds the improvised weapon profile (requires_lit). Other gear: Other.
    std::optional<std::string> add(const assets::Json& doc);
    // A definition built in code (tests; item@2 light / ammunition kinds before their loader). def.id is set from content_id;
    // def.weapon.id is re-pointed at owned storage (the attack id is the content id's last segment).
    std::optional<std::string> add(const std::string& content_id, rules5e::ItemDef def);
    const rules5e::ItemDef* find(uint32_t def) const;
    const rules5e::ItemDef* find(std::string_view content_id) const { return find(item_def_id(content_id)); }
    const std::string* content_id(uint32_t def) const;
    // Radii of a light item's state (0 = LightDef bright / dim; k = item@2 light.states[k - 1]). nullopt: not a light, or
    // no such state. (rules5e::LightDef carries only the state count, so the per-state radii live here.)
    struct LightRadii { uint16_t bright_ft = 0, dim_ft = 0; };
    std::optional<LightRadii> light_radii(uint32_t def, uint8_t state) const;
    bool empty() const noexcept { return by_id_.empty(); }
    std::size_t size() const noexcept { return by_id_.size(); }

private:
    struct Entry { std::string content_id; rules5e::ItemDef def; std::vector<LightRadii> states{}; };
    std::vector<LightRadii> pending_states_;   // add(doc) -> add(id, def) hand-off
    std::map<uint32_t, Entry> by_id_;
    std::deque<std::string> strings_;
};

// One combatant's starting equipment as the loader read it (character@4 equipment[] entries that name a loaded item).
struct LoadoutSeed {
    uint32_t def = 0;
    bool equipped = false;
    uint16_t count = 1;
};

// Builds `owner`'s loadout (item ids from st.next_item_id, in entry order) and its attack links, then calibrates the setup
// adjustments so recompute_derived reproduces the printed AC / to-hit / damage. Placement: worn armor; equipped shield ->
// off hand (else main); other equipped holdables -> main, then off (a two-handed item needs both); the rest is carried.
// `two_handed_grip`: Archetype bit mask of versatile attacks held in two hands (R-G7). Returns false when no seed resolves.
// Equipped holdables that do not fit the hands go to carried, and `errors` (if given) gets rules5e::hands_overfull_error
// (`<id>: holds N items but has H hands`, the validator's string) with `content_id`.
bool build_loadout(rules5e::EncounterState& st, rules5e::CombatantId owner, const std::vector<LoadoutSeed>& seeds,
                   const ItemDefTable& items, uint32_t two_handed_grip, std::string_view content_id = {},
                   std::vector<std::string>* errors = nullptr);


rules5e::Loadout* find_loadout(rules5e::EncounterState& st, rules5e::CombatantId id);
const rules5e::Loadout* find_loadout(const rules5e::EncounterState& st, rules5e::CombatantId id);

// EQUIPMENT.md 7: Combatant::ac and the to-hit / damage bonus of every linked attack (Combatant::attacks[i] with
// view.attacks[i].def != 0) from the loadout, abilities and proficiency bonus, plus the setup adjustments. AC effects
// (modifiers, floors) stay dynamic in rules and are not baked. No-op for a creature without a loadout.
void recompute_derived(rules5e::EncounterState& st, rules5e::CombatantId id, const ItemDefTable& items);

// What a recompute depends on besides the loadout (EQUIPMENT.md 7.2): ability scores, proficiency bonus, level, and the AC
// effects on the creature. Conditions are deliberately absent. Sim compares it after every step.
struct DeriveInputs {
    std::array<int8_t, 6> abilities{};
    int8_t pb = 0;
    uint8_t caster_level = 0, hit_dice_max = 0;
    uint32_t ac_effects = 0;   // number of ModStat::Ac modifiers targeting it
    friend bool operator==(const DeriveInputs&, const DeriveInputs&) = default;
};
DeriveInputs derive_inputs(const rules5e::EncounterState& st, rules5e::CombatantId id);

// Canonical digest of the loadout state (0xD0 fold body): next_item_id, loadout count, then per loadout (combatant order)
// owner, main {instance, def, count, flags}, off {...}, armor def, carried count + each {instance, def, count, flags},
// attack link count + each {def, use}, ac_adjust, hit / dmg adjust per link.
uint64_t loadout_digest(const rules5e::EncounterState& st);

} // namespace eng::sim
