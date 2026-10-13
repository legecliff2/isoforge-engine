#pragma once
// spell@1 (schemas/spell/v1.schema.json) -> rules5e::SpellDef. Schema validation is done by the
// asset pipeline / content_validate; this loader re-checks what rules depends on and fails
// with a message instead of guessing.
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/schema_registry.hpp"
#include "rules5e/spells.hpp"

namespace rules5e_content {

struct SpellLoad {
    std::optional<rules5e::SpellDef> def;
    std::string error;            // set when !def
};

SpellLoad spell_from_json(const eng::assets::Json&);

struct SpellBookLoad {
    std::shared_ptr<rules5e::SpellBook> book;   // null on error
    std::vector<std::string> errors;            // "<file>: <message>", sorted by file
};
// Loads every *.json with "$schema": "spell@1" under `dir` (recursive, sorted by path).
SpellBookLoad load_spellbook(const std::filesystem::path& dir);

} // namespace rules5e_content
