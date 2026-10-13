#pragma once
// Content -> rules adapters: monster@N / character@N stat blocks -> rules5e::Combatant.
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "assets/schema_registry.hpp"
#include "sim/features.hpp"
#include "rules5e/types.hpp"

namespace eng::sim {

struct Archetype {
    std::string id;                       // "srd51.monster.goblin"
    std::string schema_type;              // "monster" | "character"
    rules5e::Combatant base{};            // id/team filled per spawn
    // Per base.attacks[i]: ranged normal / long range in feet (0 for melee). rules5e::Attack keeps
    // only a uint8_t reach_or_range_ft, so sim owns the full range data.
    std::vector<uint16_t> normal_range_ft, long_range_ft;
    std::vector<FeatureDecl> features;    // declared (features[] then traits[]); feature_id = index + 1
    std::vector<ActiveFeature> active;    // usable via UseFeature (from active hooks)
    std::vector<std::string> unsupported_features;   // "<id> (<kind>)" with no registered hook
    std::optional<std::string> ai;        // behavior.ai (monster@2 / character@2), pack-relative
};

// Owns archetypes and the strings rules5e::Attack::id views into (stable addresses).
class ContentLibrary {
public:
    ContentLibrary() = default;
    explicit ContentLibrary(const FeatureRegistry& features) : features_(&features) {}
    // Loads every manifest "contents" .json whose $schema is monster@N or character@N.
    // Returns error messages (empty = ok).
    std::vector<std::string> load_pack(const std::string& pack_dir);
    // Adds one already-migrated stat block document.
    std::optional<std::string> add(const assets::Json& doc, const std::string& schema_type);

    const Archetype* find(const std::string& id) const;
    const std::map<std::string, Archetype>& all() const noexcept { return by_id_; }

private:
    std::map<std::string, Archetype> by_id_;
    std::deque<std::string> strings_;
    const FeatureRegistry* features_ = &FeatureRegistry::builtin();
};

// "1d6+2" -> DiceExpr; nullopt if malformed.
std::optional<rules5e::DiceExpr> parse_dice(std::string_view s);

} // namespace eng::sim
