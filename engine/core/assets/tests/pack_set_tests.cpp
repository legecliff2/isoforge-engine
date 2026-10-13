// PackSet tests. The "case:" tests are ported from tests/validate/run_packset_cases.py
// (Tools & Content's reference, tools/validate/packset.py); keep them in sync.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <string>

#include "assets/pack_set.hpp"
#include "scratch_dir.hpp"

using namespace eng::assets;
namespace fs = std::filesystem;

namespace {
const fs::path kRoot = ENGINE_SOURCE_ROOT;
const fs::path kPacks = kRoot / "content/packs";
const fs::path kSample = kPacks / "sample.vertical_slice";
const fs::path kCore = kPacks / "srd51.core";

Json read(const fs::path& p) {
    std::ifstream in(p);
    return Json::parse(in);
}
void write(const fs::path& p, const Json& j) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << j.dump(2);
}
// Python doc(): from the sample pack, else srd51.core; "behavior" removed.
std::pair<std::string, Json> doc(const std::string& rel) {
    Json d = read(fs::exists(kSample / rel) ? kSample / rel : kCore / rel);
    d.erase("behavior");
    return {rel, d};
}
using Files = std::vector<std::pair<std::string, Json>>;
using Deps = std::vector<std::pair<std::string, std::string>>;
// Python make(): sample manifest with id/version/contents/depends replaced, entry_script dropped.
fs::path make(const fs::path& root, const std::string& dirname, const std::string& pid, const std::string& version = "1.2.0",
              const Deps& depends = {}, const Files& files = {}) {
    const fs::path pack = root / dirname;
    Json man = read(kSample / "manifest.json");
    Json contents = Json::array(), deps = Json::array();
    std::vector<std::string> rels;
    for (const auto& [rel, d] : files) { write(pack / rel, d); rels.push_back(rel); }
    std::sort(rels.begin(), rels.end());
    for (const auto& r : rels) contents.push_back(r);
    for (const auto& [i, r] : depends) deps.push_back(Json{{"id", i}, {"version", r}});
    man["id"] = pid;
    man["version"] = version;
    man["contents"] = contents;
    man["depends"] = deps;
    man.erase("entry_script");
    write(pack / "manifest.json", man);
    return pack;
}
std::vector<std::string> ids(const PackSet& s) {
    std::vector<std::string> out;
    for (const auto& p : s.packs()) out.push_back(p.id);
    return out;
}
std::string all_errors(const PackSet& s) {
    std::string out;
    for (const auto& e : s.error_strings()) out += e + "\n";
    for (const auto& e : s.reference_errors()) out += e + "\n";
    return out;
}
// The validator validates every given pack: request them all.
PackSet run(const std::vector<fs::path>& dirs) {
    std::vector<std::string> req;
    for (const auto& d : dirs) req.push_back(read(d / "manifest.json")["id"].get<std::string>());
    return PackSet::resolve_dirs(dirs, req);
}
bool satisfies_text(const std::string& v, const std::string& r) {
    auto sv = parse_version(v);
    auto rr = parse_range(r);
    return sv && rr && satisfies(*sv, *rr);
}
} // namespace

TEST_CASE("unit: range dialect matches packset.py") {
    const std::tuple<const char*, const char*, bool> cases[] = {
        {"1.2.3", "1.2.3", true},   {"1.2.4", "1.2.3", false},  {"1.9.0", "^1.0", true},     {"2.0.0", "^1.0", false},
        {"0.2.9", "^0.2.3", true},  {"0.3.0", "^0.2.3", false}, {"0.0.4", "^0.0.3", false}, {"0.0.9", "^0.0", true}, {"0.1.0", "^0.0", false},  {"1.2.9", "~1.2.3", true},
        {"1.3.0", "~1.2.3", false}, {"7.0.0", ">=1.2.3", true}, {"1.2.2", ">=1.2.3", false}, {"1.0.0-rc.1", "^1.0", false}};
    for (const auto& [v, r, want] : cases) CHECK_MESSAGE(satisfies_text(v, r) == want, v << " " << r);
    for (const char* bad : {"1", "1.2", "~1.2", ">=1.2", "^1", ">1.2.3", "1.2.3-rc1", "^01.0"})
        CHECK_MESSAGE(!parse_range(bad), bad);
}

TEST_CASE("unit: order is deps first, ties by id, independent of dir order") {
    ScratchDir t("packset_order");
    std::vector<fs::path> dirs = {make(t.path, "z", "test.z", "1.2.0", {{"test.m", "^1.0"}, {"test.b", "^1.0"}}),
                                  make(t.path, "m", "test.m", "1.2.0", {{"test.a", "^1.0"}}), make(t.path, "b", "test.b"),
                                  make(t.path, "a", "test.a")};
    const std::vector<std::string> want = {"test.a", "test.b", "test.m", "test.z"};
    const std::string req[] = {"test.z"};
    for (unsigned seed = 0; seed < 8; ++seed) {
        std::mt19937 rng(seed);
        std::shuffle(dirs.begin(), dirs.end(), rng);
        PackSet s = PackSet::resolve_dirs(dirs, req);
        CHECK(s.ok());
        CHECK(ids(s) == want);
    }
    const fs::path roots[] = {t.path};
    CHECK(ids(PackSet::resolve(roots, req)) == want);
}

TEST_CASE("cases ported from run_packset_cases.py") {
    ScratchDir tmp("packset_cases");
    const fs::path& t = tmp.path;
    const auto goblin = doc("monsters/goblin.json"), fighter = doc("characters/fighter_1.json"), chunk = doc("chunks/0_0_0.json");
    struct Case { const char* name; std::vector<fs::path> packs; bool ok; std::string sub; };
    const std::vector<Case> cases = {
        {"transitive_deps_resolve",
         {make(t / "a1", "a", "test.a", "1.2.0", {{"test.b", "^1.0"}}, {chunk, fighter}),
          make(t / "a1", "b", "test.b", "1.2.0", {{"test.c", "~1.2.0"}}), make(t / "a1", "c", "test.c", "1.2.0", {}, {goblin})},
         true, ""},
        {"missing_dep", {make(t / "a2", "a", "test.a", "1.2.0", {{"test.nowhere", "^1.0"}}, {fighter})}, false,
         "missing_dep: test.a: depends on 'test.nowhere'"},
        {"version_mismatch",
         {make(t / "a3", "a", "test.a", "1.2.0", {{"test.c", "^2.0"}}, {fighter}), make(t / "a3", "c", "test.c", "1.2.0", {}, {goblin})},
         false, "version_mismatch"},
        {"prerelease_never_satisfies",
         {make(t / "a4", "a", "test.a", "1.2.0", {{"test.c", "^1.0"}}, {fighter}),
          make(t / "a4", "c", "test.c", "1.2.0-rc1", {}, {goblin})},
         false, "version_mismatch"},
        {"range_outside_dialect",
         {make(t / "a5", "a", "test.a", "1.2.0", {{"test.c", "^1"}}, {fighter}), make(t / "a5", "c", "test.c", "1.2.0", {}, {goblin})},
         false, "bad_range"},
        {"cycle",
         {make(t / "a6", "a", "test.a", "1.2.0", {{"test.b", "^1.0"}}, {fighter}), make(t / "a6", "b", "test.b", "1.2.0", {{"test.a", "^1.0"}})},
         false, "dependency cycle"},
        {"duplicate_pack_id", {make(t / "a7", "x1", "test.dup", "1.2.0", {}, {fighter}), make(t / "a7", "x2", "test.dup")}, false,
         "duplicate_pack_id"},
        {"duplicate_content_id_across_set",
         {make(t / "a8", "a", "test.a", "1.2.0", {{"test.c", "^1.0"}}, {goblin, fighter}),
          make(t / "a8", "c", "test.c", "1.2.0", {}, {goblin})},
         false, "duplicate_content_id"},
        {"ref_only_in_non_dependency",
         {make(t / "a9", "a", "test.a", "1.2.0", {{"srd51.core", "^1.0"}}, {chunk, fighter}), make(t / "a9", "core", "srd51.core"),
          make(t / "a9", "other", "test.other", "1.2.0", {}, {goblin})},
         false, "archetype 'srd51.monster.goblin' not defined in pack or its depends"},
        // srd51_escape_hatch_warns_without_core: deliberately NOT ported (no escape hatch in the engine).
    };
    for (const auto& c : cases) {
        CAPTURE(c.name);
        PackSet s = run(c.packs);
        const std::string out = all_errors(s);
        CAPTURE(out);
        CHECK((out.empty()) == c.ok);
        CHECK(out.find(c.sub) != std::string::npos);
    }
}

TEST_CASE("errors: exact validator wording, every error collected") {
    ScratchDir tmp("packset_errors");
    const fs::path& t = tmp.path;
    make(t, "a", "t.a", "1.0.0", {{"t.b", "^1.0"}, {"t.c", "^1"}, {"t.d", "^2.0"}, {"t.x", "1.0.0"}});
    make(t, "b", "t.b", "1.0.0", {{"t.a", "^1.0"}});
    make(t, "d", "t.d", "1.0.0");
    make(t, "d2", "t.d");
    fs::create_directories(t / "bad");
    std::ofstream(t / "bad/manifest.json") << "{nope";
    const fs::path roots[] = {t};
    const std::string req[] = {"t.a", "t.a", "t.q"};
    PackSet s = PackSet::resolve(roots, req);
    const std::vector<std::string> want = {
        "bad_manifest: " + (t / "bad").string() + ": unreadable manifest.json",
        "duplicate_pack_id: pack id 't.d' is defined by both " + (t / "d").string() + " and " + (t / "d2").string(),
        "cycle: dependency cycle: t.a -> t.b -> t.a",
        "bad_range: t.a: depends 't.c' range '^1' is not X.Y.Z, ^X.Y, ^X.Y.Z, ~X.Y.Z or >=X.Y.Z",
        "version_mismatch: t.a: depends on 't.d' ^2.0, but the set has t.d 1.0.0",
        "missing_dep: t.a: depends on 't.x' 1.0.0, which is not in the pack set",
        "missing_dep: requested pack 't.q' is not in the pack set",
    };
    CHECK(s.error_strings() == want);
    CHECK(s.errors()[2].kind == PackSetError::Kind::Cycle);
    CHECK(s.errors()[2].pack == "t.a");
    CHECK(s.errors()[4].id == "t.d");
}

TEST_CASE("cycle reported once, against the smallest id, from any root") {
    ScratchDir tmp("packset_cycle");
    make(tmp.path, "c", "t.c", "1.0.0", {{"t.a", "^1.0"}});
    make(tmp.path, "a", "t.a", "1.0.0", {{"t.b", "^1.0"}});
    make(tmp.path, "b", "t.b", "1.0.0", {{"t.c", "^1.0"}});
    const fs::path roots[] = {tmp.path};
    const std::string req[] = {"t.c", "t.b"};
    PackSet s = PackSet::resolve(roots, req);
    REQUIRE(s.errors().size() == 1);
    CHECK(s.error_strings()[0] == "cycle: dependency cycle: t.a -> t.b -> t.c -> t.a");
}

TEST_CASE("repo: sample.vertical_slice resolves with srd51.core") {
    const fs::path roots[] = {kPacks};
    const std::string req[] = {"sample.vertical_slice"};
    PackSet s = PackSet::resolve(roots, req);
    CAPTURE(all_errors(s));
    REQUIRE(s.ok());
    CHECK(s.reference_errors().empty());
    CHECK(s.document_errors().empty());
    CHECK(ids(s) == std::vector<std::string>{"srd51.core", "sample.vertical_slice"});
    REQUIRE(s.owner_of("srd51.monster.goblin"));
    CHECK(s.owner_of("srd51.monster.goblin")->id == "srd51.core");
    CHECK(s.owner_of("nope") == nullptr);
    CHECK(s.script_key_for("srd51.monster.goblin", "scripts/ai/skirmisher.luau") == "srd51.core:scripts/ai/skirmisher.luau");
    CHECK(s.script("srd51.core:scripts/ai/skirmisher.luau").has_value());
    CHECK(!s.script("sample.vertical_slice:scripts/ai/skirmisher.luau"));
    CHECK(s.closure("sample.vertical_slice") == std::vector<std::string>{"srd51.core", "sample.vertical_slice"});
    CHECK(s.closure("srd51.core") == std::vector<std::string>{"srd51.core"});
    CHECK(s.packs()[0].version == SemVer{1, 0, 0});
    // Wrapper: one pack dir -> its root, its id.
    PackSet w = PackSet::from_pack_dir(kSample);
    CHECK(w.ok());
    CHECK(ids(w) == ids(s));
}

TEST_CASE("repo: fl1 compat pack loads as its own set") {
    PackSet s = PackSet::from_pack_dir(kRoot / "tests/compat/fl1/sample.vertical_slice");
    CAPTURE(all_errors(s));
    CHECK(s.ok());
    CHECK(s.packs().size() == 1);
    CHECK(s.document_errors().empty());
}
