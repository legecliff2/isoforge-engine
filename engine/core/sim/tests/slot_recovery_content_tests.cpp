// VERSION_BUMP_BATCH 7.2n sim wiring: the slot_recovery feature kind (Arcane Recovery / Natural Recovery) loads into
// Archetype::slot_recovery, sim::register_slot_recovery puts its FeatureUses row into a kept roster, and
// sim::short_rest_with_recovery / apply_slot_recovery use it after a short rest. Rests stay outside Combat (no hash tags).
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sim/content.hpp"
#include "sim/rest.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::SlotRecoveryResult;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kId = "test.character.caster";
using Slots = std::array<uint8_t, 10>;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json recovery(Json effect) {
    effect["kind"] = "slot_recovery";
    return {{"id", "recovery"}, {"name", "Recovery"}, {"description", "x"}, {"effect", std::move(effect)}};
}
// A level-6 caster of `cls` (budget 3) with 4 / 3 / 3 slots of levels 1..3.
Json caster(const std::string& cls, Json features, int level = 6) {
    Json c = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    c["$schema"] = "character@4";
    c["id"] = kId;
    c["classes"] = Json::array({{{"class", cls}, {"level", level}, {"hit_die", "d6"}}});
    c["abilities"]["int"] = 16;
    c["features"] = std::move(features);
    c["spellcasting"] = {{"ability", cls == "wizard" ? "int" : "wis"}, {"caster_level", level},
                         {"slots", {{"1", 4}, {"2", 3}, {"3", 3}}}, {"spells", Json::array()}};
    return c;
}
std::optional<std::string> load(ContentLibrary& lib, const Json& doc) { return lib.add(doc, "character"); }

struct Party {
    ContentLibrary lib;
    const Archetype* a = nullptr;
    rules5e::EncounterState roster;
    rules5e::CombatantId id = 1;
    rules5e::Combatant& self() { return roster.combatants[0]; }
};
void setup(Party& p, const Json& doc) {
    auto e = load(p.lib, doc);
    if (e) MESSAGE(*e);
    REQUIRE_FALSE(e);
    p.a = p.lib.find(kId);
    REQUIRE(p.a);
    REQUIRE(p.a->slot_recovery.size() == 1);
    rules5e::Combatant k = p.a->base;
    k.id = p.id;
    p.roster.combatants.push_back(k);
    REQUIRE(register_slot_recovery(p.roster, p.id, *p.a) == 1);
    CHECK(register_slot_recovery(p.roster, p.id, *p.a) == 0);   // idempotent
    // Spend some slots in the "fight": 1st 4 -> 1, 2nd 3 -> 1, 3rd 3 -> 2.
    p.self().spell_slots[1] = 1;
    p.self().spell_slots[2] = 1;
    p.self().spell_slots[3] = 2;
}
Slots slots(std::initializer_list<std::pair<int, int>> l) {
    Slots s{};
    for (auto [lvl, n] : l) s[std::size_t(lvl)] = uint8_t(n);
    return s;
}
}  // namespace

TEST_CASE("slot_recovery loader: shapes, defaults and errors") {
    {
        ContentLibrary lib;
        REQUIRE_FALSE(load(lib, caster("wizard", Json::array({recovery(Json::object({{"class", "wizard"}}))}))));
        const Archetype* a = lib.find(kId);
        REQUIRE(a);
        REQUIRE(a->slot_recovery.size() == 1);
        const auto& s = a->slot_recovery[0];
        CHECK(s.feature_id == 1);
        CHECK(s.class_level == 6);
        CHECK(s.max_slot_level == 5);
        CHECK(s.uses == 1);
        CHECK(a->unsupported_features.empty());
    }
    {
        ContentLibrary lib;
        REQUIRE_FALSE(load(lib, caster("druid", Json::array({recovery({{"class", "druid"}, {"max_slot_level", 9}, {"uses", 255}})}), 3)));
        const auto& s = lib.find(kId)->slot_recovery.at(0);
        CHECK(s.class_level == 3);
        CHECK(s.max_slot_level == 9);
        CHECK(s.uses == 255);
    }
    const Json bad[] = {
        Json::object(),                                          // no class
        {{"class", "sorcerer"}},                                 // unsupported class
        {{"class", "druid"}},                                    // not in classes[] (a wizard)
        {{"class", "wizard"}, {"max_slot_level", 0}},
        {{"class", "wizard"}, {"max_slot_level", 10}},
        {{"class", "wizard"}, {"max_slot_level", "5"}},
        {{"class", "wizard"}, {"max_slot_level", Json::parse("2.5")}},
        {{"class", "wizard"}, {"uses", 0}},
        {{"class", "wizard"}, {"uses", 256}},
        {{"class", "wizard"}, {"uses", -1}},
    };
    for (const Json& eff : bad) {
        ContentLibrary lib;
        CAPTURE(eff.dump());
        CHECK(load(lib, caster("wizard", Json::array({recovery(eff)}))).has_value());
    }
    {   // two declarations for one class
        ContentLibrary lib;
        Json f2 = recovery({{"class", "wizard"}});
        f2["id"] = "recovery2";
        CHECK(load(lib, caster("wizard", Json::array({recovery({{"class", "wizard"}}), f2}))).has_value());
    }
}

TEST_CASE("slot_recovery: Ok after a short rest recovers the chosen slots and spends the use") {
    Party p;
    setup(p, caster("wizard", Json::array({recovery({{"class", "wizard"}})})));
    auto rest = make_rest_rng(7);
    const auto r = short_rest_with_recovery(p.roster, p.id, rest, 0, &p.a->slot_recovery[0], slots({{1, 1}, {2, 1}}));
    CHECK(r.rest.ok);
    REQUIRE(r.recovery.has_value());
    CHECK(*r.recovery == SlotRecoveryResult::Ok);
    CHECK(p.self().spell_slots[1] == 2);
    CHECK(p.self().spell_slots[2] == 2);
    CHECK(p.self().spell_slots[3] == 2);
    CHECK(rules5e::feature_uses_left(p.roster, p.id, p.a->slot_recovery[0].feature_id) == 0);
    // A refused rest attempts nothing.
    p.self().dead = true;
    const auto dead = short_rest_with_recovery(p.roster, p.id, rest, 0, &p.a->slot_recovery[0], slots({{1, 1}}));
    CHECK_FALSE(dead.rest.ok);
    CHECK_FALSE(dead.recovery.has_value());
}

TEST_CASE("slot_recovery: every refusal leaves the roster unchanged") {
    struct Case { const char* name; Slots add; uint8_t max_level; bool spend_first; bool unknown_id; bool unregistered; SlotRecoveryResult want; };
    const Case cases[] = {
        {"unknown id", slots({{1, 1}}), 5, false, true, false, SlotRecoveryResult::NoFeature},
        {"no row", slots({{1, 1}}), 5, false, false, true, SlotRecoveryResult::NoFeature},
        {"no uses", slots({{1, 1}}), 5, true, false, false, SlotRecoveryResult::NoUsesLeft},
        {"empty", Slots{}, 5, false, false, false, SlotRecoveryResult::Empty},
        {"too high", slots({{2, 1}}), 1, false, false, false, SlotRecoveryResult::LevelTooHigh},
        {"over budget", slots({{2, 2}}), 5, false, false, false, SlotRecoveryResult::OverBudget},
        {"above max", slots({{1, 2}}), 5, false, false, false, SlotRecoveryResult::AboveMax},
    };
    for (const Case& tc : cases) {
        CAPTURE(tc.name);
        Party p;
        setup(p, caster("wizard", Json::array({recovery({{"class", "wizard"}, {"max_slot_level", tc.max_level}})})));
        const auto& spec = p.a->slot_recovery[0];
        if (tc.unregistered) p.roster.features.clear();
        if (tc.want == SlotRecoveryResult::AboveMax) p.self().spell_slots[1] = 3;   // 3 of 4: only one 1st-level slot expended
        if (tc.spend_first) REQUIRE(apply_slot_recovery(p.roster, p.id, spec, slots({{1, 1}})) == SlotRecoveryResult::Ok);
        const Slots before = p.self().spell_slots;
        const auto rows_before = p.roster.features;
        auto rest = make_rest_rng(1);
        REQUIRE(short_rest(p.roster, p.id, rest, 0).ok);
        const auto got = apply_slot_recovery(p.roster, tc.unknown_id ? rules5e::CombatantId(9) : p.id, spec, tc.add);
        CHECK(got == tc.want);
        CHECK(p.self().spell_slots == before);
        REQUIRE(p.roster.features.size() == rows_before.size());
        for (std::size_t i = 0; i < rows_before.size(); ++i) CHECK(p.roster.features[i].used == rows_before[i].used);
    }
}

TEST_CASE("slot_recovery: uses deplete across short rests, a long rest restores them (table reset, nothing extra)") {
    Party p;
    setup(p, caster("wizard", Json::array({recovery({{"class", "wizard"}, {"uses", 2}})})));
    const auto& spec = p.a->slot_recovery[0];
    auto rest = make_rest_rng(3);
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{1, 1}})).recovery == SlotRecoveryResult::Ok);
    CHECK(rules5e::feature_uses_left(p.roster, p.id, spec.feature_id) == 1);
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{1, 1}})).recovery == SlotRecoveryResult::Ok);
    CHECK(rules5e::feature_uses_left(p.roster, p.id, spec.feature_id) == 0);
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{2, 1}})).recovery == SlotRecoveryResult::NoUsesLeft);
    CHECK(rules5e::feature_uses_left(p.roster, p.id, spec.feature_id) == 0);   // short rests do not recharge it
    REQUIRE(long_rest(p.roster, p.id).ok);
    CHECK(rules5e::feature_uses_left(p.roster, p.id, spec.feature_id) == 2);
    CHECK(p.self().spell_slots == p.self().spell_slots_max);
    p.self().spell_slots[3] = 0;
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{3, 1}})).recovery == SlotRecoveryResult::Ok);
    CHECK(p.self().spell_slots[3] == 1);
}

TEST_CASE("slot_recovery: druid Natural Recovery") {
    Party p;
    setup(p, caster("druid", Json::array({recovery({{"class", "druid"}})}), 5));   // budget ceil(5/2) = 3
    const auto& spec = p.a->slot_recovery[0];
    CHECK(spec.class_level == 5);
    auto rest = make_rest_rng(11);
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{1, 2}, {2, 1}})).recovery == SlotRecoveryResult::OverBudget);
    CHECK(p.self().spell_slots[1] == 1);
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{3, 1}})).recovery == SlotRecoveryResult::Ok);
    CHECK(p.self().spell_slots[3] == 3);
    CHECK(*short_rest_with_recovery(p.roster, p.id, rest, 0, &spec, slots({{1, 1}})).recovery == SlotRecoveryResult::NoUsesLeft);
    REQUIRE(long_rest(p.roster, p.id).ok);
    CHECK(rules5e::feature_uses_left(p.roster, p.id, spec.feature_id) == 1);
}
