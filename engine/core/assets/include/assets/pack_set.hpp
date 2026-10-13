#pragma once
// PackSet: multi-pack resolution with manifest depends (docs/proposals/PACKSET_API.md).
// Reference semantics: tools/validate/packset.py (Tools & Content); keep the two in sync. The
// shared cases from tests/validate/run_packset_cases.py are ported in assets/tests/pack_set_tests.cpp.
//
//  * Index: <root>/<dir>/manifest.json, one level deep, all roots' dirs sorted by path. A pack id
//    seen twice -> DuplicatePackId (the later dir is ignored); unreadable manifest -> BadManifest.
//  * Closure from `requested` (deduplicated, sorted), following depends (sorted by id): per dep
//    BadRange, then MissingDep, then VersionMismatch; a requested id not indexed -> MissingDep.
//  * Cycle: reported once, against the smallest id in it, ring rotated to start there.
//  * Order: Kahn, ready set by pack id (deps first, ties by id).
//  * Duplicate monster/character/spell ids across the resolved set -> DuplicateContentId (no
//    overrides in v1).
//  * References resolve only within the referencing pack and its transitive depends.
//  * Every error is collected; packs() still holds everything that resolved.
//  Range dialect (integers only, no leading zeros, no prerelease):
//    X.Y.Z exact | ^X.Y / ^X.Y.Z (npm caret) | ~X.Y.Z (< X.(Y+1).0) | >=X.Y.Z.
//  Prerelease pack versions never satisfy any range.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/schema_registry.hpp"

namespace eng::assets {

struct SemVer {
    uint32_t major = 0, minor = 0, patch = 0;
    auto operator<=>(const SemVer&) const = default;
};
struct VersionRange {
    SemVer lo;
    std::optional<SemVer> hi_exclusive;   // nullopt = unbounded
};
std::optional<SemVer> parse_version(std::string_view);        // "1.2.3"; prerelease -> nullopt
std::optional<VersionRange> parse_range(std::string_view);    // dialect above; else nullopt
bool satisfies(const SemVer&, const VersionRange&);
std::string to_string(const SemVer&);

struct PackInfo {
    std::string id;
    SemVer version;                   // {0,0,0} if version_text does not parse (see below)
    std::filesystem::path dir;
    Json manifest;                    // migrated manifest@N
    std::string version_text;         // manifest "version" verbatim (may be prerelease/invalid)
};

struct PackSetError {
    enum class Kind { MissingDep, VersionMismatch, BadRange, Cycle, DuplicatePackId, DuplicateContentId, BadManifest };
    Kind kind;
    std::string pack;     // pack the error is reported against (BadManifest: the directory)
    std::string id;       // dependency / content id involved ("" if none)
    std::string detail;   // same wording as the validator
};
// Validator spelling: "missing_dep", "version_mismatch", "bad_range", "cycle",
// "duplicate_pack_id", "duplicate_content_id", "bad_manifest".
std::string_view to_string(PackSetError::Kind);
std::string to_string(const PackSetError&);   // "<kind>: <detail>" (validator format)

// A contents .json of a resolved pack, loaded through the schema registry (migrated).
struct PackDocument {
    std::size_t pack = 0;     // index into packs()
    std::string path;         // pack-relative
    std::string content_id;   // top-level "id" ("" if none)
    LoadResult doc;           // doc.ok may be false: see document_errors()
};

std::string make_script_key(std::string_view pack_id, std::string_view rel_path);   // "pack:path"

class PackSet {
public:
    static PackSet resolve(std::span<const std::filesystem::path> roots, std::span<const std::string> requested);
    // Same, over explicit pack directories (validator's PackSet(dirs).resolve(requested)).
    static PackSet resolve_dirs(std::span<const std::filesystem::path> pack_dirs, std::span<const std::string> requested);
    // Single-directory wrapper (compat tests, tools): the dir's parent is the root, its manifest
    // id the request. Only that one root's packs are visible.
    static PackSet from_pack_dir(const std::filesystem::path& pack_dir);

    bool ok() const noexcept { return errors_.empty(); }
    std::span<const PackSetError> errors() const noexcept { return errors_; }
    std::vector<std::string> error_strings() const;
    std::span<const PackInfo> packs() const noexcept { return packs_; }   // load order
    const PackInfo* find(std::string_view pack_id) const;
    std::optional<std::size_t> index_of(std::string_view pack_id) const;

    // Monster/character/spell content id -> owning pack (first in load order); nullptr if none.
    const PackInfo* owner_of(std::string_view content_id) const;
    // Owner of content_id if it is visible from `from_pack` (itself or its transitive depends).
    const PackInfo* resolve_ref(std::string_view from_pack, std::string_view content_id) const;
    // `from_pack` and its transitive depends (load order).
    std::vector<std::string> closure(std::string_view from_pack) const;

    // Chunk spawn archetypes not visible from their pack, validator wording:
    // "<pack>:<rel>: spawns[i]: archetype '<id>' not defined in pack or its depends".
    std::vector<std::string> reference_errors() const;

    // Scripts: every <pack>/scripts/**/*.luau of the resolved packs, key "pack_id:path".
    std::optional<std::filesystem::path> script(std::string_view key) const;
    const std::vector<std::string>& script_keys() const noexcept { return script_keys_; }   // load order, sorted per pack
    // behavior.ai of an archetype resolves relative to the archetype's own pack.
    std::string script_key_for(std::string_view content_id, std::string_view rel_path) const;

    // Every contents .json of the resolved packs: pack order, then sorted contents.
    const std::vector<PackDocument>& documents() const noexcept { return docs_; }
    // "<pack>:<path>: <error>" for documents that failed to load (consumers decide; not PackSetErrors).
    const std::vector<std::string>& document_errors() const noexcept { return doc_errors_; }

private:
    std::vector<PackInfo> packs_;
    std::vector<PackSetError> errors_;
    std::map<std::string, std::vector<std::string>, std::less<>> deps_;   // resolved edges
    std::map<std::string, std::size_t, std::less<>> owner_;               // content id -> pack index
    std::vector<std::string> script_keys_;
    std::map<std::string, std::filesystem::path, std::less<>> scripts_;
    std::vector<PackDocument> docs_;
    std::vector<std::string> doc_errors_;
};

} // namespace eng::assets
