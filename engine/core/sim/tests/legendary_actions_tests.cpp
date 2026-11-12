// Sim side of legendary actions: content load, logged destination, 0xE9 targets, hash 0xE8,
// replay, and the built-in policy passing.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

namespace eng::sim {
struct CombatTestAccess {   // same definition as hash_fold_tests.cpp (ODR)
    static rules5e::EncounterState& state(Combat& c) { return c.state_; }
    static std::vector<LogEntry>& log(Combat& c) { return c.log_; }
    static std::vector<Combat::MaterialUse>& materials(Combat& c) { return c.materials_used_; }
    static std::shared_ptr<const rules5e::SpellBook>& spells(Combat& c) { return c.spells_; }
};
}  // namespace eng::sim

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

void offer(Combat& c, CombatantId beast) {
    for (int i = 0; i < 8 && c.state().legendary_window.candidate != beast && !c.state().ended; ++i)
        REQUIRE(c.submit(end_turn(c)).ok);
    REQUIRE(c.state().legendary_window.candidate == beast);
}

Command stride_to(CombatantId beast, Vec2i to, uint32_t option = 3) {
    Command m;
    m.request = {beast, ActionKind::LegendaryAction};
    m.request.feature_id = option;
    m.to = to;
    return m;
}

void expect_replay(const world::Chunk& ch, const ContentLibrary& l, uint64_t seed, const Combat& c) {
    ecs::Registry reg2;
    std::string err;
    auto again = replay(reg2, ch, l, seed, c.log(), &err);
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c.hash());
    CHECK(again->log_hash() == c.log_hash());
    CHECK(event_stream_hash(again->events()) == event_stream_hash(c.events()));
}

bool attacked(const Combat& c, CombatantId who) {
    for (const auto& e : c.events())
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == who)
            return true;
    return false;
}

world::Chunk walled(std::vector<std::pair<std::string, Vec2i>> spawns, Vec2i wall) {
    auto c = arena(std::move(spawns));
    c.layers[2][std::size_t(wall.y * 32 + wall.x)].flags = world::tile_flags::kBlocksMove;
    c.content_hash = world::compute_content_hash(c);
    return c;
}

// y = 5 is open, plus the fighter's alcove tile. A straight legendary move cannot dodge around.
world::Chunk corridor(Vec2i fighter_at, Vec2i beast_at, const char* beast_id = "test.legendary.beast") {
    auto c = arena({{"srd51.character.fighter_1", fighter_at}, {beast_id, beast_at}});
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            if (y == 5 || (x == fighter_at.x && y == fighter_at.y)) continue;
            c.layers[2][std::size_t(y * 32 + x)].flags = world::tile_flags::kBlocksMove;
        }
    c.content_hash = world::compute_content_hash(c);
    return c;
}

SubmitResult take_reactions(Combat& c) {
    return c.settle([](const Combat&, const NeedsDecision& n) { return ReactionDecision{n.window_seq, true, 0}; });
}

Json frail_beast() {
    Json m = legendary_beast();
    m["id"] = "test.legendary.frail";
    m["hit_points"]["average"] = 1;
    return m;
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
    auto ch = arena({{"srd51.character.fighter_1", {7, 4}}, {"test.legendary.beast", {8, 4}}});
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

    Command bite;
    bite.request = {beast, ActionKind::LegendaryAction};
    bite.request.feature_id = 1;   // the bite, which takes a creature
    bite.request.target = host;
    auto plain = c->submit(bite);
    REQUIRE_MESSAGE(plain.ok, plain.error);
    const uint64_t plain_log = c->log_hash();
    CHECK(c->log().back().to.has_value());   // MUST-1: every LegendaryAction logs a destination tile
    CHECK(c->hash() != before);              // 0xE8: the pool changed (an attack spends 1) or the window moved

    Command listed = bite;
    listed.request.targets = {host};   // 0xE9: a target list on an option that takes creatures
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

// A multi-tile legendary move is one request. Each tile is checked for opportunity attacks and
// zones, so a hostile left in the middle of the path is offered the attack even when the origin
// and the destination are both outside that reach. An interrupt stops on the last tile kept.
// Replay re-derives the path from the logged destination.
TEST_CASE("legendary move provokes and enters zones tile by tile") {
    auto l = lib_with();
    REQUIRE_FALSE(l.add(frail_beast(), "monster"));
    const auto fighter_of = [](const world::Chunk& ch) { return ch.spawns[0].stable_id; };
    const auto beast_of = [](const world::Chunk& ch) { return ch.spawns[1].stable_id; };
    {
        const auto pass = corridor({5, 4}, {2, 5});
        ecs::Registry reg;
        auto c = Combat::create(reg, pass, l, 3);
        REQUIRE(c);
        const auto fighter = fighter_of(pass), beast = beast_of(pass);
        offer(*c, beast);
        auto r = c->submit(stride_to(beast, {8, 5}));
        REQUIRE(r.ok);
        REQUIRE(r.status == StepStatus::NeedsDecision);
        REQUIRE(r.needs);
        CHECK(r.needs->reactor == fighter);
        CHECK(r.needs->trigger == rules5e::ReactionTrigger::LeavingReach);
        CHECK(c->pos(beast) == Vec2i{2, 5});
        auto done = take_reactions(*c);
        REQUIRE_MESSAGE(done.ok, done.error);
        CHECK(attacked(*c, fighter));
        CHECK(c->pos(beast) == Vec2i{8, 5});
        CHECK(c->log().back().to == Vec2i{8, 5});
        expect_replay(pass, l, 3, *c);
    }
    {
        const auto back = walled({{"srd51.character.fighter_1", {5, 4}}, {"test.legendary.beast", {4, 5}}}, {5, 5});
        ecs::Registry reg;
        auto c = Combat::create(reg, back, l, 4);
        REQUIRE(c);
        const auto fighter = fighter_of(back), beast = beast_of(back);
        offer(*c, beast);
        auto r = c->submit(stride_to(beast, {6, 5}));
        REQUIRE(r.ok);
        REQUIRE(r.status == StepStatus::NeedsDecision);
        auto done = take_reactions(*c);
        REQUIRE_MESSAGE(done.ok, done.error);
        CHECK(attacked(*c, fighter));
        CHECK(c->pos(beast) == Vec2i{6, 5});
        expect_replay(back, l, 4, *c);
    }
    {
        const auto glide = corridor({5, 4}, {4, 5});
        ecs::Registry reg;
        auto c = Combat::create(reg, glide, l, 5);
        REQUIRE(c);
        const auto fighter = fighter_of(glide), beast = beast_of(glide);
        offer(*c, beast);
        auto r = c->submit(stride_to(beast, {7, 5}, 4));   // half speed, no opportunity attacks
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(r.status == StepStatus::Done);
        CHECK_FALSE(attacked(*c, fighter));
        CHECK(c->pos(beast) == Vec2i{7, 5});
        expect_replay(glide, l, 5, *c);
    }
    {
        auto zone_lib = lib_with();
        auto* arch = const_cast<Archetype*>(zone_lib.find("srd51.character.fighter_1"));
        REQUIRE(arch);
        rules5e::SpellDef d;
        d.content_id = "test.legendary.enter";
        d.id = rules5e::spell_id_of(d.content_id);
        d.is_spell = false;
        d.kind = rules5e::SpellEffectKind::Zone;
        d.zone.shape = rules5e::AreaShape::Sphere;
        d.zone.anchor = rules5e::ZoneAnchor::Creature;
        d.zone.size_ft = 5;
        rules5e::ZoneTriggerRow row;
        row.on = rules5e::ZoneTrigger::Enter;
        row.completely_within = true;   // zone_contains reads the step tile, not the origin
        row.effect.kind = rules5e::SpellEffectKind::ApplyCondition;
        d.zone.triggers.push_back(row);
        auto book = std::const_pointer_cast<rules5e::SpellBook>(zone_lib.spells());
        REQUIRE(book);
        REQUIRE(book->add(std::move(d)));
        arch->aura_spell_ids.push_back(rules5e::spell_id_of("test.legendary.enter"));
        const auto pass = corridor({5, 4}, {2, 5});
        ecs::Registry reg;
        auto c = Combat::create(reg, pass, zone_lib, 6);
        REQUIRE(c);
        const auto beast = beast_of(pass);
        REQUIRE_FALSE(c->state().zones.empty());
        offer(*c, beast);
        auto r = c->submit(stride_to(beast, {8, 5}));
        REQUIRE(r.ok);
        if (r.status == StepStatus::NeedsDecision) {
            auto done = take_reactions(*c);
            REQUIRE_MESSAGE(done.ok, done.error);
        }
        CHECK(c->pos(beast) == Vec2i{8, 5});
        bool entered = false;
        for (const auto& e : c->events())
            entered = entered || (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::FeatureTriggered &&
                                  e.rules.target == beast && e.rules.a == int32_t(rules5e::spell_id_of("test.legendary.enter")));
        CHECK(entered);
        expect_replay(pass, zone_lib, 6, *c);
    }
    {
        const auto pass = corridor({5, 4}, {2, 5}, "test.legendary.frail");
        bool dropped = false;
        for (uint64_t seed = 1; seed <= 40 && !dropped; ++seed) {
            ecs::Registry reg;
            auto c = Combat::create(reg, pass, l, seed);
            REQUIRE(c);
            const auto beast = beast_of(pass);
            offer(*c, beast);
            auto r = c->submit(stride_to(beast, {8, 5}));
            REQUIRE(r.ok);
            if (r.status == StepStatus::NeedsDecision) {
                auto done = take_reactions(*c);
                REQUIRE_MESSAGE(done.ok, done.error);
            }
            if (rules5e::find(c->state(), beast)->hp > 0) continue;
            dropped = true;
            CHECK(c->pos(beast) == Vec2i{6, 5});
            CHECK(c->log().back().to == Vec2i{8, 5});
            expect_replay(pass, l, seed, *c);
        }
        CHECK(dropped);
    }
}

// The origin is around a corner: total cover from the fighter. The tile the move leaves is beside
// the fighter, with no cover. The opportunity attack must be offered from that tile.
TEST_CASE("legendary opportunity attack uses cover from the tile being left") {
    auto l = lib_with();
    // A two-tile-thick corner between the origin (1,5) and the fighter (4,6). The walk goes around
    // it and leaves the fighter's reach from (5,6), which the wall does not cover.
    auto ch = arena({{"srd51.character.fighter_1", {4, 6}}, {"test.legendary.beast", {1, 5}}});
    for (int y = 5; y <= 6; ++y)
        for (int x = 2; x <= 3; ++x)
            ch.layers[2][std::size_t(y * 32 + x)].flags = world::tile_flags::kBlocksMove | world::tile_flags::kBlocksSight;
    ch.content_hash = world::compute_content_hash(ch);
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 7);
    REQUIRE(c);
    const auto fighter = ch.spawns[0].stable_id, beast = ch.spawns[1].stable_id;
    rules5e::CoverQuery q{fighter, beast};
    CHECK(c->cover_of(q) == rules5e::Cover::Total);
    offer(*c, beast);
    auto r = c->submit(stride_to(beast, {6, 6}));
    REQUIRE(r.ok);
    REQUIRE(r.status == StepStatus::NeedsDecision);
    REQUIRE(r.needs);
    CHECK(r.needs->reactor == fighter);
    CHECK(r.needs->trigger == rules5e::ReactionTrigger::LeavingReach);
    CHECK(c->pos(beast) == Vec2i{1, 5});
    auto done = take_reactions(*c);
    REQUIRE_MESSAGE(done.ok, done.error);
    CHECK(attacked(*c, fighter));
    CHECK(c->pos(beast) == Vec2i{6, 6});
    expect_replay(ch, l, 7, *c);
}

// Polymorph replaces the creature's game statistics, so the legendary pool and Legendary Resistance are gone
// until the spell ends. la_cur comes back as it was; the turn-start refill does not run while the pool is suspended.
TEST_CASE("polymorph suspends a legendary creature; revert restores the pool; replay matches") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    Json boss = legendary_beast();
    boss["id"] = "test.legendary.poly";
    boss["resources"] = Json::array({
        {{"id", "legendary_resistance"}, {"name", "Legendary Resistance"}, {"max", 3}, {"recharge", "never"}},
    });
    REQUIRE_FALSE(l.add(boss, "monster"));
    Json mouse = load_json("content/packs/srd51.core/monsters/goblin.json");
    mouse["id"] = "test.legendary.mouse";
    mouse["type"] = "beast";
    mouse["challenge"] = {{"rating", "0"}, {"xp", 10}};
    mouse["hit_points"]["average"] = 40;
    REQUIRE_FALSE(l.add(mouse, "monster"));
    Json poly = {{"$schema", "spell@1"}, {"id", "test.spell.poly"}, {"name", "Poly"}, {"level", 1},
                 {"school", "transmutation"}, {"casting_time", {{"kind", "action"}}},
                 {"range", {{"kind", "ranged"}, {"feet", 60}}}, {"components", {{"v", true}, {"s", true}}},
                 {"duration", {{"kind", "hours"}, {"amount", 1}, {"concentration", true}}},
                 {"effects", Json::array({{{"kind", "polymorph"}, {"forms", {"test.legendary.mouse"}},
                                           {"max_cr", "0"}, {"save", {{"ability", "wis"}}}}})}};
    Json zap = {{"$schema", "spell@1"}, {"id", "test.spell.zap"}, {"name", "zap"}, {"level", 0},
                {"school", "evocation"}, {"casting_time", {{"kind", "action"}}},
                {"range", {{"kind", "ranged"}, {"feet", 60}}}, {"components", {{"v", true}, {"s", true}}},
                {"duration", {{"kind", "instantaneous"}}},
                {"effects", Json::array({{{"kind", "save"}, {"ability", "wis"}, {"on_success", "none"},
                                          {"damage", {{"dice", "1d4"}, {"type", "fire"}}}}})}};
    REQUIRE_FALSE(l.add_spell(poly));
    REQUIRE_FALSE(l.add_spell(zap));
    Json wiz_doc = load_json("content/packs/srd51.core/characters/wizard_level_1.json");
    wiz_doc["id"] = "test.character.polywiz";
    wiz_doc["spellcasting"]["dc"] = 30;   // the beast's Wisdom 8 cannot succeed
    wiz_doc["spellcasting"]["spells"].push_back("test.spell.poly");
    wiz_doc["spellcasting"]["spells"].push_back("test.spell.zap");
    REQUIRE_FALSE(l.add(wiz_doc, "character"));

    const uint32_t lr = rules5e::spell_id_of("legendary_resistance");
    auto settle = [](Combat& c, SubmitResult r) {
        while (r.ok && r.needs) r = c.decide(rules5e::ReactionDecision{r.needs->window_seq, false, 0});
        return r;
    };
    bool ran = false;
    std::string why;
    for (uint64_t seed = 1; seed <= 24 && !ran; ++seed) {
        auto ch = arena({{"test.character.polywiz", {2, 2}},
                         {"test.character.polywiz", {4, 2}},
                         {"test.legendary.poly", {3, 2}}});
        ch.spawns[1].overrides_json = "{\"team\":\"monsters\"}";   // the zap caster
        ch.spawns[2].overrides_json = "{\"team\":\"party\"}";      // willing: Polymorph applies without a save
        ch.content_hash = world::compute_content_hash(ch);
        ecs::Registry reg;
        auto fight = Combat::create(reg, ch, l, seed);
        REQUIRE(fight);
        Combat& c = *fight;
        const auto wiz = ch.spawns[0].stable_id, foe = ch.spawns[1].stable_id, beast = ch.spawns[2].stable_id;
        REQUIRE(rules5e::find(c.state(), beast)->la_max == 3);
        REQUIRE(rules5e::find(c.state(), beast)->legendary_pool == lr);
        REQUIRE(rules5e::find_pool(c.state(), beast, lr)->cur == 3);
        auto next_after = [&](CombatantId x) {
            const auto& o = c.state().order;
            for (std::size_t i = 0; i < o.size(); ++i)
                if (o[i].id == x) return o[(i + 1) % o.size()].id;
            return CombatantId{0};
        };
        bool spent = false;
        for (int guard = 0; guard < 16 && rules5e::current_actor(c.state()) != wiz && !c.ended(); ++guard) {
            if (c.state().legendary_window.x != 0) {
                const auto cand = c.state().legendary_window.candidate;
                const auto opener = c.state().legendary_window.x;
                if (!spent && cand == beast && next_after(opener) == wiz) {
                    Command sniff;
                    sniff.request = {beast, ActionKind::LegendaryAction};
                    sniff.request.feature_id = 2;
                    auto u = settle(c, c.submit(sniff));
                    if (!u.ok) { why = std::string(u.error); break; }
                    spent = rules5e::find(c.state(), beast)->la_cur == 2;
                    continue;
                }
                Command pass;
                pass.request = {cand, ActionKind::LegendaryAction};
                auto u = settle(c, c.submit(pass));
                if (!u.ok) { why = std::string(u.error); break; }
            } else {
                auto u = settle(c, c.submit(end_turn(c)));
                if (!u.ok) { why = std::string(u.error); break; }
            }
        }
        if (!spent || rules5e::current_actor(c.state()) != wiz) continue;

        int form_index = -1;
        const auto& forms = c.polymorph_forms();
        for (int i = 0; i < int(forms.size()); ++i)
            if (forms[std::size_t(i)]->id == "test.legendary.mouse") form_index = i;
        REQUIRE(form_index >= 0);
        REQUIRE(form_index < 255);
        std::string form_why;
        REQUIRE_MESSAGE(c.build_form(*rules5e::find(c.state(), beast), rules5e::FormKind::Polymorph,
                                     rules5e::spell_id_of("test.spell.poly"), uint8_t(form_index), &form_why),
                        form_why);
        Command cast;
        cast.request = {wiz, ActionKind::CastSpell, beast};
        cast.request.spell_id = rules5e::spell_id_of("test.spell.poly");
        cast.request.spell_level = 1;
        cast.request.form_archetype = uint8_t(form_index + 1);
        cast.request.targets = {beast};
        auto shaped = settle(c, c.submit(cast));
        REQUIRE_MESSAGE(shaped.ok, shaped.error);
        REQUIRE(rules5e::find(c.state(), beast)->form == rules5e::FormKind::Polymorph);
        CHECK(rules5e::find(c.state(), beast)->la_max == 0);
        CHECK(rules5e::find(c.state(), beast)->legendary.empty());
        CHECK(rules5e::find(c.state(), beast)->legendary_pool == 0);

        const int form_hp = rules5e::find(c.state(), beast)->hp;
        auto back = settle(c, c.submit(end_turn(c)));
        REQUIRE_MESSAGE(back.ok, back.error);
        CHECK(c.state().legendary_window.x == 0);
        CHECK(c.state().legendary_window.candidate != beast);
        for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != foe && !c.ended(); ++i) {
            CHECK(c.state().legendary_window.x == 0);
            auto u = settle(c, c.submit(end_turn(c)));
            REQUIRE_MESSAGE(u.ok, u.error);
        }
        REQUIRE(rules5e::current_actor(c.state()) == foe);
        CHECK(rules5e::find(c.state(), beast)->form == rules5e::FormKind::Polymorph);

        Command blast;
        blast.request = {foe, ActionKind::CastSpell, beast};
        blast.request.spell_id = rules5e::spell_id_of("test.spell.zap");
        blast.request.targets = {beast};
        auto saved = settle(c, c.submit(blast));
        REQUIRE_MESSAGE(saved.ok, saved.error);
        int spent_lr = 0;
        for (const auto& e : c.events())
            if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::ResourceSpent && e.rules.a == int32_t(lr))
                ++spent_lr;
        CHECK(spent_lr == 0);
        CHECK(rules5e::find_pool(c.state(), beast, lr)->cur == 3);
        CHECK(rules5e::find(c.state(), beast)->hp < form_hp);
        CHECK(rules5e::find(c.state(), beast)->legendary_pool == 0);
        CHECK(rules5e::find(c.state(), beast)->form == rules5e::FormKind::Polymorph);

        Command drop;
        drop.request = {wiz, ActionKind::EndConcentration};
        auto reverted = settle(c, c.submit(drop));
        REQUIRE_MESSAGE(reverted.ok, reverted.error);
        CHECK(rules5e::find(c.state(), beast)->form == rules5e::FormKind::None);
        CHECK(rules5e::find(c.state(), beast)->la_max == 3);
        CHECK(rules5e::find(c.state(), beast)->la_cur == 2);
        CHECK(rules5e::find(c.state(), beast)->legendary.size() == 4);
        CHECK(rules5e::find(c.state(), beast)->legendary_pool == lr);
        CHECK(rules5e::find_pool(c.state(), beast, lr)->cur == 3);
        expect_replay(ch, l, seed, c);
        ran = true;
    }
    REQUIRE_MESSAGE(ran, why);
}

// Pinned before the suspended section existed. The live 0xE8 bytes are unchanged, and the trailing
// section is omitted unless someone is polymorphed, so these digests stay put.
TEST_CASE("0xE8 folds a suspended legendary pool; a non-polymorph hash stays pinned") {
    auto l = lib_with();
    auto ch = arena({{"srd51.character.fighter_1", {7, 4}}, {"test.legendary.beast", {8, 4}}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 11);
    REQUIRE(c);
    CHECK(c->hash() == 0x40c5e7a2531bae06ull);

    ContentLibrary plain;
    REQUIRE(plain.load(sample_set()).empty());
    auto ch2 = arena({{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {8, 4}}});
    ecs::Registry reg2;
    auto plain_fight = Combat::create(reg2, ch2, plain, 4);
    REQUIRE(plain_fight);
    REQUIRE(plain_fight->submit(end_turn(*plain_fight)).ok);
    CHECK(plain_fight->hash() == 0x67bca9677135717dull);

    const auto beast = ch.spawns[1].stable_id;
    rules5e::Combatant* boss = nullptr;
    for (auto& k : CombatTestAccess::state(*c).combatants)
        if (k.id == beast) boss = &k;
    REQUIRE(boss);
    rules5e::FormStats form;
    form.hp_max = 22;
    form.ac = 13;
    form.abilities = boss->abilities;
    form.speed_ft = boss->speed_ft;
    form.attacks = boss->attacks;
    form.creature_type = rules5e::CreatureType::Beast;
    REQUIRE(rules5e::apply_form(*boss, rules5e::FormKind::Polymorph, form));
    REQUIRE(boss->la_suspended);
    REQUIRE(boss->la_max == 0);
    REQUIRE(boss->la_saved_cur > 0);
    const uint64_t suspended = c->hash();
    CHECK(suspended != 0x40c5e7a2531bae06ull);   // the only legendary creature is suspended, and 0xE8 is still emitted

    boss->la_saved_cur = uint8_t(boss->la_saved_cur - 1);
    const uint64_t fewer = c->hash();
    CHECK(fewer != suspended);                   // the saved count is the only difference
    boss->la_saved_cur = uint8_t(boss->la_saved_cur + 1);
    CHECK(c->hash() == suspended);

    REQUIRE_FALSE(boss->la_saved.empty());
    boss->la_saved[0].cost = uint8_t(boss->la_saved[0].cost + 1);
    CHECK(c->hash() != suspended);               // option identity
    boss->la_saved[0].cost = uint8_t(boss->la_saved[0].cost - 1);
    CHECK(c->hash() == suspended);

    const uint32_t pool = boss->la_saved_pool;
    boss->la_saved_pool = pool + 1;
    CHECK(c->hash() != suspended);
    boss->la_saved_pool = pool;
    CHECK(c->hash() == suspended);
}

// A Polymorph of a creature with no legendary pool and no Legendary Resistance must not emit 0xE8.
TEST_CASE("polymorph of a non-legendary creature does not fold 0xE8") {
    ContentLibrary plain;
    REQUIRE(plain.load(sample_set()).empty());
    auto ch = arena({{"srd51.character.fighter_1", {4, 4}}, {"srd51.monster.goblin", {8, 4}}});
    ecs::Registry reg;
    auto fight = Combat::create(reg, ch, plain, 4);
    REQUIRE(fight);
    const auto goblin = ch.spawns[1].stable_id;
    rules5e::Combatant* g = nullptr;
    for (auto& k : CombatTestAccess::state(*fight).combatants)
        if (k.id == goblin) g = &k;
    REQUIRE(g);
    CHECK(g->la_max == 0);
    CHECK(g->legendary.empty());
    CHECK(g->legendary_pool == 0);
    rules5e::FormStats form;
    form.hp_max = 11;
    form.ac = 12;
    form.abilities = g->abilities;
    form.speed_ft = g->speed_ft;
    form.attacks = g->attacks;
    form.creature_type = rules5e::CreatureType::Beast;
    REQUIRE(rules5e::apply_form(*g, rules5e::FormKind::Polymorph, form));
    CHECK(g->form == rules5e::FormKind::Polymorph);
    CHECK_FALSE(g->la_suspended);
    const uint64_t hashed = fight->hash();
    g->la_suspended = false;
    g->la_saved_max = 0;
    g->la_saved_cur = 0;
    g->la_saved_pool = 0;
    g->la_saved.clear();
    CHECK(fight->hash() == hashed);   // clearing the saved fields does not move the digest
    g->la_suspended = true;            // the empty flag would emit 0xE8
    CHECK(fight->hash() != hashed);
    g->la_suspended = false;
    CHECK(fight->hash() == hashed);
}
