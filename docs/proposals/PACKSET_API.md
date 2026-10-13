# Proposal: `eng::assets::PackSet` (multi-pack loading with dependencies)

Status: draft for Engine Core (implementer) and Rules (spellbook port). Director approved the
srd51.core split. Content-side reference semantics: `tools/validate/packset.py` (PR #39). The
engine must match it exactly, and the shared cases in `tests/validate/run_packset_cases.py`
define the behaviour.

## 1. Shape (engine/assets)

```cpp
namespace eng::assets {

struct SemVer { uint32_t major = 0, minor = 0, patch = 0; };          // integers only, no prerelease
struct VersionRange { SemVer lo; std::optional<SemVer> hi_exclusive; };
std::optional<SemVer>       parse_version(std::string_view);          // "1.2.3"; prerelease -> nullopt
std::optional<VersionRange> parse_range(std::string_view);            // dialect below; else nullopt
bool satisfies(const SemVer&, const VersionRange&);

struct PackInfo {
    std::string id;                 // manifest id
    SemVer version;
    std::filesystem::path dir;
    Json manifest;                  // migrated manifest@N
};

struct PackSetError {
    enum class Kind { MissingDep, VersionMismatch, BadRange, Cycle, DuplicatePackId,
                      DuplicateContentId, BadManifest };
    Kind kind;
    std::string pack;               // pack the error is reported against
    std::string id;                 // dependency / content id involved ("" if none)
    std::string detail;             // human-readable; same wording as the validator
};

class PackSet {
public:
    // Scans each root for <root>/<dir>/manifest.json (one level deep), then resolves `requested`
    // and their transitive depends. Never throws on content errors: every error is collected.
    static PackSet resolve(std::span<const std::filesystem::path> roots,
                           std::span<const std::string> requested);

    bool ok() const noexcept;                                   // errors().empty()
    std::span<const PackSetError> errors() const noexcept;
    std::span<const PackInfo> packs() const noexcept;           // load order: deps first

    // Content ids (monster, character, spell) -> owning pack; nullptr if not in the set.
    const PackInfo* owner_of(std::string_view content_id) const;
    // Scripts: key = pack_id + ':' + pack-relative path ("srd51.core:scripts/ai/skirmisher.luau").
    std::optional<std::filesystem::path> script(std::string_view key) const;
    // behavior.ai of an archetype resolves relative to the archetype's own pack.
    std::string script_key_for(std::string_view content_id, std::string_view rel_path) const;
};

} // namespace eng::assets
```

## 2. Resolution rules (identical to the validator)

- **Index:** packs are indexed by manifest `id` across all roots. The same id found twice is
  `DuplicatePackId` and the second is ignored. Roots and directories are scanned, then **sorted by
  path**, so results never depend on filesystem enumeration order.
- **Closure:** from `requested` (deduplicated, sorted), follow manifest `depends` transitively.
  Each dependency is checked in order:
  - its range parses (`BadRange`)
  - the pack exists (`MissingDep`; also used for a requested id that isn't found)
  - its version satisfies the range (`VersionMismatch`)
- **Cycle:** reported once, against the smallest pack id in the cycle, with the ring rotated to
  start there (`a -> b -> a`).
- **Order:** topological sort with Kahn's algorithm, where the ready set is ordered by pack id.
  Dependencies always come before their dependents, and ties break by pack id. Example:
  z depends on {m, b} and m depends on a, which gives `a, b, m, z`.
- **Duplicate content ids:** any monster/character/spell id defined by two packs of the
  resolved set is `DuplicateContentId`. **No override in v1.** A future explicit `patches`
  mechanism may relax this; nothing silently shadows.
- **References** (chunk spawn archetypes, spell refs, etc.) resolve only within the referencing
  pack or its **transitive** depends. A pack that isn't a dependency never satisfies a reference,
  even if it is loaded in the same set.
- **All errors are collected.** Resolution doesn't stop at the first error, and `packs()` still
  holds every pack that resolved, for diagnostics. Callers must refuse to start when `!ok()`.

### Range dialect

| form | meaning |
|---|---|
| `X.Y.Z` | exactly X.Y.Z |
| `^X.Y.Z` / `^X.Y` | >= X.Y.Z (Z = 0 for `^X.Y`), < next bump of the left-most non-zero component: `^1.2.3` -> <2.0.0, `^0.2.3` -> <0.3.0, `^0.0.3` -> <0.0.4 (so `^0.0.3` rejects 0.0.4), `^0.0` -> <0.1.0 (npm: a missing patch is a wildcard) |
| `~X.Y.Z` | >= X.Y.Z, < X.(Y+1).0 |
| `>=X.Y.Z` | >= X.Y.Z, unbounded |

Integers only, with no leading zeros. Prerelease versions (`1.0.0-rc1`) never satisfy any range.
Anything else (`^1`, `1.2`, `~1.2`, `>1.2.3`) is `BadRange`, even though the manifest@1
schema pattern admits some of these.

## 3. Consumers take `const PackSet&`

```cpp
// sim (Engine Core)
std::vector<std::string> ContentLibrary::load(const assets::PackSet&);   // packs() order; Archetype gains pack_id
// rules (Rules)
SpellBookLoad rules5e_content::load_spellbook(const eng::assets::PackSet&);  // refs resolve via the set rules above
// script_host (Engine Core)
PackScripts load_pack_scripts(ScriptHost&, const assets::PackSet&);      // keys "pack:path"; ai_by_archetype
                                                                         // binds behavior.ai in the archetype's pack
```

- The existing single-directory overloads stay as thin wrappers that build a one-pack set, so
  the compat tests keep working unchanged.
- Each consumer walks `packs()` in order and, inside a pack, its manifest `contents` in sorted order.
- Script ids are `pack_id:path` everywhere: in logs, replays and the AI binding table. Two packs
  may both ship `scripts/ai/x.luau` without colliding.
- `load_pack_scripts` type-checks each pack's scripts with that pack's `api_level`.

## 4. Game CLI / config

`vertical_slice --pack-root <dir>... --pack <id>...`, defaulting to `--pack-root content/packs`
and `--pack sample.vertical_slice`. srd51.core arrives through depends, never by being listed.
The chunk still comes from the requested pack's `chunks/`. The same pair (roots, requested ids)
is the config-file shape: `{ "pack_roots": [...], "packs": [...] }`.

## 5. Determinism

The load order is a pure function of the manifests:
- sorted root and directory scan
- id-ordered closure and Kahn ready set
- sorted `contents`

Nothing may depend on `directory_iterator` order, hash-map iteration, or wall-clock time.
Archetype ids, spell runtime ids (FNV-1a of the content id) and stable_ids don't involve the pack
at all, so moving content between packs doesn't change goldens. Adding the srd51.core split
must leave every golden bit-identical; if one changes, that's a bug.

## 6. Compat packs

`tests/compat/<level>/` is its own root and its own set. fl1 is resolved with
`roots = {tests/compat/fl1}` and never sees `content/packs`, so a frozen copy can't collide with
or resolve against live packs. fl1 has no depends and keeps its srd51 ids in-pack, so it resolves
as a one-pack set. If a future compat level depends on srd51.core, the frozen copy of core goes
into that level's directory.

## 7. srd51.* escape hatch (validator only, deprecated)

The validator still accepts undefined `srd51.*` references, with a deprecation warning, but only
in packs that don't (transitively) depend on srd51.core. The engine shouldn't implement the hatch:
a reference that doesn't resolve is skipped with a diagnostic, as the sim does today.

## 8. Decisions recorded with Engine Core (PR #41)

1. **Single-directory wrapper.** `PackSet::from_pack_dir(dir)` uses `dir`'s parent as the only
   root and requests `dir`'s manifest id. `content/packs/sample.vertical_slice` therefore sees
   its sibling `srd51.core` through depends. fl1's pack is `tests/compat/fl1/sample.vertical_slice`,
   so its root is `tests/compat/fl1`, and it stays its own one-pack set (see §6).
2. **Document load failures are not PackSetErrors.** A contents `.json` that fails to load or
   migrate is reported in `document_errors()` as `"<pack>:<path>: <error>"`. `errors()` / `ok()`
   only cover resolution. Each consumer decides whether a document error is fatal. The validator
   reports these per pack, as before.
3. **`reference_errors()`** uses the validator wording
   (`"<pack>:<rel>: spawns[i]: archetype '<id>' not defined in pack or its depends"`).
   **Known gap:** right now it only checks chunk spawn archetypes. The other reference kinds the
   validator checks (spell refs and other content refs) still need to be added before this
   proposal leaves draft.
4. **behavior.ai is strictly per pack.** `script_key_for` resolves relative to the archetype's own
   pack only, with no fallback to the depends. A test that builds a scratch copy of the sample pack
   with its own archetype must also copy `scripts/ai/skirmisher.luau` into that pack. (The
   script_host test does this.)
5. **Caret follows npm.** `^1.2.3` -> <2.0.0, `^0.2.3` -> <0.3.0, `^0.0.3` -> <0.0.4 (rejects 0.0.4),
   `^0.0` -> <0.1.0. Both `packset.py` and `pack_set.cpp` implement this, and the shared cases cover it.

## 9. Work items

1. Engine Core: `PackSet` in engine/assets, plus a port of `tests/validate/run_packset_cases.py`
   cases to C++ unit tests, including the 8-shuffle order test.
2. Engine Core: `ContentLibrary::load(const PackSet&)`, `load_pack_scripts(…, const PackSet&)`,
   game CLI.
3. Rules: `load_spellbook(const PackSet&)`.
4. Update the tests listed in the srd51.core PR to resolve `{content/packs}` +
   `sample.vertical_slice`.
