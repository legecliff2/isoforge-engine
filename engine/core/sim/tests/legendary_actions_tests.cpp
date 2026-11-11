// Sim side of legendary actions: content load, logged destination, 0xE9 targets, hash 0xE8,
// replay, and the built-in policy passing.
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
using rules5e::EventKind;

namespace {

Json load_json(const std::string& rel) {
    std::ifstream f(std::string(ENGINE_SOURCE_ROOT) + "/" + rel);
    REQUIRE(f.good());
    return assets::Json::parse(f);
}

Json legendary_beast() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@7";
    m["id"] = "test.legendary.beast";
    m["type"] = "beast";
    m["challenge"] = {{"rating", "0"}, {"xp", 10}};
    m["name"] = "Legendary Beast";
    m["hit_points"]["average"] = 80;
    m["legendary_actions_per_round"] = 3;
    m["legendary_actions"] = Json::array({
        {{"id", "bite"}, {"name", "Bite"}, {"description", "One bite."}, {"cost", 1},
         {"use", {{"attack", "scimitar"}}}},
        {{"id", "sniff"}, {"name", "Sniff"}, {"description", "A Perception check."},
         {"use", {{"check", "perception"}}}},
        {{"id", "stride"}, {"name", "Stride"}, {"description", "Move up to its speed."},
         {"use", {{"move", "speed"}}}},
        {{"id", "glide"}, {"name", "Glide"}, {"description", "Move without provoking."},
         {"use", {{"move", "half_speed"}, {"no_opportunity_attacks", true}}}},
    });
    return m;
}

Json plain_goblin() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["id"] = "test.legendary.host";
    m["hit_points"]["average"] = 40;
    return m;
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

ContentLibrary lib_with() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(legendary_beast(), "monster"));
    REQUIRE_FALSE(l.add(plain_goblin(), "monster"));
    return l;
}

Command end_turn(const Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    return e;
}

}  // namespace

TEST_CASE("monster@7 loads legendary actions; a descriptive list and a form do not") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    auto described = load_json("content/packs/srd51.core/monsters/goblin.json");
    described["legendary_actions"] = Json::array({
        {{"id", "detect"}, {"name", "Detect"}, {"kind", "special"}, {"description", "text only"}},
    });
    REQUIRE_FALSE(l.add(described, "monster"));
    const Archetype* old = l.find("srd51.monster.goblin");
    REQUIRE(old);
    CHECK(old->base.la_max == 0);
    CHECK(old->base.legendary.empty());

    REQUIRE_FALSE(l.add(legendary_beast(), "monster"));
    const Archetype* b = l.find("test.legendary.beast");
    REQUIRE(b);
    CHECK(b->base.la_max == 3);
    CHECK(b->base.la_cur == 3);
    REQUIRE(b->base.legendary.size() == 4);
    CHECK(b->base.legendary[0].use == rules5e::LegendaryUse::Attack);
    CHECK(b->base.legendary[2].use == rules5e::LegendaryUse::Move);
    CHECK_FALSE(b->base.legendary[2].half_speed);
    CHECK(b->base.legendary[3].no_opportunity_attacks);
    CHECK(b->base.legendary[3].half_speed);
    CHECK(b->base.creature_type == rules5e::CreatureType::Beast);
    CHECK(b->cr_x8 == 0);

    REQUIRE_FALSE(l.add(plain_goblin(), "monster"));
    auto ch = arena({{"srd51.character.fighter_1", {4, 4}}, {"test.legendary.beast", {8, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    const auto host = ch.spawns[0].stable_id, beast = ch.spawns[1].stable_id;
    CHECK(rules5e::find(c->state(), beast)->la_max == 3);
    std::optional<rules5e::FormStats> form;
    std::string why;
    for (int i = 0; i < 255 && !form; ++i) {
        auto f = c->build_form(*rules5e::find(c->state(), host), rules5e::FormKind::Polymorph, 0, uint8_t(i), &why);
        if (f && !f->attacks.empty() && f->attacks[0].id == "scimitar" && f->hp_max == 80) form = std::move(f);
    }
    REQUIRE_MESSAGE(form, why);
    rules5e::Combatant shaped = *rules5e::find(c->state(), host);
    REQUIRE(rules5e::apply_form(shaped, rules5e::FormKind::Polymorph, *form));
    CHECK(shaped.la_max == 0);
    CHECK(shaped.legendary.empty());
}

TEST_CASE("legendary move logs the destination, creature targets fold under 0xE9, and replay matches") {
    auto l = lib_with();
    auto ch = arena({{"srd51.character.fighter_1", {4, 4}}, {"test.legendary.beast", {8, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 11);
    REQUIRE(c);
    const auto host = ch.spawns[0].stable_id, beast = ch.spawns[1].stable_id;
    const uint64_t before = c->hash();
    // No legendary state in a fight that has it IS hashed, so the hash differs from a pool-less fight.
    CHECK(rules5e::find(c->state(), beast)->la_max == 3);
    while (rules5e::current_actor(c->state()) != host) REQUIRE(c->submit(end_turn(*c)).ok);
    REQUIRE(c->submit(end_turn(*c)).ok);
    REQUIRE(c->state().legendary_window.candidate == beast);

    Command sniff;
    sniff.request = {beast, ActionKind::LegendaryAction};
    sniff.request.feature_id = 2;   // perception, no creature list
    auto plain = c->submit(sniff);
    REQUIRE_MESSAGE(plain.ok, plain.error);
    const uint64_t plain_log = c->log_hash();
    CHECK(c->log().back().to.has_value());   // MUST-1: every LegendaryAction logs a destination tile
    CHECK(c->hash() != before);              // 0xE8: the pool changed (a check spends 1) or the window moved

    Command listed = sniff;
    listed.request.targets = {host};
    auto with_list = c->submit(listed);
    REQUIRE_MESSAGE(with_list.ok, with_list.error);
    CHECK(c->log_hash() != plain_log);   // 0xE9 folds the creature list

    Command stride;
    stride.request = {beast, ActionKind::LegendaryAction};
    stride.request.feature_id = 3;
    stride.to = {8, 5};
    auto moved = c->submit(stride);
    REQUIRE_MESSAGE(moved.ok, moved.error);
    REQUIRE(c->log().back().to);
    CHECK(*c->log().back().to == Vec2i{8, 5});
    CHECK(c->pos(beast) == Vec2i{8, 5});

    ecs::Registry reg2;
    std::string err;
    auto again = replay(reg2, ch, l, 11, c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c->hash());
    CHECK(again->log_hash() == c->log_hash());
    CHECK(event_stream_hash(again->events()) == event_stream_hash(c->events()));
    CHECK(event_stream_version(c->events()) == 13);
}

TEST_CASE("a fight with no legendary content does not hash 0xE8 and is not event version 13") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    auto ch = arena({{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {8, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 4);
    REQUIRE(c);
    REQUIRE(c->submit(end_turn(*c)).ok);
    CHECK(c->state().legendary_window.x == 0);
    CHECK(event_stream_version(c->events()) != 13);
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::Rules)
            CHECK(uint8_t(e.rules.kind) < 53);
    ecs::Registry reg2;
    auto again = replay(reg2, ch, l, 4, c->log());
    REQUIRE(again);
    CHECK(again->hash() == c->hash());
    CHECK(again->log_hash() == c->log_hash());
}

TEST_CASE("run_to_end passes legendary actions") {
    auto l = lib_with();
    auto ch = arena({{"srd51.character.fighter_1", {4, 4}}, {"test.legendary.beast", {7, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 9);
    REQUIRE(c);
    const auto beast = ch.spawns[1].stable_id;
    NearestEnemyPolicy pol;
    const auto rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &pol; }, 80);
    CHECK(rr.commands > 0);
    for (const auto& e : c->events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::LegendaryActionUsed)
            FAIL("the built-in policy spent a legendary action");
    if (const auto* b = rules5e::find(c->state(), beast)) CHECK(b->la_cur == b->la_max);
    ecs::Registry reg2;
    std::string err;
    auto again = replay(reg2, ch, l, 9, c->log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c->hash());
    CHECK(again->log_hash() == c->log_hash());
}
