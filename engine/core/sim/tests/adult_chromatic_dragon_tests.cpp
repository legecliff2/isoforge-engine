// L1: the five SRD 5.1 adult chromatic dragons (monster@7).
#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <vector>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Condition;
using rules5e::EventKind;
using rules5e::LegendaryUse;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;

struct Row {
    const char* id;
    int fp_dc;
    int wa_dc;
    int wa_bonus;
    rules5e::Ability breath_ability;
};

const Row kRows[] = {
    {"srd51.monster.adult_white_dragon", 14, 19, 6, rules5e::Ability::Con},
    {"srd51.monster.adult_black_dragon", 16, 19, 6, rules5e::Ability::Dex},
    {"srd51.monster.adult_green_dragon", 16, 19, 6, rules5e::Ability::Con},
    {"srd51.monster.adult_blue_dragon", 17, 20, 7, rules5e::Ability::Dex},
    {"srd51.monster.adult_red_dragon", 19, 22, 8, rules5e::Ability::Dex},
};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}

struct Spawn { std::string archetype; int x, y; std::string team; };
world::Chunk arena(const std::vector<Spawn>& spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        std::string ov = spawns[i].team.empty() ? "" : std::string("{\"team\":\"") + spawns[i].team + "\"}";
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y},
                            world::spawn_stable_id(c.coord, uint32_t(i)), ov});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}

SubmitResult go(Combat& c, Command cmd) {
    auto r = c.submit(cmd);
    while (r.needs) r = c.decide(rules5e::ReactionDecision{r.needs->window_seq, false, 0});
    return r;
}
Command end_turn(const Combat& c) {
    Command e;
    e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
    return e;
}
void to_actor(Combat& c, rules5e::CombatantId id) {
    for (int i = 0; i < 48 && (rules5e::current_actor(c.state()) != id || c.state().legendary_window.x != 0) &&
         !c.state().ended;
         ++i) {
        if (c.state().legendary_window.x != 0) {
            Command pass;
            pass.request = {c.state().legendary_window.candidate, ActionKind::LegendaryAction};
            REQUIRE(go(c, pass).ok);
        } else {
            REQUIRE(go(c, end_turn(c)).ok);
        }
    }
    REQUIRE(c.state().legendary_window.x == 0);
    REQUIRE(rules5e::current_actor(c.state()) == id);
}
void offer(Combat& c, rules5e::CombatantId id) {
    for (int i = 0; i < 16 && c.state().legendary_window.candidate != id && !c.state().ended; ++i)
        REQUIRE(go(c, end_turn(c)).ok);
    REQUIRE(c.state().legendary_window.candidate == id);
}

Json victim() {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@5";
    m["id"] = "t.l1.victim";
    m["hit_points"]["average"] = 80;
    m["abilities"]["dex"] = 1;   // Dex save max 15; every adult Wing Attack DC is 19 or higher
    m["abilities"]["wis"] = 10;
    m.erase("behavior");
    return m;
}

ContentLibrary library() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(victim(), "monster"));
    return l;
}
}  // namespace

TEST_CASE("L1 adult chromatic dragons load Frightful Presence, Wing Attack, Tail Attack, and Legendary Resistance") {
    ContentLibrary lib;
    REQUIRE(lib.load(sample_set()).empty());
    const uint32_t lr = rules5e::spell_id_of("legendary_resistance");
    for (const Row& row : kRows) {
        const auto* arch = lib.find(row.id);
        REQUIRE(arch);
        CHECK(arch->base.la_max == 3);
        CHECK(arch->base.legendary_pool == lr);
        REQUIRE(arch->resources.size() == 1);
        CHECK(arch->resources[0].id == "legendary_resistance");
        CHECK(arch->resources[0].max == 3);
        CHECK(arch->resources[0].pool_id == lr);
        REQUIRE(arch->base.legendary.size() == 3);
        CHECK(arch->base.legendary[0].use == LegendaryUse::Check);
        CHECK(arch->base.legendary[0].cost == 1);
        CHECK(arch->base.legendary[1].use == LegendaryUse::Attack);
        CHECK(arch->base.legendary[1].cost == 1);
        CHECK(arch->base.legendary[2].use == LegendaryUse::Effect);
        CHECK(arch->base.legendary[2].cost == 2);
        REQUIRE(arch->base.multiattacks.size() == 2);
        CHECK(arch->base.multiattacks[0].after_action == 0);
        const auto* fp = lib.spells()->find(rules5e::spell_id_of(std::string(row.id) + "#frightful_presence"));
        REQUIRE(fp);
        CHECK(arch->base.multiattacks[1].after_action == fp->id);
        CHECK(arch->base.multiattacks[1].attacks == arch->base.multiattacks[0].attacks);
        CHECK(fp->within_ft == 120);
        CHECK(fp->total_cover_saves);
        CHECK(fp->chooses_targets);
        CHECK(fp->aware_only);
        CHECK_FALSE(fp->repeat_los_disadvantage);
        CHECK(fp->fixed_dc == row.fp_dc);
        CHECK(fp->save_ability == rules5e::Ability::Wis);
        const auto* wing = lib.spells()->find(arch->base.legendary[2].ref_id);
        REQUIRE(wing);
        CHECK(wing->within_ft == 10);
        CHECK_FALSE(wing->total_cover_saves);
        CHECK_FALSE(wing->chooses_targets);
        CHECK_FALSE(wing->aware_only);
        CHECK(wing->fixed_dc == row.wa_dc);
        CHECK(wing->save_ability == rules5e::Ability::Dex);
        CHECK(wing->on_save == rules5e::OnSave::None);
        CHECK(wing->dice == rules5e::DiceExpr{2, 6, int16_t(row.wa_bonus)});
        CHECK(wing->damage_type == rules5e::DamageType::Bludgeoning);
        CHECK(wing->condition == Condition::Prone);
        CHECK(wing->condition_untracked);
        bool breath = false;
        for (const auto& sa : arch->save_actions) {
            const auto* def = lib.spells()->find(sa.spell_id);
            REQUIRE(def);
            if (def->id == fp->id || def->id == wing->id) continue;
            if (def->area == rules5e::AreaShape::None) continue;
            breath = true;
            CHECK(def->on_save == rules5e::OnSave::Half);
            CHECK(def->save_ability == row.breath_ability);
            CHECK(def->usage_kind == 1);   // recharge
        }
        CHECK(breath);
    }
}

TEST_CASE("L1 Frightful Presence is the Multiattack lead and does not spend a second action") {
    auto lib = library();
    const char* id = "srd51.monster.adult_white_dragon";
    const auto* arch = lib.find(id);
    REQUIRE(arch);
    const uint32_t fp = rules5e::spell_id_of(std::string(id) + "#frightful_presence");
    auto ch = arena({{id, 4, 4, ""}, {"t.l1.victim", 7, 5, "foes"}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 3);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, v = ch.spawns[1].stable_id;
    to_actor(*c, d);
    bool plain = false, lead_listed = false, fp_listed = false;
    for (const auto& q : c->legal_actions()) {
        if (q.kind == ActionKind::Attack && q.multiattack == 1) plain = true;
        if (q.kind == ActionKind::Attack && q.multiattack == 2) lead_listed = true;
        if (q.kind == ActionKind::CastSpell && q.spell_id == fp) fp_listed = true;
    }
    CHECK(plain);
    CHECK_FALSE(lead_listed);
    CHECK(fp_listed);
    Command direct;
    direct.request = {d, ActionKind::Attack, v, arch->base.multiattacks[1].attacks[0]};
    direct.request.multiattack = 2;
    auto no = go(*c, direct);
    CHECK_FALSE(no.ok);
    CHECK(c->state().budget.action);
    Command cast;
    cast.request = {d, ActionKind::CastSpell};
    cast.request.spell_id = fp;
    auto used = go(*c, cast);
    REQUIRE_MESSAGE(used.ok, used.error);
    CHECK_FALSE(c->state().budget.action);
    CHECK(c->state().budget.sequence == 2);
    CHECK(c->state().budget.attacks_remaining == 3);
    Command bite;
    bite.request = {d, ActionKind::Attack, v, arch->base.multiattacks[1].attacks[0]};
    REQUIRE(go(*c, bite).ok);
    CHECK(c->state().budget.attacks_remaining == 2);
}

TEST_CASE("L1 Wing Attack saves, damages, knocks prone, and the dragon does not fly") {
    auto lib = library();
    const char* id = "srd51.monster.adult_red_dragon";
    auto ch = arena({{id, 4, 4, ""}, {"t.l1.victim", 7, 5, "foes"}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 4);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, v = ch.spawns[1].stable_id;
    const Vec2i before = c->pos(d);
    const int alt = rules5e::find(c->state(), d)->alt_ft;
    offer(*c, d);
    CHECK(rules5e::find(c->state(), d)->la_cur == 3);
    Command w;
    w.request = {d, ActionKind::LegendaryAction};
    w.request.feature_id = 3;
    const std::size_t ev0 = c->events().size();
    auto used = go(*c, w);
    REQUIRE_MESSAGE(used.ok, used.error);
    CHECK(c->pos(d) == before);
    CHECK(rules5e::find(c->state(), d)->alt_ft == alt);
    CHECK(rules5e::find(c->state(), v)->has(Condition::Prone));
    CHECK(rules5e::find(c->state(), d)->la_cur == 1);
    bool saved = false, damaged = false, effect = false;
    for (std::size_t i = ev0; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == v) {
            saved = true;
            CHECK((e.rules.c & 1) == 0);   // Dex 1 cannot beat DC 22
        }
        if (e.rules.kind == EventKind::DamageDealt && e.rules.target == v) {
            damaged = true;
            CHECK(e.rules.a > 0);
        }
    }
    for (const auto& e : c->state().effects)
        if (e.target == v) effect = true;
    CHECK(saved);
    CHECK(damaged);
    CHECK_FALSE(effect);   // untimed prone: no effect entry
}

TEST_CASE("L1 Tail Attack is a legendary weapon attack") {
    auto lib = library();
    const char* id = "srd51.monster.adult_blue_dragon";
    auto ch = arena({{id, 4, 4, ""}, {"t.l1.victim", 7, 5, "foes"}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    const auto d = ch.spawns[0].stable_id, v = ch.spawns[1].stable_id;
    offer(*c, d);
    const int la = rules5e::find(c->state(), d)->la_cur;
    bool listed = false;
    for (const auto& q : c->legal_actions())
        if (q.kind == ActionKind::LegendaryAction && q.feature_id == 2 && q.target == v) listed = true;
    CHECK(listed);
    Command tail;
    tail.request = {d, ActionKind::LegendaryAction, v};
    tail.request.feature_id = 2;
    const std::size_t ev0 = c->events().size();
    auto used = go(*c, tail);
    REQUIRE_MESSAGE(used.ok, used.error);
    CHECK(rules5e::find(c->state(), d)->la_cur == la - 1);
    bool rolled = false, used_event = false;
    for (std::size_t i = ev0; i < c->events().size(); ++i) {
        const auto& e = c->events()[i];
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::AttackResolved && e.rules.actor == d && e.rules.target == v) rolled = true;
        if (e.rules.kind == EventKind::LegendaryActionUsed && e.rules.a == 2 && e.rules.b == 1) used_event = true;
    }
    CHECK(rolled);
    CHECK(used_event);
}

TEST_CASE("L1 Legendary Resistance spends the pool and turns a failed save into a success") {
    auto lib = library();
    Json zap = {{"$schema", "spell@1"}, {"id", "test.spell.lrzap"}, {"name", "zap"}, {"level", 0},
                {"school", "evocation"}, {"casting_time", {{"kind", "action"}}},
                {"range", {{"kind", "ranged"}, {"feet", 120}}}, {"components", {{"v", true}, {"s", true}}},
                {"duration", {{"kind", "instantaneous"}}},
                {"effects", Json::array({{{"kind", "save"}, {"ability", "wis"}, {"on_success", "none"},
                                          {"damage", {{"dice", "8d8"}, {"type", "force"}}}}})}};
    REQUIRE_FALSE(lib.add_spell(zap));
    Json wiz = load_json("content/packs/srd51.core/characters/wizard_level_1.json");
    wiz["id"] = "test.character.lrzap";
    wiz["spellcasting"]["dc"] = 30;   // adult white Wisdom save +6 cannot succeed
    wiz["spellcasting"]["spells"].push_back("test.spell.lrzap");
    REQUIRE_FALSE(lib.add(wiz, "character"));

    const char* id = "srd51.monster.adult_white_dragon";
    const uint32_t lr = rules5e::spell_id_of("legendary_resistance");
    auto ch = arena({{"test.character.lrzap", 10, 10, ""}, {id, 4, 4, ""}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 6);
    REQUIRE(c);
    const auto wiz_id = ch.spawns[0].stable_id, d = ch.spawns[1].stable_id;
    REQUIRE(rules5e::find(c->state(), d)->legendary_pool == lr);
    REQUIRE(rules5e::find_pool(c->state(), d, lr)->cur == 3);
    const int hp = rules5e::find(c->state(), d)->hp;
    to_actor(*c, wiz_id);
    Command cast;
    cast.request = {wiz_id, ActionKind::CastSpell, d};
    cast.request.spell_id = rules5e::spell_id_of("test.spell.lrzap");
    cast.request.targets = {d};
    auto used = go(*c, cast);
    REQUIRE_MESSAGE(used.ok, used.error);
    CHECK(rules5e::find_pool(c->state(), d, lr)->cur == 2);
    CHECK(rules5e::find(c->state(), d)->hp == hp);
    bool spent = false, succeeded = false;
    for (const auto& e : c->events()) {
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::ResourceSpent && e.rules.actor == d && e.rules.a == int32_t(lr)) {
            spent = true;
            CHECK(e.rules.b == 1);
            CHECK(e.rules.c == 2);
        }
        if (e.rules.kind == EventKind::SavingThrowRolled && e.rules.actor == d) {
            succeeded = true;
            CHECK((e.rules.c & 1) == 1);
        }
    }
    CHECK(spent);
    CHECK(succeeded);
}
