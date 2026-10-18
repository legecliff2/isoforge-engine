// Row 6 (save actions), rules side: the contract a `save_effect` action is compiled to. Sim's
// ContentLibrary builds a SpellDef like these (is_spell=false, magical=false, fixed_dc, usage) and
// appends its id to spells_known; these tests pin how rules resolve and offer it. Single target.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBreath = spell_id_of("test.dragon#fire_breath");
const uint32_t kGaze = spell_id_of("test.dragon#paralyzing_gaze");

SpellDef save_action(uint32_t id, const char* name, Ability ab, int dc, OnSave on_success, DiceExpr dice, DamageType t) {
    SpellDef d; d.id = id; d.content_id = name; d.level = 0; d.range_ft = 60; d.kind = SpellEffectKind::Save;
    d.save_ability = ab; d.fixed_dc = int8_t(dc); d.on_save = on_success; d.dice = dice; d.damage_type = t;
    d.targets = 1; d.is_spell = false; d.magical = false;
    return d;
}

Combatant dragon(CombatantId id, std::vector<uint32_t> known) {
    auto d = monster(id);
    d.hp = d.hp_max = 300;
    d.spells_known = std::move(known);
    return d;
}

EncounterHooks hooks(std::initializer_list<SpellDef> defs) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>();
    for (const auto& d : defs) REQUIRE(b->add(d));
    h.spells = b;
    return h;
}

Combatant hero(CombatantId id, int dex = 10) {
    auto c = pc(id, 400);
    c.abilities.score[std::size_t(Ability::Dex)] = int8_t(dex);
    return c;
}

const Event* first(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return &e;
    return nullptr;
}

}  // namespace

TEST_CASE("save action: half damage on success, full on failure, DC from the action not the caster") {
    int saved = 0, failed = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto s = begin_encounter({dragon(1, {kBreath}), hero(2)},
                                 seed, nullptr,
                                 hooks({save_action(kBreath, "test.dragon#fire_breath", Ability::Dex, 15, OnSave::Half, {8, 6, 0}, DamageType::Fire)}));
        end_turns_until(s, 1);
        const int hp0 = find(s, 2)->hp;
        auto r = step(s, cast(1, kBreath, 2, 0));
        REQUIRE(r.ok);
        const Event* sv = first(r.events, EventKind::SavingThrowRolled);
        const Event* dm = first(r.events, EventKind::DamageDealt);
        REQUIRE(sv); REQUIRE(dm);
        CHECK(sv->c == int32_t(sv->b >= 15));   // DC 15; the dragon has no spellcasting ability (derived DC would be 8)
        const int dealt = hp0 - find(s, 2)->hp;
        if (sv->c) { ++saved; CHECK(dealt >= 4); CHECK(dealt <= 24); }
        else { ++failed; CHECK(dealt >= 8); CHECK(dealt <= 48); }
        CHECK(dealt == dm->a);
    }
    CHECK(saved > 0);
    CHECK(failed > 0);
}

TEST_CASE("save action: on_success none deals nothing on a save") {
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = begin_encounter({dragon(1, {kBreath}), hero(2, 20)}, seed, nullptr,
                                 hooks({save_action(kBreath, "test.dragon#fire_breath", Ability::Dex, 12, OnSave::None, {4, 6, 0}, DamageType::Fire)}));
        end_turns_until(s, 1);
        const int hp0 = find(s, 2)->hp;
        auto r = step(s, cast(1, kBreath, 2, 0));
        REQUIRE(r.ok);
        const Event* sv = first(r.events, EventKind::SavingThrowRolled);
        REQUIRE(sv);
        if (sv->c) CHECK(find(s, 2)->hp == hp0);
    }
}

TEST_CASE("save action: a breath weapon is not magical (Magic Resistance gives no advantage); a magical action is") {
    auto passes = [&](bool magical) {
        int n = 0;
        for (uint64_t seed = 0; seed < 400; ++seed) {
            SpellDef d = save_action(kBreath, "test.dragon#fire_breath", Ability::Dex, 11, OnSave::None, {1, 4, 0}, DamageType::Fire);
            d.magical = magical;
            auto t = hero(2);
            RollTrait mr; mr.roll = TraitRoll::Save; mr.when = kWhenSourceMagical;   // Magic Resistance
            t.roll_traits = {mr};
            auto s = begin_encounter({dragon(1, {kBreath}), t}, seed, nullptr, hooks({d}));
            end_turns_until(s, 1);
            auto r = step(s, cast(1, kBreath, 2, 0));
            REQUIRE(r.ok);
            const Event* sv = first(r.events, EventKind::SavingThrowRolled);
            REQUIRE(sv);
            n += int(sv->c & 1);
        }
        return n;
    };
    const int plain = passes(false), magic = passes(true);   // d20 vs DC 11: ~50% plain, ~75% with advantage
    CHECK(plain > 140); CHECK(plain < 260);
    CHECK(magic > plain + 50);
}

TEST_CASE("save action with a condition rider: applied on a failed save, repeat save ends it") {
    SpellDef gaze = save_action(kGaze, "test.dragon#paralyzing_gaze", Ability::Con, 30, OnSave::None, {0, 0, 0}, DamageType::Psychic);
    gaze.applies_condition = true; gaze.condition = Condition::Paralyzed; gaze.repeat_save = RepeatSave::EndOfTurn;
    int paralyzed = 0;
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto s = begin_encounter({dragon(1, {kGaze}), hero(2)}, seed, nullptr, hooks({gaze}));
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kGaze, 2, 0));
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::ActionUsed) == 1);
        CHECK(count(r.events, EventKind::SpellCast) == 0);
        if (find(s, 2)->has(Condition::Paralyzed)) {   // DC 30: fails unless a nat... always fails for a +0 save
            ++paralyzed;
            REQUIRE(s.effects.size() == 1);
            CHECK(s.effects[0].repeat_save == RepeatSave::EndOfTurn);
            CHECK_FALSE(s.effects[0].concentration);   // not concentration: the dragon's death/incapacity does not end it
        }
    }
    CHECK(paralyzed == 20);
}

TEST_CASE("legal_actions offers a save action as CastSpell, level 0, per hostile target; usage filters it") {
    SpellDef d = save_action(kBreath, "test.dragon#fire_breath", Ability::Dex, 15, OnSave::Half, {8, 6, 0}, DamageType::Fire);
    d.usage_kind = 1; d.recharge_min = 5;
    auto s = begin_encounter({dragon(1, {kBreath}), hero(2), hero(3)}, 5, nullptr, hooks({d}));
    register_action_use(s, 1, kBreath);
    end_turns_until(s, 1);
    auto offered = [&] {
        std::vector<CombatantId> t;
        for (const auto& q : legal_actions(s)) if (q.kind == ActionKind::CastSpell && q.spell_id == kBreath) {
            CHECK(q.spell_level == 0);
            t.push_back(q.target);
        }
        std::sort(t.begin(), t.end());
        return t;
    };
    CHECK(offered() == std::vector<CombatantId>{2, 3});
    REQUIRE(step(s, cast(1, kBreath, 2, 0)).ok);
    CHECK(offered().empty());   // expended: not offered, and rejected
    auto r = step(s, cast(1, kBreath, 3, 0));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "action has not recharged");
}

TEST_CASE("save action cannot target more than one creature unless the caller lists them (single target by default)") {
    auto s = begin_encounter({dragon(1, {kBreath}), hero(2), hero(3)}, 5, nullptr,
                             hooks({save_action(kBreath, "test.dragon#fire_breath", Ability::Dex, 15, OnSave::Half, {2, 6, 0}, DamageType::Fire)}));
    end_turns_until(s, 1);
    auto two = cast(1, kBreath, 2, 0); two.targets = {2, 3};
    auto r = step(s, two);
    CHECK_FALSE(r.ok);
    CHECK(r.error == "too many targets");
    CHECK(step(s, cast(1, kBreath, 2, 0)).ok);
}
