// Ghost Possession content (SRD 5.1, CONTENT_CONVENTIONS §59) and the werebear unlink-in-form case.
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include "rules5e/conditions.hpp"
#include "sim/combat.hpp"
#include "sim/components.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::EventKind;
using rules5e::FormKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kGhost = "srd51.monster.ghost";
const char* kFighter = "srd51.character.fighter_level_1";
const char* kWerebear = "srd51.monster.werebear";
const char* kWolf = "srd51.monster.wolf";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

ContentLibrary pack_lib() {
    ContentLibrary lib;
    for (const char* item : {"greataxe", "chain_mail", "shield", "longsword", "crossbow_light", "crossbow_bolt"}) {
        auto e = lib.add_item(load_json(std::string("content/packs/srd51.core/items/") + item + ".json"));
        INFO(item);
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
    }
    for (const char* who : {kGhost, kWerebear, kWolf}) {
        auto e = lib.add(load_json(std::string("content/packs/srd51.core/monsters/") +
                                   std::string(who).substr(std::string("srd51.monster.").size()) + ".json"),
                         "monster");
        INFO(who);
        INFO(e.value_or(""));
        REQUIRE_FALSE(e);
    }
    auto fighter = lib.add(load_json("content/packs/srd51.core/characters/fighter_level_1.json"), "character");
    INFO(fighter.value_or(""));
    REQUIRE_FALSE(fighter);
    return lib;
}

uint32_t possession_id() { return rules5e::spell_id_of("srd51.monster.ghost#possession"); }

world::Chunk arena(const std::vector<std::pair<std::string, Vec2i>>& spawns) {
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

struct Fight {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    std::optional<Combat> c;
    Fight(const ContentLibrary& l, const std::vector<std::pair<std::string, Vec2i>>& spawns, uint64_t seed)
        : lib(&l) {
        chunk = arena(spawns);
        std::string err;
        c = Combat::create(reg, chunk, l, seed, {}, &err);
        INFO(err);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return chunk.spawns[i].stable_id; }
    const rules5e::Combatant& who(std::size_t i) const { return *rules5e::find(c->state(), id(i)); }
    rules5e::Combatant& mut(std::size_t i) {
        auto& st = const_cast<rules5e::EncounterState&>(c->state());
        for (auto& k : st.combatants)
            if (k.id == id(i)) return k;
        REQUIRE(false);
        return st.combatants.front();
    }
    SubmitResult settle(SubmitResult r) {
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    void end_turn() {
        Command e;
        e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
        auto r = settle(c->submit(e));
        INFO(r.error);
        REQUIRE(r.ok);
    }
    void advance_to(CombatantId w) {
        for (int i = 0; i < 16 && rules5e::current_actor(c->state()) != w; ++i) end_turn();
        REQUIRE(rules5e::current_actor(c->state()) == w);
    }
    SubmitResult cast_possession(CombatantId ghost, CombatantId target) {
        Command cast;
        cast.request = {ghost, ActionKind::CastSpell, target};
        cast.request.spell_id = possession_id();
        return settle(c->submit(cast));
    }
};

int rules_since(const Fight& f, int mark, EventKind kind) {
    int n = 0;
    const auto& ev = f.c->events();
    for (int i = mark; i < int(ev.size()); ++i)
        if (ev[std::size_t(i)].kind == SimEvent::Kind::Rules && ev[std::size_t(i)].rules.kind == kind) ++n;
    return n;
}

bool save_succeeded(const Fight& f, int mark) {
    const auto& ev = f.c->events();
    for (int i = mark; i < int(ev.size()); ++i) {
        const auto& e = ev[std::size_t(i)];
        if (e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::SavingThrowRolled) return e.rules.c != 0;
    }
    return false;
}

bool possession_ready(const Fight& f, CombatantId ghost) {
    for (const auto& u : f.c->state().action_uses)
        if (u.actor == ghost && u.spell_id == possession_id() && !u.expended) return true;
    return false;
}

bool has_immunity(const Fight& f, CombatantId body, CombatantId ghost) {
    for (const auto& im : f.c->state().action_immunities)
        if (im.creature == body && im.source == ghost && im.spell_id == possession_id() &&
            uint32_t(f.c->state().round) < im.until_round)
            return true;
    return false;
}

std::vector<std::string> attack_ids(const rules5e::Combatant& c) {
    std::vector<std::string> ids;
    for (const auto& a : c.attacks) ids.push_back(std::string(a.id));
    return ids;
}

uint32_t shape_feature(const Archetype& a) {
    for (std::size_t i = 0; i < a.features.size(); ++i)
        if (a.features[i].kind == "shapechanger") return uint32_t(i + 1);
    return 0;
}

int tiles_apart(Fight& f, CombatantId a, CombatantId b) {
    const TilePos* pa = f.reg.get<TilePos>(f.c->entity(a));
    const TilePos* pb = f.reg.get<TilePos>(f.c->entity(b));
    REQUIRE(pa);
    REQUIRE(pb);
    const int dx = pa->x > pb->x ? pa->x - pb->x : pb->x - pa->x;
    const int dy = pa->y > pb->y ? pa->y - pb->y : pb->y - pa->y;
    return dx > dy ? dx : dy;
}
}  // namespace

TEST_CASE("ghost possession loads as the SRD 5.1 save") {
    ContentLibrary lib = pack_lib();
    const auto* arch = lib.find(kGhost);
    REQUIRE(arch);
    const rules5e::SpellDef* def = nullptr;
    for (const auto& sa : arch->save_actions) {
        if (sa.id != "possession") continue;
        CHECK(sa.range_ft == 5);
        CHECK(sa.usage_kind == 1);
        def = lib.spells()->find(sa.spell_id);
    }
    REQUIRE(def);
    CHECK(def->possess);
    CHECK_FALSE(def->magical);
    CHECK_FALSE(def->is_spell);
    CHECK(def->save_ability == rules5e::Ability::Cha);
    CHECK(def->fixed_dc == 13);
    CHECK(def->range_ft == 5);
    CHECK(def->targets == 1);
    CHECK(def->caster_sees_target);
    CHECK(def->target_types == rules5e::bit(rules5e::CreatureType::Humanoid));
    CHECK(def->success_immunity_hours == 24);
    CHECK(def->success_immunity_scope == 1);
    CHECK(def->recharge_min == 6);
    CHECK(def->usage_kind == 1);
    CHECK(def->dice.count == 0);
    CHECK_FALSE(def->applies_condition);
    CHECK(def->control == rules5e::ControlKind::None);
}

TEST_CASE("ghost possession: a successful save does not link, and the 24 hour immunity blocks the recharge") {
    ContentLibrary lib = pack_lib();
    bool found = false;
    for (uint64_t seed = 1; seed <= 40 && !found; ++seed) {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, seed);
        const CombatantId ghost = f.id(0);
        const CombatantId body = f.id(1);
        f.advance_to(ghost);
        const int mark = int(f.c->events().size());
        auto cast = f.cast_possession(ghost, body);
        INFO(cast.error);
        REQUIRE(cast.ok);
        if (rules_since(f, mark, EventKind::Possessed) != 0) continue;
        REQUIRE(save_succeeded(f, mark));
        CHECK(f.who(0).possessing == 0);
        CHECK(f.who(1).possessed_by == 0);
        CHECK(has_immunity(f, body, ghost));
        bool ready = false;
        for (int n = 0; n < 80 && !ready; ++n) {
            f.advance_to(ghost);
            ready = possession_ready(f, ghost) && f.c->state().budget.action;
            if (!ready) f.end_turn();
        }
        REQUIRE(ready);
        const int again = int(f.c->events().size());
        auto blocked = f.cast_possession(ghost, body);
        INFO(blocked.error);
        REQUIRE(blocked.ok);
        CHECK(rules_since(f, again, EventKind::SavingThrowRolled) == 0);
        CHECK(rules_since(f, again, EventKind::Possessed) == 0);
        CHECK(f.who(0).possessing == 0);
        CHECK(f.who(1).possessed_by == 0);
        found = true;
    }
    REQUIRE(found);
}

TEST_CASE("ghost possession: a failed save links, and 0 HP returns the ghost within 5 ft with immunity") {
    ContentLibrary lib = pack_lib();
    bool found = false;
    for (uint64_t seed = 1; seed <= 40 && !found; ++seed) {
        // A second party member stays conscious. The possessed body does not count for its own team,
        // so a fight of only the ghost and that body would end on the failed save.
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}, {kWolf, {6, 4}}, {kFighter, {10, 10}}}, seed);
        const CombatantId ghost = f.id(0);
        const CombatantId body = f.id(1);
        const CombatantId wolf = f.id(2);
        f.advance_to(ghost);
        const int mark = int(f.c->events().size());
        auto cast = f.cast_possession(ghost, body);
        INFO(cast.error);
        REQUIRE(cast.ok);
        if (rules_since(f, mark, EventKind::Possessed) == 0) continue;
        CHECK(f.who(0).possessing == body);
        CHECK(f.who(1).possessed_by == ghost);
        CHECK_FALSE(has_immunity(f, body, ghost));
        // A character at 0 HP is down, not dead, so the engine records the 24-hour row.
        // A monster would be killed first, and a dead body is not recorded.
        f.mut(1).hp = 4;
        f.mut(1).ac = 5;
        f.mut(2).attacks[0].to_hit = 19;
        bool down = false;
        for (int n = 0; n < 12 && !down; ++n) {
            f.advance_to(wolf);
            Command atk;
            atk.request = {wolf, ActionKind::Attack, body, 0};
            atk.request.target_within_5ft = true;
            auto hit = f.settle(f.c->submit(atk));
            INFO(hit.error);
            REQUIRE(hit.ok);
            down = f.who(1).hp <= 0;
            if (!down) f.end_turn();
        }
        REQUIRE(down);
        CHECK_FALSE(f.who(1).dead);
        CHECK(f.who(0).possessing == 0);
        CHECK(f.who(1).possessed_by == 0);
        CHECK(rules_since(f, mark, EventKind::PossessionEnded) >= 1);
        CHECK(tiles_apart(f, ghost, body) <= 1);
        CHECK(has_immunity(f, body, ghost));
        found = true;
    }
    REQUIRE(found);
}

TEST_CASE("werebear linked in human form, unlinked in bear form, keeps climb 30 and reverts to the human") {
    ContentLibrary lib = pack_lib();
    // A second team keeps the fight open. A possessed body does not count as conscious
    // for its own team, and the ghost is on that same team.
    Fight f(lib, {{kWerebear, {4, 4}}, {kGhost, {8, 8}}, {kFighter, {12, 12}}}, 1);
    const CombatantId bear = f.id(0);
    const CombatantId ghost = f.id(1);
    const uint32_t fid = shape_feature(*lib.find(kWerebear));
    REQUIRE(fid != 0);
    CHECK(f.who(0).climb_ft == 0);
    CHECK(f.who(0).speed_ft == 30);
    CHECK(f.who(0).ac == 10);
    CHECK(f.who(0).size == rules5e::Size::Medium);
    CHECK(attack_ids(f.who(0)) == std::vector<std::string>{"greataxe"});
    const int hp_max = f.who(0).hp_max;
    const auto human_triggers = f.who(0).triggers.size();
    const auto human_reactions = f.who(0).reactions.size();
    const auto human_bonus = f.who(0).bonus_attacks.size();

    std::vector<rules5e::Event> ev;
    auto& st = const_cast<rules5e::EncounterState&>(f.c->state());
    REQUIRE(rules5e::begin_possession(st, ghost, bear, possession_id(), 24, 1, ev).empty());
    f.advance_to(ghost);
    Command shape;
    shape.request = {bear, ActionKind::UseFeature};
    shape.request.feature_id = fid;
    shape.request.form_archetype = 2;   // bear, after hybrid
    auto shaped = f.settle(f.c->submit(shape));
    INFO(shaped.error);
    REQUIRE(shaped.ok);
    CHECK(f.who(0).form == FormKind::Shapechanger);
    CHECK(f.who(0).climb_ft == 30);
    CHECK(f.who(0).speed_ft == 40);
    CHECK(attack_ids(f.who(0)) == std::vector<std::string>({"bite", "claw"}));
    const auto bear_triggers = f.who(0).triggers.size();
    const auto bear_reactions = f.who(0).reactions.size();
    const auto bear_bonus = f.who(0).bonus_attacks.size();

    rules5e::end_possession(st, ghost, 2, ev, false);
    CHECK(f.who(0).form == FormKind::Shapechanger);
    CHECK(f.who(0).climb_ft == 30);
    CHECK(f.who(0).speed_ft == 40);
    CHECK(attack_ids(f.who(0)) == std::vector<std::string>({"bite", "claw"}));
    CHECK(f.who(0).triggers.size() == bear_triggers);
    CHECK(f.who(0).reactions.size() == bear_reactions);
    CHECK(f.who(0).bonus_attacks.size() == bear_bonus);
    CHECK(f.who(0).abilities[rules5e::Ability::Cha] == 12);

    f.advance_to(bear);
    Command back;
    back.request = {bear, ActionKind::Revert};
    auto reverted = f.settle(f.c->submit(back));
    INFO(reverted.error);
    REQUIRE(reverted.ok);
    CHECK(f.who(0).form == FormKind::None);
    CHECK(f.who(0).climb_ft == 0);
    CHECK(f.who(0).speed_ft == 30);
    CHECK(f.who(0).ac == 10);
    CHECK(f.who(0).size == rules5e::Size::Medium);
    CHECK(f.who(0).hp_max == hp_max);
    CHECK(attack_ids(f.who(0)) == std::vector<std::string>{"greataxe"});
    CHECK(f.who(0).triggers.size() == human_triggers);
    CHECK(f.who(0).reactions.size() == human_reactions);
    CHECK(f.who(0).bonus_attacks.size() == human_bonus);
    CHECK(f.who(0).abilities[rules5e::Ability::Int] == 11);
    CHECK(f.who(0).abilities[rules5e::Ability::Wis] == 12);
    CHECK(f.who(0).abilities[rules5e::Ability::Cha] == 12);
}

namespace {

Command ai_now(Fight& f, CombatantId actor, std::optional<CombatantId> force = std::nullopt) {
    f.advance_to(actor);
    NearestEnemyPolicy pol;
    const auto target = force ? force : pol.choose_target(*f.c, rules5e::turn_body(f.c->state()));
    return next_command(*f.c, target);
}

bool casts_possession(const Command& cmd, CombatantId target) {
    return cmd.request.kind == ActionKind::CastSpell && cmd.request.spell_id == possession_id() && cmd.request.target == target;
}

bool withering(const Fight& f, CombatantId ghost, const Command& cmd) {
    if (cmd.request.kind != ActionKind::Attack || cmd.request.actor != ghost) return false;
    const auto* g = rules5e::find(f.c->state(), ghost);
    return g && cmd.request.attack_index < g->attacks.size() && g->attacks[cmd.request.attack_index].id == "withering_touch";
}

rules5e::EncounterState& mut_state(Fight& f) { return const_cast<rules5e::EncounterState&>(f.c->state()); }

}  // namespace

TEST_CASE("the built-in AI possesses a legal target instead of withering touch") {
    ContentLibrary lib = pack_lib();
    Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 11);
    const auto cmd = ai_now(f, f.id(0));
    CHECK(casts_possession(cmd, f.id(1)));
}

TEST_CASE("the built-in AI does not possess an illegal target") {
    ContentLibrary lib = pack_lib();
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        f.advance_to(f.id(0));
        for (auto& u : mut_state(f).action_uses)
            if (u.actor == f.id(0) && u.spell_id == possession_id()) u.expended = true;
        NearestEnemyPolicy pol;
        const auto cmd = next_command(*f.c, pol.choose_target(*f.c, f.id(0)));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
        CHECK(withering(f, f.id(0), cmd));
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        mut_state(f).action_immunities.push_back(
            {f.id(1), f.id(0), possession_id(), uint32_t(mut_state(f).round) + 600u});
        const auto cmd = ai_now(f, f.id(0));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
        CHECK(withering(f, f.id(0), cmd));
    }
    {
        world::Chunk chunk = arena({{kGhost, {4, 4}}, {kWolf, {5, 4}}});
        chunk.spawns[1].overrides_json = R"({"team":"party"})";
        chunk.content_hash = world::compute_content_hash(chunk);
        ecs::Registry reg;
        std::string err;
        auto c = Combat::create(reg, chunk, lib, 3, {}, &err);
        INFO(err);
        REQUIRE(c);
        const CombatantId ghost = chunk.spawns[0].stable_id;
        const CombatantId wolf = chunk.spawns[1].stable_id;
        for (int i = 0; i < 16 && rules5e::current_actor(c->state()) != ghost; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(c->submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == ghost);
        NearestEnemyPolicy pol;
        const auto cmd = next_command(*c, pol.choose_target(*c, ghost));
        CHECK(cmd.request.kind == ActionKind::Attack);
        CHECK(cmd.request.spell_id == 0);
        const auto* g = rules5e::find(c->state(), ghost);
        REQUIRE(g);
        CHECK(g->attacks[cmd.request.attack_index].id == "withering_touch");
        CHECK(cmd.request.target == wolf);
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {4, 6}}}, 3);
        const auto cmd = ai_now(f, f.id(0));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        rules5e::add_condition(f.mut(1), rules5e::Condition::Invisible);
        f.advance_to(f.id(0));
        f.c->legal_actions();   // binds the sight hook; before the first submit it is empty
        REQUIRE(f.who(1).has(rules5e::Condition::Invisible));
        REQUIRE_FALSE(rules5e::sight(f.c->state(), f.id(0), f.id(1)).can_see);
        NearestEnemyPolicy pol;
        const auto cmd = next_command(*f.c, pol.choose_target(*f.c, f.id(0)));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        f.mut(1).plane = rules5e::Plane::BorderEthereal;
        const auto cmd = ai_now(f, f.id(0));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kGhost, {8, 8}}, {kFighter, {5, 4}}}, 3);
        std::vector<rules5e::Event> ev;
        REQUIRE(rules5e::begin_possession(mut_state(f), f.id(1), f.id(2), possession_id(), 24, 1, ev).empty());
        const auto cmd = ai_now(f, f.id(0), f.id(2));
        CHECK_FALSE(casts_possession(cmd, f.id(2)));
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        rules5e::ActiveSpellEffect ward;
        ward.target = f.id(1);
        ward.blocks_possession = true;
        ward.immune_from_types = rules5e::bit(rules5e::CreatureType::Undead);
        mut_state(f).effects.push_back(ward);
        const auto cmd = ai_now(f, f.id(0));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
        CHECK(withering(f, f.id(0), cmd));
    }
}

TEST_CASE("the built-in AI uses withering touch against a refused host") {
    ContentLibrary lib = pack_lib();
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        f.mut(1).possession_unsupported = true;
        const auto cmd = ai_now(f, f.id(0));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
        CHECK(withering(f, f.id(0), cmd));
    }
    {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
        f.mut(1).possession_refuse = "possessed: unsupported host: improved_critical";
        const auto cmd = ai_now(f, f.id(0));
        CHECK_FALSE(casts_possession(cmd, f.id(1)));
        CHECK(withering(f, f.id(0), cmd));
    }
}

TEST_CASE("the built-in AI uses withering touch against an incapacitated target") {
    ContentLibrary lib = pack_lib();
    Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 3);
    rules5e::add_condition(f.mut(1), rules5e::Condition::Stunned);
    const auto cmd = ai_now(f, f.id(0));
    CHECK_FALSE(casts_possession(cmd, f.id(1)));
    CHECK(withering(f, f.id(0), cmd));
}

TEST_CASE("the same seed runs the same multi-round ghost fight") {
    ContentLibrary lib = pack_lib();
    // Goblins, not the fighter: a possessed fighter's class feature is refused and the
    // built-in policy stops. Two party goblins keep the fight open after a link.
    REQUIRE_FALSE(lib.add(load_json("content/packs/srd51.core/monsters/goblin.json"), "monster"));
    world::Chunk chunk = arena({{kGhost, {4, 4}}, {"srd51.monster.goblin", {5, 4}}, {"srd51.monster.goblin", {6, 4}}});
    chunk.spawns[1].overrides_json = R"({"team":"party"})";
    chunk.spawns[2].overrides_json = R"({"team":"party"})";
    chunk.content_hash = world::compute_content_hash(chunk);
    auto play = [&](uint64_t seed) {
        ecs::Registry reg;
        std::string err;
        auto c = Combat::create(reg, chunk, lib, seed, {}, &err);
        INFO(err);
        REQUIRE(c);
        NearestEnemyPolicy pol;
        const RunResult rr = run_to_end(*c, [&](CombatantId) -> IActorPolicy* { return &pol; }, 80);
        REQUIRE_MESSAGE(rr.error.empty(), rr.error);
        REQUIRE(c->state().round >= 2);
        return std::pair{c->hash(), c->log_hash()};
    };
    const auto a = play(11);
    const auto b = play(11);
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
    CHECK(a.first != 0);
}

TEST_CASE("a possessed body acts for the ghost's side under AI control") {
    ContentLibrary lib = pack_lib();
    bool linked = false;
    for (uint64_t seed = 1; seed <= 40 && !linked; ++seed) {
        Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}, {kFighter, {4, 23}}}, seed);
        const CombatantId ghost = f.id(0);
        const CombatantId body = f.id(1);
        const CombatantId far = f.id(2);
        const auto opening = ai_now(f, ghost);
        if (!casts_possession(opening, body)) continue;
        const int mark = int(f.c->events().size());
        auto cast = f.settle(f.c->submit(opening));
        if (!cast.ok || f.who(0).possessing != body) continue;
        CHECK_FALSE(save_succeeded(f, mark));
        CHECK(rules5e::side_of(f.c->state(), f.who(1)) == rules5e::side_of(f.c->state(), f.who(0)));
        CHECK(f.who(1).team != f.who(0).team);
        Command done;
        done.request = {ghost, ActionKind::EndTurn};
        REQUIRE(f.settle(f.c->submit(done)).ok);
        f.advance_to(ghost);
        REQUIRE(f.who(0).possessing == body);
        NearestEnemyPolicy pol;
        const auto foe = pol.choose_target(*f.c, rules5e::turn_body(f.c->state()));
        REQUIRE(foe);
        CHECK(*foe == far);
        CHECK(*foe != ghost);
        const auto step = next_command(*f.c, foe);
        const bool shot = step.request.kind == ActionKind::Attack && step.request.actor == body && step.request.target == far;
        const bool closing = step.kind == CommandKind::Step;
        INFO(int(step.request.kind));
        INFO(int(step.kind));
        CHECK((shot || closing));
        linked = true;
    }
    CHECK(linked);
}

TEST_CASE("the built-in AI ends possession when the body cannot act") {
    ContentLibrary lib = pack_lib();
    Fight f(lib, {{kGhost, {4, 4}}, {kFighter, {5, 4}}}, 8);
    f.c->set_end_condition([](const rules5e::EncounterState&) { return std::optional<rules5e::EncounterOutcome>{}; });
    std::vector<rules5e::Event> ev;
    REQUIRE(rules5e::begin_possession(mut_state(f), f.id(0), f.id(1), possession_id(), 24, 1, ev).empty());
    rules5e::add_condition(f.mut(1), rules5e::Condition::Stunned);
    const auto cmd = ai_now(f, f.id(0));
    CHECK(cmd.request.kind == ActionKind::Revert);
    CHECK(cmd.request.actor == f.id(0));
    CHECK(cmd.request.as_bonus_action);
    const int mark = int(f.c->events().size());
    auto ended = f.settle(f.c->submit(cmd));
    INFO(ended.error);
    REQUIRE(ended.ok);
    CHECK(f.who(0).possessing == 0);
    CHECK(f.who(1).possessed_by == 0);
    CHECK(rules_since(f, mark, EventKind::PossessionEnded) == 1);
}
