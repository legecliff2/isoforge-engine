// Untimed prone on on_hit save_condition riders (SRD 5.1 wolf / dire wolf Bite; T&C #130). A save_condition rider may leave out
// the duration only for a condition that ends by its own rules (content.cpp kEndsByOwnRules: prone). It is applied the way
// Shove applies prone: the condition bit only, no ActiveSpellEffect / timed entry. Prone is existing condition state: no new
// hash tag, goldens unchanged.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";
const char* kUnsupported = ": save_condition uses an unsupported duration";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// SRD wolf Bite rider: "DC 11 Strength saving throw or be knocked prone", authored with no duration.
Json knock_prone(int dc, Json apply = Json::array({{{"condition", "prone"}}})) {
    return {{"kind", "save_condition"}, {"save", {{"ability", "str"}, {"dc", dc}}}, {"apply", apply}};
}
// The sample goblin as a monster@4 "wolf": its first attack (scimitar) carries the rider and always hits.
Json wolf(const std::string& id, Json rider) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["hit_points"]["average"] = 400;
    m["actions"][0]["to_hit"] = 30;
    m["actions"][0]["on_hit"] = Json::array({rider});
    return m;
}
ContentLibrary base_lib() {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    Json tank = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    tank["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(tank, "character"));
    return lib;
}
const rules5e::SpellDef* rider_def(const ContentLibrary& lib, const std::string& id) {
    const auto* a = lib.find(id);
    REQUIRE(a);
    REQUIRE(a->base.triggers.size() == 1);
    REQUIRE(a->base.triggers[0].effects.size() == 1);
    CHECK(a->base.triggers[0].effects[0].kind == rules5e::RiderKind::SaveCondition);
    return lib.spells()->find(a->base.triggers[0].effects[0].spell_id);
}
world::Chunk arena(std::vector<std::pair<std::string, Vec2i>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].first, spawns[i].second, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
void to_actor(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
}  // namespace

TEST_CASE("prone rider loader: an untimed prone save_condition rider loads and compiles untracked") {
    auto lib = base_lib();
    const auto e = lib.add(wolf("t.wolf", knock_prone(11)), "monster");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    const auto* d = rider_def(lib, "t.wolf");
    REQUIRE(d);
    CHECK(d->kind == rules5e::SpellEffectKind::Save);
    CHECK(d->fixed_dc == 11);
    CHECK(d->save_ability == rules5e::Ability::Str);
    CHECK(d->applies_condition);
    CHECK(d->condition == Condition::Prone);
    CHECK(d->expiry == 0);
    CHECK(d->repeat_save == rules5e::RepeatSave::None);
    CHECK(d->condition_untracked);
    CHECK(lib.find("t.wolf")->unsupported_features.empty());

    // a timed prone rider is still accepted as before: a timed effect, tracked
    REQUIRE_FALSE(lib.add(wolf("t.wolf_timed", knock_prone(11, Json::array({{{"condition", "prone"},
                                                   {"duration", {{"kind", "rounds"}, {"n", 1}}}}}))), "monster"));
    const auto* td = rider_def(lib, "t.wolf_timed");
    REQUIRE(td);
    CHECK(td->expiry == uint8_t(rules5e::Expiry::Rounds));
    CHECK_FALSE(td->condition_untracked);

    // untimed prone as a further apply[] entry: an untracked ApplyCondition extra effect
    REQUIRE_FALSE(lib.add(wolf("t.wolf_extra", knock_prone(11, Json::array({{{"condition", "poisoned"},
                                                   {"duration", {{"kind", "rounds"}, {"n", 1}}}}, {{"condition", "prone"}}}))), "monster"));
    const auto* xd = rider_def(lib, "t.wolf_extra");
    REQUIRE(xd);
    CHECK_FALSE(xd->condition_untracked);
    REQUIRE(xd->extra_effects.size() == 1);
    CHECK(xd->extra_effects[0].condition == Condition::Prone);
    CHECK(xd->extra_effects[0].condition_untracked);
}

TEST_CASE("prone rider loader: every other untimed condition is still rejected with the same error") {
    auto lib = base_lib();
    for (const char* cond : {"restrained", "poisoned", "grappled", "unconscious"}) {
        const auto e = lib.add(wolf(std::string("t.bad_") + cond, knock_prone(11, Json::array({{{"condition", cond}}}))), "monster");
        REQUIRE_MESSAGE(e, cond);
        CHECK(*e == std::string("t.bad_") + cond + ": scimitar on_hit" + kUnsupported);
        CHECK_FALSE(lib.find(std::string("t.bad_") + cond));
    }
    // an untimed restrained alongside an untimed prone: still rejected
    const auto e2 = lib.add(wolf("t.bad_mix", knock_prone(11, Json::array({{{"condition", "prone"}}, {{"condition", "restrained"}}}))), "monster");
    REQUIRE(e2);
    CHECK(e2->find(kUnsupported) != std::string::npos);
    // a bad duration object on prone is not "no duration": still rejected
    const auto e3 = lib.add(wolf("t.bad_kind", knock_prone(11, Json::array({{{"condition", "prone"}, {"duration", {{"kind", "forever"}}}}}))), "monster");
    REQUIRE(e3);
    CHECK(e3->find(kUnsupported) != std::string::npos);
}

TEST_CASE("prone rider loader: a save_effect action with an untimed prone stays listed, not compiled (unchanged)") {
    auto lib = base_lib();
    Json m = wolf("t.wing", knock_prone(11));
    m["actions"][0].erase("on_hit");
    m["actions"].push_back({{"id", "wing"}, {"name", "Wing Attack"}, {"kind", "save_effect"}, {"save", {{"ability", "dex"}, {"dc", 11}}},
                            {"damage", Json::array({{{"dice", "1d6"}, {"type", "bludgeoning"}}})},
                            {"apply", Json::array({{{"condition", "prone"}}})}});
    REQUIRE_FALSE(lib.add(m, "monster"));
    CHECK(lib.find("t.wing")->base.triggers.empty());
    CHECK_FALSE(lib.find("t.wing")->unsupported_features.empty());
}

TEST_CASE("prone rider combat: a hit and a failed save knock the target prone untimed; a passed save does not; both replay") {
    auto lib = base_lib();
    REQUIRE_FALSE(lib.add(wolf("t.wolf_fail", knock_prone(30)), "monster"));   // the fighter cannot make DC 30
    REQUIRE_FALSE(lib.add(wolf("t.wolf_pass", knock_prone(1)), "monster"));    // nor fail DC 1
    for (const bool fail : {true, false}) {
        CAPTURE(fail);
        auto ch = arena({{fail ? "t.wolf_fail" : "t.wolf_pass", Vec2i{6, 5}}, {kFighter, Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId w = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        to_actor(*c, w);
        const std::size_t ev0 = c->events().size();
        Command cmd;
        cmd.request = {w, ActionKind::Attack, f, 0};
        const auto r = c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        c->settle(nullptr);
        const auto* fk = rules5e::find(c->state(), f);
        CHECK(fk->hp < fk->hp_max);   // the bite hit
        int saves = 0, prone_added = 0;
        for (std::size_t i = ev0; i < c->events().size(); ++i) {
            const auto& e = c->events()[i];
            if (e.kind != SimEvent::Kind::Rules) continue;
            if (e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == f) {
                ++saves;
                CHECK(bool(e.rules.c) == !fail);
            }
            if (e.rules.kind == EventKind::ConditionAdded && e.rules.actor == f && e.rules.a == int32_t(Condition::Prone)) ++prone_added;
        }
        CHECK(saves == 1);
        CHECK(fk->has(Condition::Prone) == fail);
        CHECK(prone_added == (fail ? 1 : 0));
        // untimed: no lasting-effect entry at all (as for Shove), so nothing for 0xA5 to fold
        CHECK(c->state().effects.empty());
        CHECK_FALSE(rules5e::has_timed_effects(c->state()));
        // a second bite on the prone target adds no second ConditionAdded and no entry
        if (fail) {
            Command e;
            e.request = {w, ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
            to_actor(*c, w);
            Command again;
            again.request = {w, ActionKind::Attack, f, 0};
            REQUIRE(c->submit(again).ok);
            c->settle(nullptr);
            CHECK(rules5e::find(c->state(), f)->has(Condition::Prone));
            CHECK(c->state().effects.empty());
        }
        ecs::Registry reg2;
        std::string err;
        auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
        REQUIRE_MESSAGE(rp, err);
        CHECK(rp->hash() == c->hash());
        CHECK(rp->log_hash() == c->log_hash());
        CHECK(rules5e::find(rp->state(), f)->has(Condition::Prone) == fail);
    }
}

// SRD 5.1 Gladiator Shield Bash: "If the target is a Medium or smaller creature, it must succeed on a DC 15 Strength saving
// throw or be knocked prone." A save_condition rider's first apply[] entry may carry max_size without contain: a larger target
// makes no save and is not knocked prone, but the hit's damage stands. No new state: no hash tag, goldens unchanged.
TEST_CASE("prone rider size gate: max_size on a save_condition rider (Shield Bash, Medium or smaller)") {
    auto lib = base_lib();
    Json big = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    big["id"] = "t.big_fighter";
    big["size"] = "large";
    big["hit_points"]["average"] = 2000;
    REQUIRE_FALSE(lib.add(big, "character"));
    const Json gated = Json::array({{{"condition", "prone"}, {"max_size", "medium"}}});
    REQUIRE_FALSE(lib.add(wolf("t.bash_fail", knock_prone(30, gated)), "monster"));
    REQUIRE_FALSE(lib.add(wolf("t.bash_pass", knock_prone(1, gated)), "monster"));
    const auto* d = rider_def(lib, "t.bash_fail");
    REQUIRE(d);
    CHECK(d->condition_max_size == int8_t(rules5e::Size::Medium));
    CHECK(d->contain == 0);
    // the gate belongs to the whole save: on a later apply[] entry it is a load error
    const auto e = lib.add(wolf("t.bash_late", knock_prone(11, Json::array({{{"condition", "prone"}},
                                                                             {{"condition", "prone"}, {"max_size", "medium"}}}))), "monster");
    REQUIRE(e);
    CHECK(*e == "t.bash_late: save_effect 'save_condition.0': max_size on a save_condition rider goes on the first apply[] entry");

    struct Case { const char* mon; const char* target; bool large; bool fail; };
    for (const Case k : {Case{"t.bash_fail", "t.big_fighter", true, true}, Case{"t.bash_pass", "t.big_fighter", true, false},
                         Case{"t.bash_fail", kFighter, false, true}, Case{"t.bash_pass", kFighter, false, false}}) {
        CAPTURE(k.large); CAPTURE(k.fail);
        auto ch = arena({{k.mon, Vec2i{6, 5}}, {k.target, k.large ? Vec2i{4, 5} : Vec2i{5, 5}}});
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 7);
        REQUIRE(c);
        const CombatantId w = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
        REQUIRE(rules5e::find(c->state(), f)->size == (k.large ? rules5e::Size::Large : rules5e::Size::Medium));
        to_actor(*c, w);
        const std::size_t ev0 = c->events().size();
        Command cmd;
        cmd.request = {w, ActionKind::Attack, f, 0};
        const auto r = c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        c->settle(nullptr);
        const auto* fk = rules5e::find(c->state(), f);
        CHECK(fk->hp < fk->hp_max);   // the hit's damage applies either way
        int saves = 0;
        for (std::size_t i = ev0; i < c->events().size(); ++i) {
            const auto& ev = c->events()[i];
            if (ev.kind == SimEvent::Kind::Rules && ev.rules.kind == EventKind::SavingThrowRolled && ev.rules.actor == f) ++saves;
        }
        CHECK(saves == (k.large ? 0 : 1));
        CHECK(fk->has(Condition::Prone) == (!k.large && k.fail));
        ecs::Registry reg2;
        std::string err;
        auto rp = replay(reg2, ch, lib, 7, c->log(), &err);
        REQUIRE_MESSAGE(rp, err);
        CHECK(rp->hash() == c->hash());
    }
}
