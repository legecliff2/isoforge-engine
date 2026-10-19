// R-G16 sim wiring (PR #108, VERSION_BUMP_BATCH 7.2h hand-off): the character@N hit_die loader, Combat::apply_surprise
// with the 0xA8 Combat::hash fold, replay of a surprised fight, and the between-encounter rest entry points on their own
// PRNG stream.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "rules5e/conditions.hpp"
#include "sim/rest.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using eng::assets::Json;

namespace {
Json load_json(const char* rel) { return Json::parse(std::ifstream(std::filesystem::path(ENGINE_SOURCE_ROOT) / rel)); }

world::Chunk arena(const char* who0, const char* who1) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    const char* who[] = {who0, who1};
    for (std::size_t i = 0; i < 2; ++i)
        c.spawns.push_back({who[i], Vec2i{int(5 + 3 * i), 5}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
}  // namespace

TEST_CASE("loader: character@N hit_die and hit_dice_max, none for monsters") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const rules5e::Combatant& w = l.find("srd51.character.wizard_level_3")->base;   // wizard d6, level 3, Con 14 (+2)
    CHECK(w.hit_die == 6);
    CHECK(w.hit_dice == 3);
    CHECK(w.hit_dice_max == 3);
    const rules5e::Combatant& f = l.find("srd51.character.fighter_level_5")->base;   // fighter d10, level 5
    CHECK(f.hit_die == 10);
    CHECK(f.hit_dice == 5);
    CHECK(f.hit_dice_max == 5);
    CHECK(l.find("srd51.monster.goblin")->base.hit_die == 0);   // monsters have no Hit Dice

    Json ch = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    ch["$schema"] = "character@4";
    ch["id"] = "t.mixed";
    ch["classes"] = Json::array({{{"class", "fighter"}, {"level", 2}, {"hit_die", "d10"}},
                                 {{"class", "wizard"}, {"level", 3}, {"hit_die", "d6"}}});
    const auto e = l.add(ch, "character");
    REQUIRE_MESSAGE(!e, e.value_or(""));
    const rules5e::Combatant& m = l.find("t.mixed")->base;
    CHECK(m.hit_die == 10);       // rules5e models one die size: classes[0]'s
    CHECK(m.hit_dice == 5);       // hit_dice_max is the summed class levels
    CHECK(m.hit_dice_max == 5);

    Json none = ch;
    none["id"] = "t.nodie";
    none["classes"][0]["hit_die"] = "d4";   // not a character hit die: nothing is loaded
    REQUIRE_MESSAGE(!l.add(none, "character"), l.add(none, "character").value_or(""));
    CHECK(l.find("t.nodie")->base.hit_die == 0);
    CHECK(l.find("t.nodie")->base.hit_dice_max == 0);
}

TEST_CASE("apply_surprise: round-1 EndTurn only, 0xA8 folded only while someone is surprised, replay matches") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena("srd51.character.fighter_1", "srd51.monster.goblin");
    ecs::Registry reg, reg2;
    auto c = Combat::create(reg, ch, l, 9);
    REQUIRE(c);
    // Surprise whoever acts first (initiative decides), so its very next step is the surprised turn.
    const CombatantId a = rules5e::current_actor(c->state());
    const CombatantId b = a == ch.spawns[0].stable_id ? ch.spawns[1].stable_id : ch.spawns[0].stable_id;
    const uint64_t h0 = c->hash();

    std::string err;
    CHECK_FALSE(c->apply_surprise({a, CombatantId(7)}, &err));   // an unknown id refuses, and changes nothing
    CHECK(err.find("unknown") != std::string::npos);
    CHECK(c->surprised_at_start().empty());
    CHECK(c->hash() == h0);

    err.clear();
    REQUIRE(c->apply_surprise({a}, &err));
    CHECK(c->surprised_at_start() == std::vector<CombatantId>{a});
    const uint64_t h1 = c->hash();
    CHECK(h1 != h0);   // 0xA8 is folded while someone is surprised

    // The helper agrees with rules5e: a hider whose Stealth beats every passive Perception surprises the other side.
    rules5e::StealthTotal hid[] = {{b, 30}};
    CHECK(c->surprised_by_stealth({hid, hid + 1}) == std::vector<CombatantId>{a});
    rules5e::StealthTotal low[] = {{b, 1}};
    CHECK(c->surprised_by_stealth({low, low + 1}).empty());

    // The surprised creature's first turn accepts only EndTurn (the driver knows it too).
    const Command end = next_command(*c, b);
    CHECK(end.request.kind == rules5e::ActionKind::EndTurn);
    CHECK(end.request.actor == a);
    Command atk;
    atk.request = {a, rules5e::ActionKind::Attack, b};
    CHECK_FALSE(c->submit(atk).ok);
    Command step;
    step.kind = CommandKind::Step;
    step.to = Vec2i{c->pos(a).x, c->pos(a).y + 1};
    CHECK_FALSE(c->submit(step).ok);
    const auto decline = [](const Combat&, const NeedsDecision& n) { return ReactionDecision{n.window_seq, false, 0}; };
    auto r = c->submit(end);
    if (r.ok && r.status == rules5e::StepStatus::NeedsDecision) r = c->settle(decline);
    REQUIRE(r.ok);
    CHECK_FALSE(rules5e::has_surprised(c->state()));   // the flag cleared with the turn

    // The same EndTurn without surprise gives the same hash: once cleared, 0xA8 folds nothing.
    auto plain = Combat::create(reg2, ch, l, 9);
    REQUIRE(plain);
    CHECK(plain->hash() == h0);   // a combat never surprised hashes exactly as before
    Command pend;
    pend.request = {a, rules5e::ActionKind::EndTurn};
    auto pr = plain->submit(pend);
    if (pr.ok && pr.status == rules5e::StepStatus::NeedsDecision) pr = plain->settle(decline);
    REQUIRE(pr.ok);
    CHECK(plain->hash() == c->hash());

    // Late setup is refused (and the hash stays).
    const uint64_t h2 = c->hash();
    CHECK_FALSE(c->apply_surprise({b}, &err));
    CHECK(c->hash() == h2);

    // Replay with the same set reproduces the fight.
    ecs::Registry rr;
    auto again = replay(rr, ch, l, 9, c->log(), &err, {a});
    REQUIRE_MESSAGE(again, err);
    CHECK(again->hash() == c->hash());
    CHECK(again->log_hash() == c->log_hash());
    CHECK(again->surprised_at_start() == std::vector<CombatantId>{a});

    // A whole driven fight with an ambush replays bit-identically too.
    ecs::Registry rf, rf2;
    auto fight = Combat::create(rf, ch, l, 9);
    REQUIRE(fight);
    REQUIRE(fight->apply_surprise({a}));
    NearestEnemyPolicy pol;
    const auto res = run_to_end(*fight, [&](CombatantId) -> IActorPolicy* { return &pol; });
    REQUIRE_MESSAGE(res.ended, res.error);
    auto fight2 = replay(rf2, ch, l, 9, fight->log(), &err, fight->surprised_at_start());
    REQUIRE_MESSAGE(fight2, err);
    CHECK(fight2->hash() == fight->hash());
}

TEST_CASE("rests: short and long rest on a kept roster, on a stream the encounter rng never sees") {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    const auto ch = arena("srd51.character.wizard_level_3", "srd51.monster.goblin");
    ecs::Registry reg, reg2;
    auto c = Combat::create(reg, ch, l, 9);
    REQUIRE(c);
    const CombatantId a = ch.spawns[0].stable_id;
    const rules5e::Combatant& wiz = *rules5e::find(c->state(), a);
    CHECK(wiz.hit_die == 6);
    CHECK(wiz.hit_dice == 3);

    // Damage the wizard, spend a feature use and a recharge action, then keep the roster past the fight.
    rules5e::EncounterState roster = c->state();
    rules5e::Combatant* kp = nullptr;
    for (auto& m : roster.combatants) if (m.id == a) kp = &m;
    REQUIRE(kp);
    rules5e::Combatant& k = *kp;
    k.hp = 1;
    k.temp_hp = 4;
    k.spell_slots[1] = 0;
    k.exhaustion_level = 2;
    rules5e::register_feature(roster, a, 55, 1, rules5e::FeatureRecharge::ShortRest, 1);
    rules5e::register_action_use(roster, a, 77, true);
    CHECK(rules5e::feature_uses_left(roster, a, 55) == 0);

    // The encounter rng and the rest rng are different streams of different seeds, and resting only advances the rest one.
    rng::Rng encounter(9, 0);
    const uint32_t before = encounter.next_u32();
    rng::Rng encounter2(9, 0);
    CHECK(encounter2.next_u32() == before);   // the encounter stream was not touched
    rng::Rng rest = make_rest_rng(9);
    CHECK(rest.next_u32() != before);
    rng::Rng rest2 = make_rest_rng(9);

    auto sr = short_rest(roster, a, rest, 1);
    REQUIRE(sr.ok);
    CHECK(sr.hit_dice.spent == 1);
    CHECK(k.hit_dice == 2);
    CHECK(k.hp == 1 + sr.hit_dice.healed);   // 1d6 + Con (+2), capped at the maximum
    CHECK(sr.hit_dice.healed >= 2);
    CHECK(sr.hit_dice.healed <= 8);
    CHECK(rules5e::feature_uses_left(roster, a, 55) == 1);   // a short-rest feature recharged
    REQUIRE(roster.action_uses.size() == 1);
    CHECK_FALSE(roster.action_uses[0].expended);    // recharge actions come back on any rest
    CHECK(k.spell_slots[1] == 0);                            // spell slots do not (a wizard's are not Pact slots)
    CHECK(rest.next_u32() != rest2.next_u32());              // exactly one draw, the Hit Die

    auto refused = short_rest(roster, CombatantId(7), rest, 1);
    CHECK_FALSE(refused.ok);
    // draws so far: the comparison above, the Hit Die, the check after the rest; a refused rest draws nothing.
    CHECK(rest.next_u32() == [&] { rng::Rng r = make_rest_rng(9); r.next_u32(); r.next_u32(); r.next_u32(); return r.next_u32(); }());

    auto lr = long_rest(roster, a);
    REQUIRE(lr.ok);
    CHECK(k.hp == rules5e::effective_hp_max(k));
    CHECK(k.temp_hp == 0);
    CHECK(k.exhaustion_level == 1);   // -1 with food and drink
    CHECK(k.hit_dice == 3);           // half of 3, minimum 1, regained
    CHECK(k.spell_slots[1] == k.spell_slots_max[1]);
    CHECK(k.spell_slots[2] == k.spell_slots_max[2]);

    // A long rest needs 1 HP, and an unknown id rests nobody.
    k.hp = 0;
    CHECK_FALSE(long_rest(roster, a).ok);
    CHECK(k.hp == 0);
    CHECK_FALSE(long_rest(roster, CombatantId(7)).ok);

    // Resting never changes the encounter it was copied from.
    auto fresh = Combat::create(reg2, ch, l, 9);
    REQUIRE(fresh);
    CHECK(fresh->hash() == c->hash());
    CHECK(fresh->log_hash() == c->log_hash());
}
