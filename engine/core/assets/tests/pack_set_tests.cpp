// PackSet tests. The "case:" tests are ported from tests/validate/run_packset_cases.py
// (Tools & Content's reference, tools/validate/packset.py); keep them in sync.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <functional>
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

// ---- reference_errors(): mirrors check_spell / check_script_ref / check_chunk in validate_packs.py ----
namespace {
Json spell(const std::string& rel) { return read(kCore / "spells" / rel); }
Json patched_manifest(const fs::path& pack, const std::function<void(Json&)>& f) {
    Json m = read(pack / "manifest.json");
    f(m);
    write(pack / "manifest.json", m);
    return m;
}
std::string refs_of(const PackSet& s) {
    std::string out;
    for (const auto& e : s.reference_errors()) out += e + "\n";
    return out;
}
Json goblin_v3() { return read(kCore / "monsters/goblin.json"); }
} // namespace

TEST_CASE("refs: blocks_spell and or_targeted_by, same pack (positive)") {
    ScratchDir tmp("packset_refs_spell_pos");
    const auto pack = make(tmp.path, "a", "test.a", "1.0.0", {}, {{"spells/shield.json", spell("shield.json")}, {"spells/magic_missile.json", spell("magic_missile.json")}});
    PackSet s = run({pack});
    CAPTURE(refs_of(s));
    CHECK(s.ok());
    CHECK(s.reference_errors().empty());
}

TEST_CASE("refs: blocks_spell and or_targeted_by, undefined (negative, exact wording, validator order)") {
    ScratchDir tmp("packset_refs_spell_neg");
    const auto pack = make(tmp.path, "a", "test.a", "1.0.0", {}, {{"spells/shield.json", spell("shield.json")}});
    PackSet s = run({pack});
    const std::vector<std::string> want = {
        "test.a:spells/shield.json: effect blocks_spell 'srd51.spell.magic_missile' is not a spell@1 in this pack or its depends",
        "test.a:spells/shield.json: casting_time.trigger.or_targeted_by 'srd51.spell.magic_missile' is not a spell@1 in this pack or its depends"};
    // validator order: blocks_spell first, then each or_targeted_by entry.
    CHECK(s.reference_errors() == want);
}

TEST_CASE("refs: each spell reference kind alone") {
    ScratchDir tmp("packset_refs_spell_each");
    Json only_blocks = spell("shield.json"), only_or = spell("shield.json");
    only_blocks["casting_time"]["trigger"].erase("or_targeted_by");
    only_or["effects"][0].erase("blocks_spell");
    const auto b = make(tmp.path, "b", "test.b", "1.0.0", {}, {{"spells/shield.json", only_blocks}});
    const auto o = make(tmp.path, "o", "test.o", "1.0.0", {}, {{"spells/shield.json", only_or}});
    PackSet sb = run({b}), so = run({o});
    CHECK(sb.reference_errors() ==
          std::vector<std::string>{"test.b:spells/shield.json: effect blocks_spell 'srd51.spell.magic_missile' is not a spell@1 in this pack or its depends"});
    CHECK(so.reference_errors() ==
          std::vector<std::string>{"test.o:spells/shield.json: casting_time.trigger.or_targeted_by 'srd51.spell.magic_missile' is not a spell@1 in this pack or its depends"});
    // Several or_targeted_by entries are checked in order; resolvable ones pass.
    Json many = only_or;
    many["casting_time"]["trigger"]["or_targeted_by"] = Json::array({"test.m.zzz", "test.m.shield", "test.m.aaa"});
    many["id"] = "test.m.shield";
    const auto m = make(tmp.path, "m", "test.m", "1.0.0", {}, {{"spells/shield.json", many}});
    PackSet sm = run({m});
    CHECK(sm.reference_errors() ==
          std::vector<std::string>{"test.m:spells/shield.json: casting_time.trigger.or_targeted_by 'test.m.zzz' is not a spell@1 in this pack or its depends",
                                   "test.m:spells/shield.json: casting_time.trigger.or_targeted_by 'test.m.aaa' is not a spell@1 in this pack or its depends"});
}

TEST_CASE("refs: spell references resolve through depends only") {
    ScratchDir tmp("packset_refs_spell_deps");
    const Files shield = {{"spells/shield.json", spell("shield.json")}};
    const Files mm = {{"spells/magic_missile.json", spell("magic_missile.json")}};
    // positive: spell lives in a transitive dependency
    const auto a = make(tmp.path, "a", "test.a", "1.0.0", {{"test.b", "^1.0"}}, shield);
    const auto b = make(tmp.path, "b", "test.b", "1.0.0", {{"test.c", "^1.0"}});
    const auto c = make(tmp.path, "c", "test.c", "1.0.0", {}, mm);
    PackSet pos = run({a, b, c});
    CHECK(pos.ok());
    CHECK(pos.reference_errors().empty());
    // negative: the spell is in the set but not in a's closure
    const auto a2 = make(tmp.path, "a2", "test.a2", "1.0.0", {}, shield);
    PackSet neg = run({a2, c});
    CHECK(neg.ok());
    CHECK(neg.reference_errors().size() == 2);
    CHECK(refs_of(neg).find("test.a2:spells/shield.json: effect blocks_spell 'srd51.spell.magic_missile' is not a spell@1 in this pack or its depends") !=
          std::string::npos);
}

TEST_CASE("refs: a non-spell content id is not a spell reference") {
    ScratchDir tmp("packset_refs_spell_kind");
    Json shield = spell("shield.json");
    shield["effects"][0]["blocks_spell"] = "srd51.monster.goblin";
    shield["casting_time"]["trigger"].erase("or_targeted_by");
    const auto goblin = doc("monsters/goblin.json");   // behavior removed: no script refs in this case
    const auto core_goblin = make(tmp.path, "g", "test.g", "1.0.0", {}, {goblin});
    const auto own = make(tmp.path, "own", "test.own", "1.0.0", {}, {{"spells/shield.json", shield}, goblin});
    const auto dep = make(tmp.path, "dep", "test.dep", "1.0.0", {{"test.g", "^1.0"}}, {{"spells/shield.json", shield}});
    // (goblin id is defined by both test.g and test.own: use separate sets)
    PackSet s_own = run({own}), s_dep = run({dep, core_goblin});
    const std::string want = "effect blocks_spell 'srd51.monster.goblin' is not a spell@1 in this pack or its depends";
    
    CHECK(s_own.reference_errors() == std::vector<std::string>{"test.own:spells/shield.json: " + want});
    CHECK(s_dep.reference_errors() == std::vector<std::string>{"test.dep:spells/shield.json: " + want});
}

TEST_CASE("refs: multi-effect spells are checked like single-effect ones (first effect, as check_spell)") {
    ScratchDir tmp("packset_refs_spell_multi");
    Json shield = spell("shield.json");
    shield["effects"].push_back(shield["effects"][0]);
    const auto pack = make(tmp.path, "a", "test.a", "1.0.0", {}, {{"spells/shield.json", shield}});
    PackSet s = run({pack});
    REQUIRE_FALSE(s.reference_errors().empty());   // the first effect's refs (blocks_spell, or_targeted_by) are checked, as single-effect spells are
    CHECK(s.reference_errors()[0].find("effect blocks_spell") != std::string::npos);
}

TEST_CASE("refs: script references (entry_script, behavior.ai, monster@1 presentation.ai)") {
    ScratchDir tmp("packset_refs_script");
    const fs::path& t = tmp.path;
    const std::string kScript = "scripts/ai/skirmisher.luau";
    Json goblin = goblin_v3();   // behavior.ai = scripts/ai/skirmisher.luau
    auto with_script = [&](const fs::path& pack) {
        fs::create_directories((pack / kScript).parent_path());
        fs::copy_file(kCore / kScript, pack / kScript);
    };
    // positive: script ships in the pack that references it
    const auto ok = make(t, "ok", "test.ok", "1.0.0", {}, {{"monsters/goblin.json", goblin}});
    with_script(ok);
    patched_manifest(ok, [&](Json& m) { m["entry_script"] = kScript; });
    PackSet s_ok = run({ok});
    CAPTURE(refs_of(s_ok));
    CHECK(s_ok.ok());
    CHECK(s_ok.reference_errors().empty());
    // negative: missing scripts, and no fallback to a dependency's script (PACKSET_API.md §8.4)
    const auto dep = make(t, "dep", "test.dep", "1.0.0");
    with_script(dep);
    const auto no = make(t, "no", "test.no", "1.0.0", {{"test.dep", "^1.0"}}, {{"monsters/goblin.json", goblin}});
    patched_manifest(no, [&](Json& m) { m["entry_script"] = "scripts/main.luau"; });
    PackSet s_no = run({no, dep});
    CHECK(s_no.reference_errors() ==
          std::vector<std::string>{"test.no:manifest.json: entry_script: script scripts/main.luau does not exist in pack",
                                   "test.no:monsters/goblin.json: behavior.ai: script scripts/ai/skirmisher.luau does not exist in pack"});
    // shape: must match ^scripts/.+\.luau$
    Json bad = goblin;
    bad["behavior"]["ai"] = "ai/skirmisher.luau";
    const auto shape = make(t, "shape", "test.shape", "1.0.0", {}, {{"monsters/goblin.json", bad}});
    patched_manifest(shape, [](Json& m) { m["entry_script"] = 7; });
    PackSet s_shape = run({shape});
    CHECK(s_shape.reference_errors() ==
          std::vector<std::string>{"test.shape:manifest.json: entry_script: script path 7 must match ^scripts/.+\\.luau$",
                                   "test.shape:monsters/goblin.json: behavior.ai: script path 'ai/skirmisher.luau' must match ^scripts/.+\\.luau$"});
    // monster@1 carries it as presentation.ai (migrated on load): validator wording keeps presentation.ai
    Json v1 = read(kRoot / "tests/compat/fl1/sample.vertical_slice/monsters/goblin.json");
    v1["presentation"]["ai"] = kScript;
    const auto legacy = make(t, "legacy", "test.legacy", "1.0.0", {}, {{"monsters/goblin.json", v1}});
    PackSet s_legacy = run({legacy});
    CHECK(s_legacy.document_errors().empty());
    CHECK(s_legacy.reference_errors() ==
          std::vector<std::string>{"test.legacy:monsters/goblin.json: presentation.ai: script scripts/ai/skirmisher.luau does not exist in pack"});
    with_script(legacy);
    CHECK(run({legacy}).reference_errors().empty());
}

TEST_CASE("refs: chunk spawn archetypes still checked alongside the new kinds") {
    ScratchDir tmp("packset_refs_chunk");
    const auto pack = make(tmp.path, "a", "test.a", "1.0.0", {}, {doc("chunks/0_0_0.json")});
    PackSet s = run({pack});
    const auto errs = s.reference_errors();
    REQUIRE(!errs.empty());
    CHECK(errs[0].rfind("test.a:chunks/0_0_0.json: spawns[0]: archetype '", 0) == 0);
}
