#pragma once
// Engine-registered content schema versions + migrations (ARCHITECTURE.md §3.2).
// JSON Schema files live in schemas/ (owned by Tools & Content); this is the loader side.
//
//  monster@2 (append-only): adds top-level "behavior": {"ai": "<pack-relative .luau path>"}.
//  Migration 1 -> 2 moves presentation.ai to behavior.ai (presentation is presentation-only,
//  §5.4; AI drives simulation) and drops "presentation" if it becomes empty.
//  character@2 (append-only): character@1 + optional top-level "behavior": {"ai": ...}
//  (NPC allies / auto-play). Migration 1 -> 2 is a no-op.
//  character@3: feature effects (features[]/traits[] "effect") become mechanically active.
//  Before @3 they were descriptive only and content baked their numbers into stat blocks (fl1's
//  longsword 1d8+5 includes Dueling). Migration 2 -> 3 marks every pre-@3 effect
//  "inert": true so the engine never double-applies it (docs/CONTENT_COMPAT.md).
#include <optional>
#include <string>

#include "assets/schema_registry.hpp"

namespace eng::assets {

inline constexpr uint32_t kMonsterSchemaVersion = 2;
inline constexpr uint32_t kCharacterSchemaVersion = 3;

// Registers every engine-known content type with its current version and migrations.
void register_content_schemas(SchemaRegistry& reg);

// behavior.ai of a monster document already migrated to current, if present.
std::optional<std::string> monster_ai_script(const Json& monster);
// behavior.ai of a monster@2+ or character@2+ document already migrated to current.
std::optional<std::string> behavior_ai_script(const Json& doc);

} // namespace eng::assets
