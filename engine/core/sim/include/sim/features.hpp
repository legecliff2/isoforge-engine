#pragma once
// Data-driven feature hooks. Content declares features (character "features[]", monster
// "traits[]") with a machine-readable `effect.kind`; the FeatureRegistry maps each kind to a hook
// implementation. sim never matches feature names or ids: what a feature does comes from its
// effect kind and parameters. Unknown kinds are reported (Archetype::unsupported_features) and
// ignored, so content can run ahead of the engine.
//
// Hook stages:
//   static  - applied once when the archetype is built (e.g. damage_bonus, bonus_action_grant).
//   active  - offered as a rules5e UseFeature request during the encounter (e.g. self_heal);
//             uses/recharge are tracked per combatant by Combat.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "assets/schema_registry.hpp"
#include "rules5e/types.hpp"

namespace eng::sim {

struct Archetype;

// The item id an equipment[] entry names, for any of its forms: a bare string ("chain_mail"), {"id": ...}, or the
// character@4 form {"item": "srd51.item.chain_mail"} ("item" wins when both are present). "" for anything else.
std::string equipment_entry_id(const assets::Json& entry);

// A feature as declared in content.
struct FeatureDecl {
    std::string id;            // content-local id ("second_wind")
    std::string kind;          // effect.kind ("self_heal")
    assets::Json effect;       // full effect object
    bool requires_humanoid_form = false;   // R-G12 (7.2q): the feature / trait object's flag (not the effect's)
};

enum class FeatureCost : uint8_t { Action, BonusAction, Free };   // Free: no action economy (Action Surge)
enum class Recharge : uint8_t { None, ShortRest, LongRest };

// An active feature a combatant can use (rules5e ActionKind::UseFeature).
struct ActiveFeature {
    uint32_t feature_id = 0;       // 1-based index into Archetype::features (stable per archetype)
    std::string id;                // content-local id, for UI/logs only
    std::string kind;              // effect kind
    FeatureCost cost = FeatureCost::Action;
    rules5e::DiceExpr heal{0, 0, 0};   // self_heal: dice + resolved flat bonus
    uint8_t uses = 0;              // per recharge; 0 = unlimited
    Recharge recharge = Recharge::None;
    // Policy hint: use when hp <= hp_max * use_below_num / use_below_den (self_heal default 1/2).
    uint8_t use_below_num = 1, use_below_den = 2;
};

// Context a hook may read while building an archetype.
class FeatureRegistry;
struct FeatureBuildCtx {
    const FeatureRegistry& registry;
    const assets::Json& doc;                    // the stat block (migrated)
    uint32_t declared_version = 0;              // schema version the file declared before migration; 0 = unknown
    const std::vector<const assets::Json*>& attack_actions;   // content action per base.attacks[i]
};

struct FeatureHook {
    // Returns an error to reject the content (malformed parameters); nullopt = applied.
    std::function<std::optional<std::string>(Archetype&, const FeatureDecl&, uint32_t feature_id,
                                             const FeatureBuildCtx&)> apply;
};

// Named predicates over a content action, used by effect "condition" parameters.
using ActionPredicate = std::function<bool(const assets::Json& stat_block, const assets::Json& action)>;

class FeatureRegistry {
public:
    // min_schema_version: the monster / character schema version that made this kind mechanical (0 = since @3,
    // the version that introduced effects). A file that declared an older version than this lists the effect
    // as inert instead of applying it (ContentLibrary::add); content never fails to load because of it.
    void add_hook(std::string kind, FeatureHook hook, uint32_t min_schema_version = 0) {
        hooks_[kind] = std::move(hook);
        min_version_[std::move(kind)] = min_schema_version;
    }
    // A kind with no hook yet whose minimum version is still known (the frozen new-kind list of the latest
    // migration): files that declared an older version list it as inert instead of "unsupported".
    void reserve_kind(std::string kind, uint32_t min_schema_version) { min_version_[std::move(kind)] = min_schema_version; }
    // Kinds (hooked or reserved) whose minimum schema version is exactly `v`, sorted.
    std::vector<std::string> kinds_introduced_at(uint32_t v) const {
        std::vector<std::string> out;
        for (const auto& [k, m] : min_version_) if (m == v) out.push_back(k);
        return out;
    }
    uint32_t min_schema_version(const std::string& kind) const {
        auto it = min_version_.find(kind);
        return it == min_version_.end() ? 0 : it->second;
    }
    void add_condition(std::string id, ActionPredicate p) { conditions_[std::move(id)] = std::move(p); }
    const FeatureHook* hook(const std::string& kind) const;
    const ActionPredicate* condition(const std::string& id) const;
    std::vector<std::string> kinds() const;   // sorted

    // Built-in hooks: damage_bonus, bonus_action_grant, self_heal, damage_mitigation, magic_weapons, multiattack; conditions:
    // one_handed_melee_no_other_weapon, melee_weapon, ranged_weapon.
    static const FeatureRegistry& builtin();

private:
    std::map<std::string, FeatureHook> hooks_;
    std::map<std::string, uint32_t> min_version_;
    std::map<std::string, ActionPredicate> conditions_;
};

// Reads features[] / traits[] (in that order) from a stat block.
std::vector<FeatureDecl> declared_features(const assets::Json& doc);

} // namespace eng::sim
