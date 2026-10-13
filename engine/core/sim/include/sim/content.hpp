#pragma once
// Content -> rules adapters: monster@N / character@N stat blocks -> rules5e::Combatant.
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "assets/pack_set.hpp"
#include "assets/schema_registry.hpp"
#include "sim/features.hpp"
#include "rules5e/types.hpp"

namespace eng::sim {

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
    std::optional<std::string> ai;        // behavior.ai (monster@2 / character@2), pack-relative
    // Script id of behavior.ai, resolved within the archetype's own pack: "pack_id:path".
    std::optional<std::string> ai_script_id() const {
        if (!ai) return std::nullopt;
        return assets::make_script_key(pack_id, *ai);
    }
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
    std::optional<std::string> add(const assets::Json& doc, const std::string& schema_type, const std::string& pack_id = "");

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
