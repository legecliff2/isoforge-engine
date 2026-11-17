// extra_attack (character@4 features[], contract agreed with T&C): {"kind": "extra_attack", "attacks_per_action": 2..4} sets
// rules5e Combatant::attacks_per_action; the Attack action makes N weapon attacks (moving between them and a shove replacing
// one still work); highest wins; exact error strings; characters only; the AI uses all N; replay.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

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
