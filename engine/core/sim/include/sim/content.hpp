#pragma once
// Content -> rules adapters: monster@N / character@N stat blocks -> rules5e::Combatant.
#include <deque>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "assets/pack_set.hpp"
#include "assets/schema_registry.hpp"
#include "sim/equipment.hpp"
#include "sim/features.hpp"
#include "rules5e/spells.hpp"
#include "rules5e/encounter.hpp"
#include "rules5e/rest.hpp"
#include "rules5e/types.hpp"

namespace eng::sim {

// SRD creature size category, from the stat block's "size".
enum class CreatureSize : uint8_t { Tiny, Small, Medium, Large, Huge, Gargantuan };
// Side of the square space a creature occupies, in tiles (5 ft each): Tiny/Small/Medium 1, Large 2,
// Huge 3, Gargantuan 4. Tiny/Small share a tile with nobody (SRD sharing rules are out of scope).
constexpr uint8_t footprint_of(CreatureSize s) noexcept {
    return s == CreatureSize::Large ? 2 : s == CreatureSize::Huge ? 3 : s == CreatureSize::Gargantuan ? 4 : 1;
}

// character@4 equipment[] entry as the sim's priced-material check sees it (CONTENT_CONVENTIONS §7): id read like
// equipment_entry_id, value_gp default 0. Every entry is kept, in array order.
struct EquipmentEntry {
    std::string id;
    uint32_t value_gp = 0;
    bool equipped = false;
    uint16_t count = 1;      // appended (EQUIPMENT.md Q10): {"item": ..., "count": N} stack size (ammunition); default 1   // appended (EQUIPMENT.md 9): {"item": ..., "equipped": true}; read only by CombatOptions::equipment
};

struct Archetype {
    std::string id;                       // "srd51.monster.goblin"
    std::string pack_id;                  // owning pack ("" when added directly via add())
    std::string schema_type;              // "monster" | "character"
    rules5e::Combatant base{};            // id/team filled per spawn
    // Per base.attacks[i]: ranged normal / long range in feet (0 for melee). rules5e::Attack keeps
    // only a uint8_t reach_or_range_ft, so sim owns the full range data.
    std::vector<uint16_t> normal_range_ft, long_range_ft;
    std::vector<FeatureDecl> features;    // declared (features[] then traits[]); feature_id = index + 1
    std::vector<ActiveFeature> active;    // usable via UseFeature (from active hooks)
    std::vector<std::string> inert_features;         // "<id> (<kind>)" pre-character@3 effect, not applied
    std::vector<std::string> unsupported_features;   // "<id> (<kind>)" with no registered hook
    // Pre-@3 descriptive defenses / extra damage entries carried by the file (migration marker
    // "inert_mechanics"): listed, never applied (docs/CONTENT_COMPAT.md).
    std::vector<std::string> inert_mechanics;
    // monster@4 / character@4 `save_effect` actions (VERSION_BUMP_BATCH 2.3): each compiles to a synthetic rules5e
    // SpellDef (id = spell_id_of("<archetype id>#<action id>"), is_spell = false) registered in the library's
    // SpellBook and appended to Combatant::spells_known. Combat::create registers the usage rows.
    struct SaveAction {
        std::string id;                 // content action id
        uint32_t spell_id = 0;
        uint8_t usage_kind = 0;         // SpellDef::usage_kind: 0 at-will, 1 recharge, 2 per-day / per-rest, 3 pool
        uint32_t pool_id = 0;           // usage_kind 3: spell_id_of(resources[].id)
        uint16_t pool_max = 0, pool_start = 0;
        uint8_t pool_cost = 0;
        Recharge pool_recharge = Recharge::None;
        uint8_t uses = 0;               // usage_kind 2
        Recharge recharge = Recharge::None;   // usage_kind 2
        bool area = false;              // has an area template (the AI never picks these before the area-template row)
        uint16_t range_ft = 0;
    };
    std::vector<SaveAction> save_actions;
    // resources[] (row 13): pools Combat::create registers (resolved against the archetype's level / abilities at load).
    // `unlimited` table entries register no pool. legendary_resistance names the pool base.legendary_pool points at.
    struct Resource { std::string id; uint32_t pool_id = 0; uint16_t max = 0, start = 0; uint8_t recharge = 0; };   // recharge: FeatureRecharge
    std::vector<Resource> resources;
    // slot_recovery features (Arcane Recovery / Natural Recovery, VERSION_BUMP_BATCH 7.2n hand-off 1): one spec per
    // declaration (feature_id = 1-based index in features). Never registered by Combat::create (rests stay outside Combat
    // and its hash); sim::register_slot_recovery puts the FeatureUses rows into a kept roster.
    std::vector<rules5e::SlotRecoverySpec> slot_recovery;
    std::vector<std::string> unlimited_resources;
    // UseFeature specs (row 13: usage.pool / cost, grant_action, heal_pool), by feature_id; hooks.feature_spec reads them.
    std::map<uint32_t, rules5e::FeatureSpec> feature_specs;
    // apply_modifier features (row 12): the modifier rows hooks.feature_modifiers hands to rules, by feature_id (1-based index in features)
    std::map<uint32_t, std::vector<rules5e::ModifierSpec>> feature_modifiers;
    // on_hit[] kind "grapple" (R-G8): attack index -> the rider (escape DC; Druid gap G appends restrains / exclusive /
    // max_grapples); a hit by that attack grapples the target (sim calls rules5e::grapple).
    std::map<uint8_t, rules5e::GrappleRider> grapple_on_hit;
    // R-G7 v0 wield model: bit i set = base.attacks[i] is a versatile melee weapon held in two hands (the stat block carries no
    // shield). The AI sets ActionRequest::two_handed_grip for such attacks unless the creature has a two-weapon bonus option.
    uint32_t two_handed_grip = 0;
    std::vector<uint32_t> trigger_spell_ids;   // synthetic Save SpellDefs of area_save riders and gaze features (SpellBook only, not spells_known)
    std::vector<std::string> forward_compat;         // monster@4 / character@4 "forward_compat" flags: listed, ignored
    CreatureSize size = CreatureSize::Medium;        // stat block "size"
    // Side of the footprint in tiles: footprint_of(size) for monster@3 / character@3, always 1 for
    // pre-@3 content (its size was descriptive; migration marker "size" in inert_mechanics).
    uint8_t footprint = 1;
    // EC swarm: a monster tagged "swarm" (SRD "Swarm" trait): shares spaces both ways (Combat::occupied_except) and cannot regain
    // HP or gain temp HP (Combatant::no_regain_hp). Squeezing through Tiny openings needs no grid change (no sub-tile openings).
    bool swarm = false;
    std::vector<std::string> spell_ids;   // spellcasting.spells content ids (monster@3 / character@3)
    std::vector<std::string> innate_spell_ids;   // spellcasting.innate.* content ids (monster@4 / character@4); base.innate holds the rows
    // `multiattack` effect options as authored: option id + indices into base.attacks. ContentLibrary::add
    // turns them into base.multiattacks (ids stored in the library). Declared order = option order.
    struct MultiattackDecl { std::string id; std::vector<uint8_t> attacks; };
    std::vector<MultiattackDecl> multiattack_decls;
    std::optional<std::string> ai;        // behavior.ai (monster@2 / character@2), pack-relative
    // Script id of behavior.ai, resolved within the archetype's own pack: "pack_id:path".
    std::optional<std::string> ai_script_id() const {
        if (!ai) return std::nullopt;
        return assets::make_script_key(pack_id, *ai);
    }
    // R-G12 transforms (VERSION_BUMP_BATCH 7.2q hand-off, sim side). Stat-block facts build_form checks: challenge.rating
    // times 8 (1/8 -> 1, 1/4 -> 2, 1/2 -> 4, 1 -> 8; -1 = no rating, e.g. characters), speed.fly / speed.swim in feet, and
    // the character level (sum of classes[] levels, else `level`; 0 for monsters).
    int16_t cr_x8 = -1;
    uint16_t fly_ft = 0, swim_ft = 0;
    uint8_t char_level = 0;
    // Transform features (effect kinds wild_shape / shapechanger), by feature_id. Form choice i (ActionRequest::form_archetype
    // = i + 1) is forms[i] (Wild Shape: a beast archetype id) or shapes[i] (shapechanger: a form of the creature's own block).
    struct FormFeature {
        uint8_t kind = 0;                       // rules5e::FormKind: 2 Wild Shape, 3 shapechanger
        std::vector<std::string> forms;         // Wild Shape: beast archetype ids, in content order
        int16_t max_cr_x8 = 0;                  // Wild Shape: the druid level's max CR (x 8)
        uint8_t level = 0;                      // Wild Shape: druid level
        uint8_t no_fly_below = 8, no_swim_below = 4;   // Wild Shape: no fly / swim speed below these druid levels
        struct Shape {
            std::string id;
            std::optional<CreatureSize> size;
            int16_t ac = -1;
            int16_t speed_ft = -1;                          // speed.walk; -1 = the creature's own
            std::optional<rules5e::CreatureType> creature_type;
            std::vector<std::string> blocked_actions;       // own actions[] ids not in the form's actions[] (attacks dropped)
        };
        std::vector<Shape> shapes;              // shapechanger forms (statistics other than these are the creature's own)
    };
    std::map<uint32_t, FormFeature> form_features;
    // Feature ids whose effect sets requires_humanoid_form (Wild Shape blocks them in a beast form, D7).
    std::vector<uint32_t> humanoid_only_features;
    std::vector<EquipmentEntry> equipment;   // equipment[] in array order (priced materials; Combat copies it per combatant)
};

// R-G12 summons (VERSION_BUMP_BATCH 7.2r, SUMMONS_TRANSFORMS.md D1): which creatures a Summon spell may bring.
// ActionRequest::summon_archetypes are indices into `archetypes` (content ids). Table:
//  * Any: any 1..SummonSpec::max_count of the listed creatures (Find Familiar, Animate Dead);
//  * CrBand (the conjure family, SRD Conjure Animals / Woodland Beings / Minor Elementals): 1 creature of CR 2 or lower, 2 of
//    CR 1 or lower, 4 of CR 1/2 or lower or 8 of CR 1/4 or lower; times 2 / 3 / 4 at slot level 5 / 7 / 9. Every chosen
//    creature must fit the band of the count (they may differ).
// max_count (Director ruling on #175): absent = no cap, so the SRD multipliers apply in full (sim writes
// SummonSpec::max_count = 255, above the largest band count 32); present = written to SummonSpec::max_count. When a present
// cap is below a band count the table allows (CrBand: any count x multiplier at slot 1..9), add_summon_spell records one
// deterministic line per clipped (count, slot level) in ContentLibrary::warnings(). Warnings are diagnostics only: never
// hashed, never logged into the encounter (the cap itself reaches rules through the SpellDef, as before).
struct SummonOptions {
    enum class Table : uint8_t { Any = 0, CrBand = 1 };
    std::vector<std::string> archetypes;
    Table table = Table::Any;
    std::optional<uint8_t> max_count;
};

// Owns archetypes and the strings rules5e::Attack::id views into (stable addresses).
class ContentLibrary {
public:
    ContentLibrary() = default;
    explicit ContentLibrary(const FeatureRegistry& features) : features_(&features) {}
    // Adds every monster@N / character@N document of an ordered pack set (pack order, then
    // manifest order). A set with errors is rejected (its errors are returned). Returns error
    // messages (empty = ok).
    std::vector<std::string> load(const assets::PackSet& packs);
    // Adapter: PackSet::load({pack_dir}) + load(). Prefer building the PackSet once.
    std::vector<std::string> load_pack(const std::string& pack_dir);
    // Adds one already-migrated stat block document (owned by pack_id, may be empty).
    std::optional<std::string> add(const assets::Json& doc, const std::string& schema_type, const std::string& pack_id = "",
                                    uint32_t declared_version = 0);

    // Spell definitions of every spell@1 document loaded by load() (pack order). Shared with Combat,
    // which installs it as rules5e hooks.spells. Empty when no pack has spells.
    const std::shared_ptr<rules5e::SpellBook>& spells() const noexcept { return spells_; }
    // Adds one already-migrated spell@1 document. Returns an error message or nullopt.
    std::optional<std::string> add_spell(const assets::Json& doc);
    // item@1 definitions of every item document loaded by load() (EQUIPMENT.md 9, Engine Core). Read by Combat only when
    // CombatOptions::equipment is on. add_item: one already-migrated item@1 document.
    const std::shared_ptr<ItemDefTable>& items() const noexcept { return items_; }
    std::optional<std::string> add_item(const assets::Json& doc) { return items_->add(doc, &warnings_); }

    const Archetype* find(const std::string& id) const;
    const std::map<std::string, Archetype>& all() const noexcept { return by_id_; }
    // R-G12 summons: a Summon SpellDef (kind Summon) and its creature options. Until spell@1 has a `summon` arm (T&C) this is
    // the way in; the def joins spells(). Errors: not a Summon def, empty or unknown archetypes, duplicate id, max_count 0.
    std::optional<std::string> add_summon_spell(const rules5e::SpellDef& def, SummonOptions options);
    const SummonOptions* summon_options(uint32_t spell_id) const;
    // Deterministic load-time diagnostics (registration order; e.g. a summon max_count clipping a CR band). Not hashed.
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::map<uint32_t, SummonOptions> summons_;
    std::vector<std::string> warnings_;
    std::map<std::string, Archetype> by_id_;
    std::deque<std::string> strings_;
    std::shared_ptr<rules5e::SpellBook> spells_ = std::make_shared<rules5e::SpellBook>();
    std::shared_ptr<ItemDefTable> items_ = std::make_shared<ItemDefTable>();
    const FeatureRegistry* features_ = &FeatureRegistry::builtin();
};

// challenge.rating ("1/8", "1/4", "1/2" or an integer / integer string) times 8; nullopt if malformed.
std::optional<int> cr_x8_of(const assets::Json& rating);

// Content damage type name ("fire") -> rules5e::DamageType; nullopt if unknown.
std::optional<rules5e::DamageType> damage_type_from_name(const std::string& s);
std::optional<rules5e::CreatureType> creature_type_from_name(const std::string& s);

// "1d6+2" -> DiceExpr; nullopt if malformed.
std::optional<rules5e::DiceExpr> parse_dice(std::string_view s);

} // namespace eng::sim
