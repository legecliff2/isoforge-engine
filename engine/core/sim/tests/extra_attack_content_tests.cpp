// extra_attack (character@4 features[], contract agreed with T&C): {"kind": "extra_attack", "attacks_per_action": 2..4} sets
// rules5e Combatant::attacks_per_action; the Attack action makes N weapon attacks (moving between them and a shove replacing
// one still work); highest wins; exact error strings; characters only; the AI uses all N; replay.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "rules5e/equipment.hpp"
#include "sim/combat.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;
using rules5e::EventKind;
using Json = assets::Json;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_level_5";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
// fighter 5 with its multiattack "extra_attack" feature replaced by the new kind (the shipped fixture is untouched)
Json fighter(Json effect, Json more = Json::array()) {
    Json d = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    for (auto& f : d["features"])
        if (f["id"] == "extra_attack") f["effect"] = effect;
    for (const auto& m : more) d["features"].push_back(m);
    return d;
}
Json xa(Json n) { return {{"kind", "extra_attack"}, {"attacks_per_action", std::move(n)}}; }
Json feat(const char* id, Json eff) { return {{"id", id}, {"name", id}, {"effect", std::move(eff)}}; }

std::string add_err(const Json& d, const char* type = "character") {
    ContentLibrary lib;
    return lib.add(d, type).value_or("");
}

world::Chunk arena(Vec2i fpos, Vec2i gpos) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    c.spawns.push_back({kFighter, fpos, world::spawn_stable_id(c.coord, 0), ""});
    c.spawns.push_back({"t.ogre", gpos, world::spawn_stable_id(c.coord, 1), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
ContentLibrary lib_with(const Json& f) {
    ContentLibrary lib;
    Json o = load_json("content/packs/srd51.core/monsters/ogre.json");
    o["id"] = "t.ogre";
    o["hit_points"]["average"] = 500;
    REQUIRE_FALSE(lib.add(o, "monster"));
    const auto e = lib.add(f, "character");
    INFO(e.value_or(""));
    REQUIRE_FALSE(e);
    return lib;
}
struct Rig {
    ecs::Registry reg;
    world::Chunk chunk;
    const ContentLibrary* lib;
    std::optional<Combat> c;
    CombatantId f = 0, g = 0;
    Rig(const ContentLibrary& l, Vec2i fpos = {4, 4}, Vec2i gpos = {5, 4}) : lib(&l) {
        chunk = arena(fpos, gpos);
        c = Combat::create(reg, chunk, l, 5);
        REQUIRE(c);
        f = chunk.spawns[0].stable_id;
        g = chunk.spawns[1].stable_id;
        to_turn();
    }
    void to_turn() {
        for (int i = 0; i < 8 && rules5e::current_actor(c->state()) != f; ++i) {
            Command e;
            e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
            REQUIRE(settle(c->submit(e)).ok);
        }
        REQUIRE(rules5e::current_actor(c->state()) == f);
    }
    SubmitResult settle(SubmitResult r) {
        while (r.ok && r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    SubmitResult attack(uint8_t special = 0) {
        Command a;
        a.request = {f, ActionKind::Attack, g};
        a.request.special = special;
        return settle(c->submit(a));
    }
    int attacks() const {
        int n = 0;
        for (const auto& e : c->events()) n += e.kind == SimEvent::Kind::Rules && e.rules.kind == EventKind::AttackResolved && e.rules.actor == f;
        return n;
    }
    void check_replay() {
        ecs::Registry r2;
        std::string err;
        auto again = replay(r2, chunk, *lib, c->seed(), c->log(), &err);
        INFO(err);
        REQUIRE(again);
        CHECK(again->hash() == c->hash());
        CHECK(again->log_hash() == c->log_hash());
        CHECK(event_stream_hash(again->events()) == event_stream_hash(c->events()));
    }
};
}  // namespace

TEST_CASE("extra_attack: a loaded fighter 5 makes 2 attacks per Attack action, a step between them; replay") {
    const auto lib = lib_with(fighter(xa(2)));
    CHECK(lib.find(kFighter)->base.attacks_per_action == 2);
    Rig r(lib);
    REQUIRE(r.attack().ok);
    CHECK(r.c->state().budget.attacks_remaining == 1);
    CHECK_FALSE(r.c->state().budget.action);
    Command s;   // moving between the attacks is allowed (stays in reach)
    s.kind = CommandKind::Step;
    s.to = {4, 5};
    REQUIRE(r.settle(r.c->submit(s)).ok);
    REQUIRE(r.attack().ok);
    CHECK(r.attacks() == 2);
    CHECK(r.c->state().budget.attacks_remaining == 0);
    CHECK_FALSE(r.attack().ok);   // no third attack
    r.check_replay();
}

TEST_CASE("extra_attack: a shove replaces one of the attacks") {
    const auto lib = lib_with(fighter(xa(2)));
    Rig r(lib);
    REQUIRE(r.attack(3).ok);   // shove (push)
    CHECK(r.c->state().budget.attacks_remaining == 1);
    const Vec2i gp = r.c->pos(r.g);
    if (r.c->distance_tiles(r.f, r.g) <= 1) {
        REQUIRE(r.attack().ok);
        CHECK(r.attacks() == 1);
    } else {   // pushed out of reach: the remaining attack is still open for a reachable target
        CHECK(r.c->state().budget.attacks_remaining == 1);
        (void)gp;
    }
    r.check_replay();
}

TEST_CASE("extra_attack: several declarations, the highest wins (no stacking); without it, one attack") {
    {
        ContentLibrary lib = lib_with(fighter(xa(2), Json::array({feat("xa3", xa(3)), feat("xa2b", xa(2))})));
        CHECK(lib.find(kFighter)->base.attacks_per_action == 3);
    }
    {   // a file that declared character@3 lists it inert (character@4 only)
        ContentLibrary lib;
        Json d = fighter(xa(2));
        d["id"] = "t.f3";
        REQUIRE_FALSE(lib.add(d, "character", "", 3));
        CHECK(lib.find("t.f3")->base.attacks_per_action == 1);
    }
    {
        Json d = load_json("content/packs/srd51.core/characters/fighter_level_1.json");
        const auto lib = lib_with(d);
        CHECK(lib.find(d["id"].get<std::string>())->base.attacks_per_action == 1);
    }
}

TEST_CASE("extra_attack: the contract's exact load errors; monsters rejected") {
    const std::string range = "extra_attack: attacks_per_action must be an integer 2..4";
    CHECK(add_err(fighter({{"kind", "extra_attack"}})).find(range) != std::string::npos);   // missing: no default
    for (Json bad : {Json(1), Json(5), Json(0), Json(-2), Json("2"), Json::parse("2.5"), Json(true)}) {
        INFO(bad.dump());
        CHECK(add_err(fighter(xa(bad))).find(range) != std::string::npos);
    }
    for (int ok : {2, 3, 4}) CHECK(add_err(fighter(xa(ok))).empty());
    Json extra = xa(2);
    extra["count"] = 2;
    CHECK(add_err(fighter(extra)).find("extra_attack: unknown key 'count'") != std::string::npos);
    Json m = load_json("content/packs/srd51.core/monsters/ogre.json");
    m["id"] = "t.ogre.xa";
    m["traits"] = Json::array({feat("xa", xa(2))});
    CHECK(add_err(m, "monster").find("extra_attack: only valid on characters") != std::string::npos);
}

TEST_CASE("extra_attack: the AI uses both attacks of its Attack action; replay") {
    const auto lib = lib_with(fighter(xa(2)));
    Rig r(lib);
    for (int i = 0; i < 6 && rules5e::current_actor(r.c->state()) == r.f; ++i) {
        const Command cmd = next_command(*r.c, r.g);
        if (cmd.request.kind == ActionKind::EndTurn) break;
        REQUIRE(r.settle(r.c->submit(cmd)).ok);
    }
    CHECK(r.attacks() >= 2);   // both Extra Attack swings (Action Surge may add more)
    CHECK(r.c->state().budget.attacks_remaining == 0);
    r.check_replay();
}

TEST_CASE("possession strips a level-5 fighter's extra attack exactly") {
    ContentLibrary lib;
    Json d = load_json("content/packs/srd51.core/characters/fighter_level_5.json");
    REQUIRE_FALSE(lib.add(d, "character"));
    const auto* a = lib.find(kFighter);
    REQUIRE(a);
    CHECK(a->base.attacks_per_action == 2);
    REQUIRE(a->base.possession_class_base);
    CHECK(a->base.possession_class_base->attacks_per_action == 1);
    CHECK_FALSE(a->base.possession_unsupported);
}

TEST_CASE("possession class strip and armor category match the printed character") {
    ContentLibrary lib;
    for (const char* rel : {"content/packs/srd51.core/items/chain_mail.json",
                            "content/packs/srd51.core/items/shield.json",
                            "content/packs/srd51.core/items/leather_armor.json",
                            "content/packs/srd51.core/items/plate_armor.json"})
        REQUIRE_FALSE(lib.add_item(load_json(rel)));
    auto add = [&](const char* rel) {
        Json d = load_json(rel);
        REQUIRE_MESSAGE(lib.add(d, "character").value_or("").empty(), rel);
        return lib.find(d["id"].get<std::string>());
    };
    const auto* barb = add("content/packs/srd51.core/characters/barbarian_level_5.json");
    REQUIRE(barb);
    CHECK_FALSE(barb->base.possession_unsupported);
    REQUIRE(barb->base.possession_class_base);
    CHECK(barb->base.possession_class_base->attacks_per_action == 1);
    CHECK(barb->base.possession_class_base->speed_ft == 30);
    CHECK(barb->base.possession_class_base->ac == 12);
    CHECK(barb->base.possession_class_base->save_advantages.empty());
    const auto* monk = add("content/packs/srd51.core/characters/monk_level_1.json");
    REQUIRE(monk);
    CHECK_FALSE(monk->base.possession_unsupported);
    REQUIRE(monk->base.possession_class_base);
    CHECK(monk->base.possession_class_base->ac == 13);
    CHECK(monk->base.possession_class_base->attacks[0].damage.count == 0);
    CHECK(monk->base.possession_class_base->attacks[0].damage.bonus == 2);   // 1 + Str 13
    const auto* rogue = add("content/packs/srd51.core/characters/rogue_level_1.json");
    REQUIRE(rogue);
    CHECK_FALSE(rogue->base.possession_unsupported);
    REQUIRE(rogue->base.possession_class_base);
    CHECK_FALSE(rogue->base.possession_class_base->thieves_tools);
    CHECK(rogue->base.possession_class_base->triggers.empty());
    const auto* cleric = add("content/packs/srd51.core/characters/sample_hill_dwarf_cleric_1.json");
    REQUIRE(cleric);
    CHECK(cleric->base.worn_armor == 4);
    CHECK(cleric->base.worn_shield);
    const auto* ranger = add("content/packs/srd51.core/characters/ranger_level_2.json");
    REQUIRE(ranger);
    CHECK_FALSE(ranger->base.possession_unsupported);
    REQUIRE(ranger->base.possession_class_base);
    CHECK(ranger->base.attack_bonus_ranged == 2);
    CHECK(ranger->base.possession_class_base->attack_bonus_ranged == 0);
    const auto* champ = add("content/packs/srd51.core/characters/fighter_level_3.json");
    REQUIRE(champ);
    CHECK_FALSE(champ->base.possession_unsupported);
    REQUIRE(champ->base.possession_class_base);
    CHECK(champ->base.crit_min == 19);
    CHECK(champ->base.possession_class_base->crit_min == 20);
    const auto* berserk = add("content/packs/srd51.core/characters/barbarian_level_2.json");
    REQUIRE(berserk);
    CHECK_FALSE(berserk->base.possession_unsupported);
    REQUIRE(berserk->base.possession_class_base);
    CHECK(berserk->base.reckless_attack);
    CHECK_FALSE(berserk->base.possession_class_base->reckless_attack);
    const auto* monk3 = add("content/packs/srd51.core/characters/monk_level_3.json");
    REQUIRE(monk3);
    CHECK_FALSE(monk3->base.possession_unsupported);
    REQUIRE(monk3->base.possession_class_base);
    CHECK_FALSE(monk3->base.reactions.empty());
    CHECK(monk3->base.possession_class_base->reactions.empty());
    const auto* thief = add("content/packs/srd51.core/characters/rogue_level_5.json");
    REQUIRE(thief);
    CHECK_FALSE(thief->base.possession_unsupported);
    REQUIRE(thief->base.possession_class_base);
    CHECK(thief->base.climb_ft == 30);
    CHECK(thief->base.possession_class_base->climb_ft == 0);
    CHECK(thief->base.possession_class_base->reactions.empty());
    const auto* sorc = add("content/packs/srd51.core/characters/sorcerer_level_1.json");
    REQUIRE(sorc);
    CHECK_FALSE(sorc->base.possession_unsupported);
    REQUIRE(sorc->base.possession_class_base);
    CHECK(sorc->base.possession_class_base->ac == 12);
    CHECK(sorc->base.possession_hp_bonus == 1);
    const auto* lock = add("content/packs/srd51.core/characters/warlock_level_2.json");
    REQUIRE(lock);
    CHECK_FALSE(lock->base.possession_unsupported);
    REQUIRE(lock->base.possession_class_base);
    CHECK_FALSE(lock->base.spell_mods.empty());
    CHECK(lock->base.possession_class_base->spell_mods.empty());
    const auto* life = add("content/packs/srd51.core/characters/cleric_level_1.json");
    REQUIRE(life);
    CHECK_FALSE(life->base.possession_unsupported);
    REQUIRE(life->base.possession_class_base);
    CHECK(life->base.has_heal_bonus);
    CHECK_FALSE(life->base.possession_class_base->has_heal_bonus);
    const auto* bard = add("content/packs/srd51.core/characters/bard_level_3.json");
    REQUIRE(bard);
    CHECK_FALSE(bard->base.possession_unsupported);
    REQUIRE(bard->base.possession_class_base);
    CHECK_FALSE(bard->base.reactions.empty());
    CHECK(bard->base.possession_class_base->reactions.empty());
    const auto* meta = add("content/packs/srd51.core/characters/sorcerer_level_3.json");
    REQUIRE(meta);
    CHECK_FALSE(meta->base.possession_unsupported);
    REQUIRE(meta->base.possession_class_base);
    CHECK(meta->base.metamagic_known != 0);
    CHECK(meta->base.possession_class_base->metamagic_known == 0);
    const auto* sculpt = add("content/packs/srd51.core/characters/wizard_level_2.json");
    REQUIRE(sculpt);
    CHECK_FALSE(sculpt->base.possession_unsupported);
    REQUIRE(sculpt->base.possession_class_base);
    CHECK(sculpt->base.sculpt_spells);
    CHECK_FALSE(sculpt->base.possession_class_base->sculpt_spells);
}

TEST_CASE("possession class strip accepts every printed character") {
    ContentLibrary lib;
    namespace fs = std::filesystem;
    const fs::path root = fs::path(kRoot) / "content/packs/srd51.core";
    for (const auto& ent : fs::directory_iterator(root / "items")) {
        if (ent.path().extension() != ".json") continue;
        REQUIRE_MESSAGE(lib.add_item(load_json(ent.path().string().substr(std::string(kRoot).size() + 1))).value_or("").empty(),
                        ent.path().string());
    }
    for (const auto& ent : fs::directory_iterator(root / "characters")) {
        if (ent.path().extension() != ".json") continue;
        const std::string rel = ent.path().string().substr(std::string(kRoot).size() + 1);
        Json d = load_json(rel);
        const auto err = lib.add(d, "character");
        const std::string loaded = rel + ": " + err.value_or("");
        REQUIRE_MESSAGE(err.value_or("").empty(), loaded);
        const auto* a = lib.find(d["id"].get<std::string>());
        REQUIRE(a);
        const std::string why = rel + " " + a->base.possession_refuse;
        INFO(why);
        CHECK_FALSE(a->base.possession_unsupported);
    }
}

TEST_CASE("possession loader: possess, origin, and the armor note") {
    ContentLibrary lib;
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["$schema"] = "monster@5";
    gob["id"] = "t.possess";
    Json act = {{"id", "possession"}, {"name", "Possession"}, {"kind", "save_effect"},
                {"possess", true}, {"caster_sees_target", true},
                {"target_creature_types", Json::array({"humanoid"})},
                {"save", {{"ability", "cha"}, {"dc", 13}}},
                {"immune_on_success", {{"hours", 24}, {"scope", "same_source"}}}};
    gob["actions"].push_back(act);
    const auto added = lib.add(gob, "monster", "", 5);
    INFO(added.value_or(""));
    REQUIRE_FALSE(added);
    const auto* arch = lib.find("t.possess");
    REQUIRE(arch);
    REQUIRE_FALSE(arch->save_actions.empty());
    const auto* def = lib.spells()->find(arch->save_actions.back().spell_id);
    REQUIRE(def);
    CHECK(def->possess);
    act.erase("caster_sees_target");
    gob["id"] = "t.possess.bad";
    gob["actions"].back() = act;
    CHECK(lib.add(gob, "monster", "", 5).value_or("").find("caster_sees_target") != std::string::npos);
    Json tagged = load_json("content/packs/srd51.core/monsters/goblin.json");
    tagged["id"] = "t.origin.monster";
    tagged["traits"] = Json::array({{{"id", "keen"}, {"name", "Keen"}, {"origin", "race"}}});
    CHECK(lib.add(tagged, "monster", "", 5).value_or("").find("origin is character@5 only") != std::string::npos);
    Json old = load_json("content/packs/srd51.core/characters/fighter_level_1.json");
    old["id"] = "t.origin.old";
    old["features"][0]["origin"] = "class";
    CHECK(lib.add(old, "character", "", 4).value_or("").find("origin is character@5 only") != std::string::npos);
    old["id"] = "t.origin.ok";
    REQUIRE_FALSE(lib.add(old, "character", "", 5));
    Json guard = load_json("content/packs/srd51.core/monsters/guard.json");
    guard["equipment"] = Json::array();
    guard["id"] = "t.guard.note";
    REQUIRE_FALSE(lib.add(guard, "monster", "", 5));
    const auto* noted = lib.find("t.guard.note");
    REQUIRE(noted);
    CHECK(noted->base.worn_armor == 2);
    CHECK(noted->base.worn_shield);
    guard["id"] = "t.guard.natural";
    guard["armor_class"]["note"] = "natural armor";
    REQUIRE_FALSE(lib.add(guard, "monster", "", 5));
    CHECK(lib.find("t.guard.natural")->base.worn_armor == 0);
    CHECK_FALSE(lib.find("t.guard.natural")->base.worn_shield);
}

TEST_CASE("possession class deltas follow an equip before and during the link") {
    ContentLibrary lib;
    for (const char* rel : {"content/packs/srd51.core/items/greataxe.json",
                            "content/packs/srd51.core/items/handaxe.json",
                            "content/packs/srd51.core/items/shield.json",
                            "content/packs/srd51.core/items/chain_mail.json",
                            "content/packs/srd51.core/items/longsword.json"})
        REQUIRE_FALSE(lib.add_item(load_json(rel)));
    Json barb = load_json("content/packs/srd51.core/characters/barbarian_level_5.json");
    barb["equipment"].push_back({{"item", "srd51.item.shield"}});
    REQUIRE_FALSE(lib.add(barb, "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["id"] = "t.ghost.equip";
    REQUIRE_FALSE(lib.add(gob, "monster"));
    world::Chunk chunk{};
    chunk.coord = {5, 5, 0};
    chunk.origin = world::Chunk::Origin::Handcrafted;
    chunk.palette = {"", "floor"};
    for (auto& cell : chunk.layers[0]) cell.tile = 1;
    chunk.spawns.push_back({barb["id"].get<std::string>(), {4, 4}, world::spawn_stable_id(chunk.coord, 0), ""});
    chunk.spawns.push_back({"t.ghost.equip", {5, 4}, world::spawn_stable_id(chunk.coord, 1), ""});
    chunk.spawns.push_back({barb["id"].get<std::string>(), {6, 6}, world::spawn_stable_id(chunk.coord, 2), ""});
    chunk.content_hash = world::compute_content_hash(chunk);
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 5, CombatOptions{true});
    REQUIRE(combat);
    const CombatantId body = chunk.spawns[0].stable_id;
    const CombatantId ghost = chunk.spawns[1].stable_id;
    auto until = [&](CombatantId who) {
        for (int i = 0; i < 8 && rules5e::current_actor(combat->state()) != who; ++i) {
            Command e;
            e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
            REQUIRE(combat->submit(e).ok);
        }
        REQUIRE(rules5e::current_actor(combat->state()) == who);
    };
    auto equip = [&](CombatantId who, rules5e::EquipOp op, uint32_t item) {
        Command c;
        c.request = {who, ActionKind::UseObject};
        c.request.equip_op = uint8_t(op);
        c.request.equip_item = item;
        auto r = combat->submit(c);
        REQUIRE_MESSAGE(r.ok, r.error);
    };
    const rules5e::Loadout* lo = combat->loadout(body);
    REQUIRE(lo);
    const uint32_t shield_def = lib.items()->find("srd51.item.shield")->id;
    uint32_t shield = 0, axe = lo->view.main.instance;
    for (const auto& h : lo->view.carried)
        if (h.def == shield_def) shield = h.instance;
    REQUIRE(shield != 0);
    REQUIRE(axe != 0);
    until(body);
    CHECK(rules5e::find(combat->state(), body)->ac == 14);
    equip(body, rules5e::EquipOp::Stow, axe);
    equip(body, rules5e::EquipOp::DonShield, shield);
    CHECK(rules5e::find(combat->state(), body)->ac == 16);
    std::vector<rules5e::Event> ev;
    auto& st = const_cast<rules5e::EncounterState&>(combat->state());
    REQUIRE(rules5e::begin_possession(st, ghost, body, 1, 24, 1, ev).empty());
    CHECK(rules5e::find(combat->state(), body)->ac == 14);
    until(ghost);
    equip(body, rules5e::EquipOp::DoffShield, shield);
    CHECK(rules5e::find(combat->state(), body)->ac == 12);
    rules5e::end_possession(st, ghost, 2, ev, false);
    CHECK(rules5e::find(combat->state(), body)->ac == 14);

    Json fighter = load_json("content/packs/srd51.core/characters/fighter_level_1.json");
    REQUIRE_FALSE(lib.add(fighter, "character"));
    world::Chunk chunk2 = chunk;
    chunk2.spawns[0].archetype = fighter["id"].get<std::string>();
    chunk2.spawns[0].stable_id = world::spawn_stable_id(chunk2.coord, 4);
    chunk2.spawns[1].stable_id = world::spawn_stable_id(chunk2.coord, 5);
    chunk2.spawns[2].archetype = fighter["id"].get<std::string>();
    chunk2.spawns[2].tile = {8, 8};
    chunk2.spawns[2].stable_id = world::spawn_stable_id(chunk2.coord, 6);
    chunk2.content_hash = world::compute_content_hash(chunk2);
    ecs::Registry reg2;
    auto duel = Combat::create(reg2, chunk2, lib, 6, CombatOptions{true});
    REQUIRE(duel);
    const CombatantId pc = chunk2.spawns[0].stable_id;
    const CombatantId gh = chunk2.spawns[1].stable_id;
    const auto* sword = rules5e::find(duel->state(), pc);
    REQUIRE(sword);
    CHECK(sword->attacks[0].damage.bonus == 5);
    ev.clear();
    auto& st2 = const_cast<rules5e::EncounterState&>(duel->state());
    REQUIRE(rules5e::begin_possession(st2, gh, pc, 1, 24, 1, ev).empty());
    CHECK(rules5e::find(duel->state(), pc)->attacks[0].damage.bonus == 3);
    for (int i = 0; i < 8 && rules5e::current_actor(duel->state()) != gh; ++i) {
        Command e;
        e.request = {rules5e::current_actor(duel->state()), ActionKind::EndTurn};
        REQUIRE(duel->submit(e).ok);
    }
    const auto* flo = duel->loadout(pc);
    REQUIRE(flo);
    Command off;
    off.request = {pc, ActionKind::UseObject};
    off.request.equip_op = uint8_t(rules5e::EquipOp::DoffShield);
    off.request.equip_item = flo->view.off.instance;
    auto doffed = duel->submit(off);
    REQUIRE_MESSAGE(doffed.ok, doffed.error);
    CHECK(rules5e::find(duel->state(), pc)->ac == 16);
    CHECK(rules5e::find(duel->state(), pc)->attacks[0].damage.bonus == 3);
    rules5e::end_possession(st2, gh, 2, ev, false);
    CHECK(rules5e::find(duel->state(), pc)->ac == 16);
    CHECK(rules5e::find(duel->state(), pc)->attacks[0].damage.bonus == 5);
}

TEST_CASE("a possessed monk's unarmed strike does not lose its class delta twice") {
    ContentLibrary lib;
    REQUIRE_FALSE(lib.add_item(load_json("content/packs/srd51.core/items/shortsword.json")));
    Json monk = load_json("content/packs/srd51.core/characters/monk_level_5.json");
    REQUIRE_FALSE(lib.add(monk, "character"));
    Json gob = load_json("content/packs/srd51.core/monsters/goblin.json");
    gob["id"] = "t.ghost.monk";
    REQUIRE_FALSE(lib.add(gob, "monster"));
    world::Chunk chunk{};
    chunk.coord = {5, 5, 0};
    chunk.origin = world::Chunk::Origin::Handcrafted;
    chunk.palette = {"", "floor"};
    for (auto& cell : chunk.layers[0]) cell.tile = 1;
    chunk.spawns.push_back({monk["id"].get<std::string>(), {4, 4}, world::spawn_stable_id(chunk.coord, 0), ""});
    chunk.spawns.push_back({"t.ghost.monk", {5, 4}, world::spawn_stable_id(chunk.coord, 1), ""});
    chunk.spawns.push_back({monk["id"].get<std::string>(), {6, 6}, world::spawn_stable_id(chunk.coord, 2), ""});
    chunk.content_hash = world::compute_content_hash(chunk);
    ecs::Registry reg;
    auto combat = Combat::create(reg, chunk, lib, 7, CombatOptions{true});
    REQUIRE(combat);
    const CombatantId body = chunk.spawns[0].stable_id;
    const CombatantId ghost = chunk.spawns[1].stable_id;
    const rules5e::Combatant* before = rules5e::find(combat->state(), body);
    REQUIRE(before);
    int unarmed = -1;
    for (int i = 0; i < int(before->attacks.size()); ++i)
        if (before->attacks[std::size_t(i)].id == "unarmed_strike") unarmed = i;
    REQUIRE(unarmed >= 0);
    CHECK(before->attacks[std::size_t(unarmed)].to_hit == 7);
    CHECK(before->attacks[std::size_t(unarmed)].damage.count == 1);
    CHECK(before->attacks[std::size_t(unarmed)].damage.sides == 6);
    CHECK(before->attacks[std::size_t(unarmed)].damage.bonus == 4);
    std::vector<rules5e::Event> ev;
    auto& st = const_cast<rules5e::EncounterState&>(combat->state());
    REQUIRE(rules5e::begin_possession(st, ghost, body, 1, 24, 1, ev).empty());
    auto strike = [&](const rules5e::Combatant& c) -> const rules5e::Attack& {
        return c.attacks[std::size_t(unarmed)];
    };
    const rules5e::Combatant* linked = rules5e::find(combat->state(), body);
    REQUIRE(linked);
    CHECK(strike(*linked).to_hit == 4);
    CHECK(strike(*linked).damage.count == 0);
    CHECK(strike(*linked).damage.sides == 0);
    CHECK(strike(*linked).damage.bonus == 2);   // 1 + Str
    for (int i = 0; i < 8 && rules5e::current_actor(combat->state()) != ghost; ++i) {
        Command e;
        e.request = {rules5e::current_actor(combat->state()), ActionKind::EndTurn};
        REQUIRE(combat->submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(combat->state()) == ghost);
    const rules5e::Loadout* lo = combat->loadout(body);
    REQUIRE(lo);
    const uint32_t sword = lo->view.main.instance;
    REQUIRE(sword != 0);
    auto equip = [&](rules5e::EquipOp op) {
        Command c;
        c.request = {body, ActionKind::UseObject};
        c.request.equip_op = uint8_t(op);
        c.request.equip_item = sword;
        auto r = combat->submit(c);
        REQUIRE_MESSAGE(r.ok, r.error);
    };
    equip(rules5e::EquipOp::Stow);
    const rules5e::Combatant* stowed = rules5e::find(combat->state(), body);
    REQUIRE(stowed);
    CHECK(strike(*stowed).to_hit == 4);
    CHECK(strike(*stowed).damage.count == 0);
    CHECK(strike(*stowed).damage.sides == 0);
    CHECK(strike(*stowed).damage.bonus == 2);
    equip(rules5e::EquipOp::Draw);
    const rules5e::Combatant* drawn = rules5e::find(combat->state(), body);
    REQUIRE(drawn);
    CHECK(strike(*drawn).to_hit == 4);
    CHECK(strike(*drawn).damage.count == 0);
    CHECK(strike(*drawn).damage.sides == 0);
    CHECK(strike(*drawn).damage.bonus == 2);
    rules5e::end_possession(st, ghost, 2, ev, false);
    const rules5e::Combatant* ended = rules5e::find(combat->state(), body);
    REQUIRE(ended);
    CHECK(strike(*ended).to_hit == 7);
    CHECK(strike(*ended).damage.count == 1);
    CHECK(strike(*ended).damage.sides == 6);
    CHECK(strike(*ended).damage.bonus == 4);
}
