#pragma once
// Engine-registered content schema versions + migrations (ARCHITECTURE.md §3.2).
// JSON Schema files live in schemas/ (owned by Tools & Content); this is the loader side.
//
//  monster@2 (append-only): adds top-level "behavior": {"ai": "<pack-relative .luau path>"}.
//  Migration 1 -> 2 moves presentation.ai to behavior.ai (presentation is presentation-only,
//  §5.4; AI drives simulation) and drops "presentation" if it becomes empty.
#include <optional>
#include <string>

#include "assets/schema_registry.hpp"

namespace eng::assets {

inline constexpr uint32_t kMonsterSchemaVersion = 2;

// Registers every engine-known content type with its current version and migrations.
void register_content_schemas(SchemaRegistry& reg);

// behavior.ai of a monster document already migrated to current, if present.
std::optional<std::string> monster_ai_script(const Json& monster);

} // namespace eng::assets
