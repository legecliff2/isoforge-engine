#pragma once
// Engine-registered content schema versions + migrations (ARCHITECTURE.md §3.2).
// JSON Schema files live in schemas/ (owned by Tools & Content); this is the loader side.
//
//  monster@2 (append-only): adds top-level "behavior": {"ai": "<pack-relative .luau path>"}.
//  Migration 1 -> 2 moves presentation.ai to behavior.ai (presentation is presentation-only,
//  §5.4; AI drives simulation) and drops "presentation" if it becomes empty.
//  character@2 (append-only): character@1 + optional top-level "behavior": {"ai": ...}
//  (NPC allies / auto-play). Migration 1 -> 2 is a no-op.
//  monster@3: same gate as character@3. Migration 2 -> 3 marks every pre-@3 features[]/traits[]
//  effect "inert": true (feature effects are active only for monster@3+ files).
//  Also at @3 (both types): damage_resistances / damage_immunities / damage_vulnerabilities /
//  condition_immunities and every damage[] entry after the first become mechanical. Migration 2 -> 3
//  records the ones a pre-@3 file carries in a top-level "inert_mechanics" string array
//  (e.g. ["damage_resistances", "damage[1..]"]); sim::ContentLibrary ignores exactly those.
//  Also at @3 (both types): the "spellcasting" block (slots, caster level, ability, spells) and "type"
//  (creature type) become mechanical. Pre-@3 files carrying a spellcasting block, or a non-humanoid type,
//  get "spellcasting" / "creature_type" in inert_mechanics. "size" is stored (Archetype::size) but has
//  no behaviour yet.
//  monster@4 / character@4 (VERSION_BUMP_BATCH, docs/proposals): migration 3 -> 4 stamps `inert: true` on every
//  reactions[] entry (marker "reactions"), on bonus_actions[] entries @4 would load (kind != "special", marker
//  "bonus_actions") and on traits/features effects of the frozen new kinds; characters with a non-empty
//  equipment list get the marker "equipment". inert_mechanics is merged (union, sorted), never overwritten.
//  character@3: feature effects (features[]/traits[] "effect") become mechanically active.
//  Before @3 they were descriptive only and content baked their numbers into stat blocks (fl1's
//  longsword 1d8+5 includes Dueling). Migration 2 -> 3 marks every pre-@3 effect
//  "inert": true so the engine never double-applies it (docs/CONTENT_COMPAT.md).
#include <optional>
#include <string>
#include <vector>

#include "assets/schema_registry.hpp"

namespace eng::assets {

inline constexpr uint32_t kMonsterSchemaVersion = 4;
inline constexpr uint32_t kCharacterSchemaVersion = 4;
// spell@1: SRD spell (schemas/spell/v1.schema.json); mapped to rules5e::SpellDef by rules5e_content.
inline constexpr uint32_t kSpellSchemaVersion = 1;

// Effect kinds first made mechanical by schema version `v` of monster / character (the frozen literal list a
// migration (v-1 -> v) marks inert in older files). @4: on_event, apply_modifier, grant_action, heal_pool,
// save_effect, reaction (VERSION_BUMP_BATCH 8.2 step 4). sim::FeatureRegistry's per-kind minimum versions must
// equal these (pinned by sim's features_tests "kind list equals the migration's frozen list").
std::vector<std::string> effect_kinds_introduced_at(uint32_t schema_version);

// Registers every engine-known content type with its current version and migrations.
void register_content_schemas(SchemaRegistry& reg);

// behavior.ai of a monster document already migrated to current, if present.
std::optional<std::string> monster_ai_script(const Json& monster);
// behavior.ai of a monster@2+ or character@2+ document already migrated to current.
std::optional<std::string> behavior_ai_script(const Json& doc);

} // namespace eng::assets
