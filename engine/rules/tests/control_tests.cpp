// VERSION_BUMP_BATCH 7.2v: control conditions (SRD 5.1 Turn Undead / Destroy Undead, Command, Fear, Hideous Laughter).
// Rules restricts the legal actions of the controlled creature; sim picks its path (away from / toward the caster).
#include <doctest/doctest.h>

#include "spell_util.hpp"
#include <span>

using namespace enc;

namespace {

const uint32_t kTurn = spell_id_of("test.turn_undead");
const uint32_t kDestroy = spell_id_of("test.destroy_undead");
const uint32_t kCommand = spell_id_of("test.command");
const uint32_t kFear = spell_id_of("test.fear");
const uint32_t kLaugh = spell_id_of("test.hideous_laughter");
const uint32_t kBlast = spell_id_of("test.blast");

SpellDef base(uint32_t id, const char* name, int dc) {
    SpellDef d; d.id = id; d.content_id = name; d.level = 1; d.range_ft = 60; d.kind = SpellEffectKind::Save;
    d.save_ability = Ability::Wis; d.fixed_dc = int8_t(dc); d.targets = 1; d.is_spell = false; d.magical = true;
    d.components = kComponentV;
    return d;
}

std::vector<SpellDef> defs(int dc) {
    SpellDef turn = base(kTurn, "test.turn_undead", dc);
    turn.control = ControlKind::Turned; turn.target_types = bit(CreatureType::Undead);
    turn.expiry = uint8_t(Expiry::Rounds); turn.duration_rounds = 10; turn.targets = 6;   // every undead sim finds in 30 ft
    SpellDef destroy = turn; destroy.id = kDestroy; destroy.content_id = "test.destroy_undead"; destroy.destroy_cr8_max = 4;
    SpellDef cmd = base(kCommand, "test.command", dc);
    cmd.control = ControlKind::Command; cmd.control_excluded = bit(CreatureType::Undead);
    cmd.expiry = uint8_t(Expiry::EndOfTargetNextTurn);
    SpellDef fear = base(kFear, "test.fear", dc);
    fear.control = ControlKind::Fear; fear.applies_condition = true; fear.condition = Condition::Frightened;
    fear.repeat_save = RepeatSave::EndOfTurnUnseen;
    SpellDef laugh = base(kLaugh, "test.hideous_laughter", dc);
    laugh.control = ControlKind::Laughter; laugh.applies_condition = true; laugh.condition = Condition::Prone;
    laugh.repeat_save = RepeatSave::EndOfTurnOrDamageAdv; laugh.control_min_int = 4;
    laugh.targets = 2;   // two targets only so one test covers the Int 4 skip
    SpellDef blast = base(kBlast, "test.blast", 30); blast.save_ability = Ability::Dex; blast.dice = {1, 1, 0};
    blast.damage_type = DamageType::Fire;
    return {turn, destroy, cmd, fear, laugh, blast};
}

EncounterHooks hooks(int dc, int see = -1) {
    EncounterHooks h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*srd_spellbook());
    for (const auto& d : defs(dc)) REQUIRE(b->add(d));
    h.spells = b;
    if (see >= 0) h.can_see = [see](const EncounterState&, const Combatant&, const Combatant&) { return see == 1; };
    return h;
}

Combatant caster() {
    auto c = wizard(1);
    c.spells_known.insert(c.spells_known.end(), {kTurn, kDestroy, kCommand, kFear, kLaugh, kBlast});
    c.abilities.score[0] = 30;   // initiative order is not the point; dex below
    c.abilities.score[1] = 30;   // acts first
    return c;
}

Combatant target(CombatantId id, CreatureType t = CreatureType::Humanoid, int wis = 10, int cr8 = -1) {
    auto c = monster(id); c.creature_type = t; c.abilities.score[4] = int8_t(wis); c.cr8 = int16_t(cr8); c.hp = c.hp_max = 40;
    return c;
}

EncounterState start(std::vector<Combatant> cs, int dc = 30, int see = -1, uint64_t seed = 3) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks(dc, see));
    end_turns_until(s, 1);
    return s;
}

const ActiveSpellEffect* control_on(const EncounterState& s, CombatantId t) {
    for (const auto& e : s.effects) if (e.target == t && e.control != 0) return &e;
    return nullptr;
}

ActionRequest cmd(CommandWord w, CombatantId t = 2) { auto r = cast(1, kCommand, t, 1); r.command_word = uint8_t(w); return r; }

}  // namespace

TEST_CASE("Turn Undead: an undead that fails is turned; Dash / Dodge / Move only, no attack; damage ends it") {
    auto s = start({caster(), target(2, CreatureType::Undead)});
    REQUIRE(step(s, cast(1, kTurn, 2, 1)).ok);
    const auto* e = control_on(s, 2);
    REQUIRE(e); CHECK(e->control == uint8_t(ControlKind::Turned)); CHECK(e->ends_on_damage); CHECK(e->no_condition);
    CHECK_FALSE(find(s, 2)->has(Condition::Frightened));
    end_turns_until(s, 2);
    CHECK(step(s, {2, ActionKind::Attack, 1}).error == "turned: move, Dash, escape or Dodge only");
    CHECK(step(s, {2, ActionKind::Dash}).ok);
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kBlast, 2, 1)).ok);   // damage ends the turning
    CHECK(control_on(s, 2) == nullptr);
}

TEST_CASE("Turn Undead: a living creature can't be targeted; a success leaves no effect") {
    auto s = start({caster(), target(2)});
    CHECK_FALSE(step(s, cast(1, kTurn, 2, 1)).ok);
    auto t = start({caster(), target(2, CreatureType::Undead, 30)}, 1);
    REQUIRE(step(t, cast(1, kTurn, 2, 1)).ok);
    CHECK(control_on(t, 2) == nullptr);
}

TEST_CASE("Destroy Undead hook: CR 1/2 or lower is destroyed on a failure, CR 1 is only turned, unknown CR never") {
    auto s = start({caster(), target(2, CreatureType::Undead, 10, 4), target(3, CreatureType::Undead, 10, 8),
                    target(4, CreatureType::Undead, 10, -1)});
    auto r = cast(1, kDestroy, 2, 1); r.targets = {2, 3, 4};
    REQUIRE(step(s, r).ok);
    CHECK(find(s, 2)->dead); CHECK(control_on(s, 2) == nullptr);
    CHECK_FALSE(find(s, 3)->dead); CHECK(control_on(s, 3) != nullptr);
    CHECK_FALSE(find(s, 4)->dead); CHECK(control_on(s, 4) != nullptr);
}

TEST_CASE("Command: the word is required (1..5) and only on a Command spell; undead are unaffected") {
    auto s = start({caster(), target(2), target(3, CreatureType::Undead)});
    CHECK(step(s, cast(1, kCommand, 2, 1)).error == "command_word: 1..5 for a Command spell only");
    CHECK(step(s, cmd(CommandWord(6))).error == "command_word: 1..5 for a Command spell only");
    auto f = cast(1, kFear, 2, 1); f.command_word = 1;
    CHECK(step(s, f).error == "command_word: 1..5 for a Command spell only");
    auto r = cmd(CommandWord::Halt, 3);
    REQUIRE(step(s, r).ok);
    CHECK(control_on(s, 3) == nullptr);   // undead: no save, no effect
}

TEST_CASE("Command Halt / Drop: only EndTurn on the target's next turn, then it ends") {
    for (auto w : {CommandWord::Halt, CommandWord::Drop}) {
        auto s = start({caster(), target(2)});
        REQUIRE(step(s, cmd(w)).ok);
        REQUIRE(control_on(s, 2));
        CHECK(control_on(s, 2)->command_word == uint8_t(w));
        end_turns_until(s, 2);
        CHECK(step(s, {2, ActionKind::Move, 0, 0, 5}).error == "command: the turn ends");
        CHECK(step(s, {2, ActionKind::Dodge}).error == "command: the turn ends");
        REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
        CHECK(control_on(s, 2) == nullptr);
    }
}

TEST_CASE("Command Grovel: prone at the start of its turn; Approach / Flee: movement only") {
    auto s = start({caster(), target(2)});
    REQUIRE(step(s, cmd(CommandWord::Grovel)).ok);
    end_turns_until(s, 2);
    CHECK(find(s, 2)->has(Condition::Prone));
    CHECK_FALSE(step(s, {2, ActionKind::StandUp}).ok);
    for (auto w : {CommandWord::Approach, CommandWord::Flee}) {
        auto t = start({caster(), target(2)});
        REQUIRE(step(t, cmd(w)).ok);
        end_turns_until(t, 2);
        CHECK(step(t, {2, ActionKind::Attack, 1}).error == "command: move only");
        CHECK(step(t, {2, ActionKind::Dash}).ok);
        CHECK(step(t, {2, ActionKind::Move, 0, 0, 5}).ok);
    }
}

TEST_CASE("Fear: frightened, Dash / Dodge / Move only; end-of-turn save only without line of sight to the caster") {
    auto seen = start({caster(), target(2, CreatureType::Humanoid, 10)}, 30, 1);
    REQUIRE(step(seen, cast(1, kFear, 2, 1)).ok);
    CHECK(find(seen, 2)->has(Condition::Frightened));
    end_turns_until(seen, 2);
    CHECK(step(seen, {2, ActionKind::Attack, 1}).error == "fear: Dash away or Dodge only");
    CHECK(step(seen, {2, ActionKind::Dash}).ok);
    auto end = step(seen, {2, ActionKind::EndTurn});
    REQUIRE(end.ok);
    CHECK(count(end.events, EventKind::SavingThrowRolled) == 0);   // sees the caster: no save
    CHECK(control_on(seen, 2) != nullptr);

    // unseen: the save is rolled at the end of its turn (DC 30 fails, so it stays)
    auto unseen = start({caster(), target(2, CreatureType::Humanoid, 10)}, 30, 0);
    REQUIRE(step(unseen, cast(1, kFear, 2, 1)).ok);
    end_turns_until(unseen, 2);
    auto e2 = step(unseen, {2, ActionKind::EndTurn});
    CHECK(count(e2.events, EventKind::SavingThrowRolled) == 1);
}

TEST_CASE("Hideous Laughter: prone + incapacitated, can't stand; Int 4 or less unaffected; damage forces a save with advantage") {
    auto s = start({caster(), target(2), [] { auto c = target(3); c.abilities.score[3] = 4; return c; }()});
    auto r = cast(1, kLaugh, 2, 1); r.targets = {2, 3};
    REQUIRE(step(s, r).ok);
    CHECK(find(s, 2)->has(Condition::Prone)); CHECK(find(s, 2)->has(Condition::Incapacitated));
    CHECK_FALSE(find(s, 3)->has(Condition::Prone)); CHECK(control_on(s, 3) == nullptr);
    end_turns_until(s, 2);
    CHECK_FALSE(step(s, {2, ActionKind::StandUp}).ok);
    auto end = step(s, {2, ActionKind::EndTurn});
    CHECK(count(end.events, EventKind::SavingThrowRolled) == 1);   // end of each turn
    end_turns_until(s, 1);
    auto b = step(s, cast(1, kBlast, 2, 1));
    REQUIRE(b.ok);
    int saves = 0; for (const auto& e : b.events) saves += e.kind == EventKind::SavingThrowRolled && e.actor == 2;
    CHECK(saves == 2);   // the blast's Dex save, then the laughter's repeat save on damage
}

TEST_CASE("Hideous Laughter: a successful repeat save ends incapacitated; it stays prone and may stand up") {
    auto s = start({caster(), target(2, CreatureType::Humanoid, 10)});
    REQUIRE(step(s, cast(1, kLaugh, 2, 1)).ok);
    REQUIRE(control_on(s, 2));
    s.effects.back().dc = -20;   // the repeat save now always succeeds
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    CHECK(control_on(s, 2) == nullptr);
    CHECK_FALSE(find(s, 2)->has(Condition::Incapacitated));
    CHECK(find(s, 2)->has(Condition::Prone));
    end_turns_until(s, 2);
    CHECK(step(s, {2, ActionKind::StandUp}).ok);
}

TEST_CASE("control: leftover_digest folds control effects only while one exists; replay with a command word matches") {
    auto plain = start({caster(), target(2)});
    const uint64_t before = leftover_digest(plain);
    CHECK_FALSE(has_leftover_state(plain));
    auto make = [] { return start({caster(), target(2)}); };
    auto a = make(), b = make();
    CHECK(leftover_digest(a) == before);
    ActionLog log;
    REQUIRE(step_logged(a, cmd(CommandWord::Halt), log).ok);
    CHECK(has_leftover_state(a));
    CHECK(leftover_digest(a) != before);
    for (const auto& ls : log) { auto r = replay_step(b, ls); REQUIRE(r.ok); CHECK_FALSE(r.desync); }
    CHECK(leftover_digest(a) == leftover_digest(b));
    REQUIRE(control_on(b, 2)); CHECK(control_on(b, 2)->command_word == uint8_t(CommandWord::Halt));
}

TEST_CASE("control: legal_actions lists only what the control condition allows") {
    auto s = start({caster(), target(2)});
    REQUIRE(step(s, cmd(CommandWord::Halt)).ok);
    end_turns_until(s, 2);
    for (const auto& r : legal_actions(s)) CHECK(r.kind == ActionKind::EndTurn);
    auto t = start({caster(), target(2, CreatureType::Undead)});
    REQUIRE(step(t, cast(1, kTurn, 2, 1)).ok);
    end_turns_until(t, 2);
    for (const auto& r : legal_actions(t)) CHECK(r.kind != ActionKind::Attack);
}

TEST_CASE("control: legal_actions lists a Command cast once per word, and each is accepted") {
    auto s = start({caster(), target(2)});
    int words = 0;
    for (const auto& r : legal_actions(s))
        if (r.kind == ActionKind::CastSpell && r.spell_id == kCommand) { ++words; CHECK(r.command_word >= 1); CHECK(r.command_word <= 5); }
    CHECK(words % 5 == 0); CHECK(words > 0);
    for (const auto& r : legal_actions(s))
        if (r.kind == ActionKind::CastSpell && r.spell_id == kCommand) { auto t = s; CHECK(step(t, r).ok); }
}

// ---- EC review of #188 ----
TEST_CASE("Turned: no reactions (no opportunity attack offered); not turned, the window opens") {
    for (bool turned : {false, true}) {
        EncounterHooks h = hooks(30);
        h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
        h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&, std::span<const ReactionOption>) {
            return ReactionDecision{0, true, 0}; };
        auto s = begin_encounter({caster(), target(2, CreatureType::Undead)}, 3, nullptr, h);
        end_turns_until(s, 1);
        if (turned) REQUIRE(step(s, cast(1, kTurn, 2, 1)).ok);
        ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5; mv.leaves_reach = true;
        auto r = step(s, mv);
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::ReactionWindowOpened) == (turned ? 0 : 1));
    }
}

TEST_CASE("Turned: the escape (Attack special 4) is allowed") {
    auto s = start({caster(), target(2, CreatureType::Undead)});
    REQUIRE(step(s, cast(1, kTurn, 2, 1)).ok);
    end_turns_until(s, 2);
    add_condition(mut(s, 2), Condition::Grappled);
    ActionRequest esc{2, ActionKind::Attack}; esc.special = 4; esc.dc = 10;
    CHECK(step(s, esc).ok);
}

TEST_CASE("Fear: no can_see hook means seen (no end-of-turn save)") {
    auto s = start({caster(), target(2)});
    REQUIRE(step(s, cast(1, kFear, 2, 1)).ok);
    end_turns_until(s, 2);
    auto e = step(s, {2, ActionKind::EndTurn});
    REQUIRE(e.ok);
    CHECK(count(e.events, EventKind::SavingThrowRolled) == 0);
}

TEST_CASE("Destroy Undead: a successful save leaves no effect and no kill") {
    auto s = start({caster(), target(2, CreatureType::Undead, 30, 4)}, 1);
    REQUIRE(step(s, cast(1, kDestroy, 2, 1)).ok);
    CHECK_FALSE(find(s, 2)->dead);
    CHECK(control_on(s, 2) == nullptr);
}

TEST_CASE("Hideous Laughter: the damage-triggered save has advantage (natural = the better of two d20s)") {
    int greater = 0;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        auto natural_of = [&](bool adv) {
            auto s = start({caster(), target(2)}, 30, -1, seed);
            REQUIRE(step(s, cast(1, kLaugh, 2, 1)).ok);
            REQUIRE(control_on(s, 2));
            REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
            end_turns_until(s, 1);   // the target's end-of-turn save is the same (normal) in both runs
            REQUIRE(control_on(s, 2));
            if (!adv) for (auto& e : s.effects) if (e.control != 0) e.repeat_save = RepeatSave::OnDamage;   // same save, no advantage
            auto b = step(s, cast(1, kBlast, 2, 1));
            INFO(b.error);
            REQUIRE(b.ok);
            int n = 0, k = 0;
            for (const auto& e : b.events) if (e.kind == EventKind::SavingThrowRolled && e.actor == 2 && ++k == 2) n = e.a;
            return n;
        };
        const int a = natural_of(true), n = natural_of(false);
        CHECK(a >= n);
        greater += a > n;
    }
    CHECK(greater > 0);
}

TEST_CASE("Hideous Laughter: an Incapacitated from another source is not its own and survives its end") {
    auto s = start({caster(), target(2, CreatureType::Humanoid, 10)});
    add_condition(mut(s, 2), Condition::Incapacitated);   // another source
    REQUIRE(step(s, cast(1, kLaugh, 2, 1)).ok);
    REQUIRE(control_on(s, 2));
    CHECK_FALSE(control_on(s, 2)->has_also);
    s.effects.back().dc = -20;
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    CHECK(control_on(s, 2) == nullptr);
    CHECK(find(s, 2)->has(Condition::Incapacitated));
}
