#pragma once
// Versioned schema loader + migration registry (ARCHITECTURE.md §3.1, §3.2).
//
// Load pipeline: parse -> read "$schema": "<type>@<N>" -> validate against N -> run
// migrations N->N+1->...->current -> validate against current -> return migrated JSON.
// Migrations are pure Json -> Json functions; no I/O. Source files are never rewritten.
//
// Determinism: Json is nlohmann::json (keys in std::map, i.e. sorted), so dump() is
// canonical. Floating-point numbers are rejected at load (§4.3: integers or decimal strings).
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace eng::assets {

using Json = nlohmann::json;

struct SchemaRef {
    std::string type;
    uint32_t version = 0;
    auto operator<=>(const SchemaRef&) const = default;
};
// "monster@3" -> {"monster", 3}. Version must be >= 1, no leading zeros.
std::optional<SchemaRef> parse_schema_ref(std::string_view s);
std::string to_string(const SchemaRef&);

struct MigrationCtx {
    std::string_view type;
    uint32_t from = 0, to = 0;
    std::vector<std::string> warnings;   // surfaced in LoadResult
};

using MigrationFn = std::function<void(Json&, MigrationCtx&)>;
// Returns an error message, or nullopt if valid. Hook for the JSON Schema validator (§6
// valijson); v0 ships no validator, callers register their own checks.
using ValidatorFn = std::function<std::optional<std::string>(const Json&)>;

struct LoadResult {
    bool ok = false;
    Json data;                           // migrated to current; "$schema" rewritten
    SchemaRef declared;                  // what the file said
    SchemaRef current;                   // what it is now
    std::string error;
    std::vector<std::string> warnings;
};

class SchemaRegistry {
public:
    // Declares a type and its current version. Throws std::logic_error on re-declare with a
    // different version.
    void register_type(std::string type, uint32_t current_version);
    // One step: to must equal from + 1. Throws std::logic_error on duplicates/bad steps.
    void register_migration(std::string type, uint32_t from, uint32_t to, MigrationFn fn);
    void register_validator(std::string type, uint32_t version, ValidatorFn fn);

    std::optional<uint32_t> current_version(std::string_view type) const;
    // Checks every declared type has a complete 1..current chain. Empty = OK.
    std::vector<std::string> check_chains() const;

    LoadResult load(Json doc) const;
    LoadResult load_text(std::string_view text) const;
    LoadResult load_file(const std::filesystem::path& path) const;

private:
    struct TypeInfo {
        uint32_t current = 0;
        std::map<uint32_t, MigrationFn> migrations;   // keyed by from-version
        std::map<uint32_t, ValidatorFn> validators;
    };
    std::map<std::string, TypeInfo, std::less<>> types_;
};

// True if any number anywhere in j is floating-point (§4.3).
bool contains_float(const Json& j);

// Process-wide registry used by ASSET_MIGRATION. Note: registration objects in static
// libraries are dropped by the linker unless referenced; engine code should prefer explicit
// register_*_migrations(SchemaRegistry&) functions. The macro is for the doc's §3.2 form.
SchemaRegistry& global_registry();

} // namespace eng::assets

#define ENG_ASSETS_CAT2(a, b) a##b
#define ENG_ASSETS_CAT(a, b) ENG_ASSETS_CAT2(a, b)
#define ASSET_MIGRATION(type, from, to, ...)                                                   \
    static const bool ENG_ASSETS_CAT(eng_asset_migration_, __LINE__) =                         \
        (::eng::assets::global_registry().register_migration(type, from, to, __VA_ARGS__), true)
