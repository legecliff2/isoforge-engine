// VERSION_BUMP_BATCH 7.2zh: Branding Smite (weapon_smite), Heat Metal held_object, Flame Blade (summon_weapon.held).
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kSmite = spell_id_of("test.zh.branding_smite");
const uint32_t kSmitePlain = spell_id_of("test.zh.branding_plain");
const uint32_t kSmiteRounds = spell_id_of("test.zh.branding_rounds");
const uint32_t kHeatFail = spell_id_of("test.zh.heat_fail");
const uint32_t kHeatPass = spell_id_of("test.zh.heat_pass");
const uint32_t kBlade = spell_id_of("test.zh.flame_blade");
const uint32_t kVanish = spell_id_of("test.zh.vanish");
const uint32_t kSmiteExtra = spell_id_of("test.zh.smite_extra");

enum : uint32_t { kItemSword = 1, kItemTorch = 2, kItemArrows = 3, kItemClub = 4, kItemArmor = 5, kItemShield = 6, kItemFocus = 7 };

struct World {
    std::map<uint32_t, ItemDef> defs;
    std::map<CombatantId, LoadoutView> views;
    std::vector<ActionRequest> applied;
    bool drop_removes = true;
};

std::shared_ptr<World> make_world() {
    auto w = std::make_shared<World>();
    auto add = [&](uint32_t id, ItemKind k, ItemMaterial mat = ItemMaterial::Absent) {
        ItemDef d; d.id = id; d.kind = k; d.material = mat; w->defs[id] = d;
    };
    add(kItemSword, ItemKind::Weapon, ItemMaterial::Metal); add(kItemTorch, ItemKind::Light); add(kItemArrows, ItemKind::Ammunition);
    add(kItemClub, ItemKind::Weapon, ItemMaterial::Metal); add(kItemArmor, ItemKind::Armor, ItemMaterial::Metal);
    add(kItemShield, ItemKind::Shield, ItemMaterial::Metal); add(kItemFocus, ItemKind::Focus);
    return w;
}

SpellDef smite_def(uint32_t id, const char* content, bool reveal, uint16_t dim, bool concentration, DiceExpr dice) {
    SpellDef d; d.id = id; d.content_id = content; d.level = 2; d.range_ft = 0;
    d.casting_time = CastingTime::BonusAction; d.concentration = concentration;
    d.kind = SpellEffectKind::WeaponSmite; d.dice = dice; d.damage_type = DamageType::Radiant; d.upcast_dice = 1;
    d.weapon_smite.reveal_invisible = reveal; d.weapon_smite.dim_ft = dim;
    d.duration = concentration ? SpellDuration::Minutes : SpellDuration::Rounds;
    d.duration_amount = concentration ? uint16_t(1) : uint16_t(10);
    return d;
}
SpellDef heat_def(uint32_t id, const char* content, int8_t dc) {
    SpellDef d; d.id = id; d.content_id = content; d.level = 2; d.range_ft = 60; d.concentration = true;
    d.kind = SpellEffectKind::SustainedDamage; d.dice = DiceExpr{2, 8, 0}; d.upcast_dice = 1;
    d.damage_type = DamageType::Fire; d.held_object = true; d.fixed_dc = dc;
    d.duration = SpellDuration::Minutes; d.duration_amount = 1;
    return d;
}

EncounterHooks zh_hooks(std::shared_ptr<World> w) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(smite_def(kSmite, "test.zh.branding_smite", true, 5, true, DiceExpr{1, 1, 0}));
    book->add(smite_def(kSmitePlain, "test.zh.branding_plain", false, 0, true, DiceExpr{1, 1, 0}));
    book->add(smite_def(kSmiteRounds, "test.zh.branding_rounds", true, 5, false, DiceExpr{1, 1, 0}));
    book->add(heat_def(kHeatFail, "test.zh.heat_fail", int8_t(30)));
    book->add(heat_def(kHeatPass, "test.zh.heat_pass", int8_t(1)));
    SpellDef fb; fb.id = kBlade; fb.content_id = "test.zh.flame_blade"; fb.level = 2; fb.range_ft = 0;
    fb.casting_time = CastingTime::BonusAction; fb.concentration = true; fb.kind = SpellEffectKind::SummonWeapon;
    fb.dice = DiceExpr{1, 1, 0}; fb.damage_type = DamageType::Fire; fb.melee = true; fb.fixed_attack = int8_t(30);
    fb.upcast_dice = 1; fb.summon_weapon.held = true; fb.summon_weapon.upcast_every = 2; fb.summon_weapon.reach_ft = 5;
    fb.summon_weapon.move_ft = 0; fb.summon_weapon.light_bright_ft = 10; fb.summon_weapon.light_dim_ft = 10;
    fb.duration = SpellDuration::Minutes; fb.duration_amount = 10;
    book->add(fb);
    SpellDef van; van.id = kVanish; van.content_id = "test.zh.vanish"; van.level = 1; van.range_ft = 60;
    van.kind = SpellEffectKind::ApplyCondition; van.applies_condition = true; van.condition = Condition::Invisible;
    van.duration = SpellDuration::Minutes; van.duration_amount = 1;
    book->add(van);
    SpellDef extra; extra.id = kSmiteExtra; extra.content_id = "test.zh.smite_extra"; extra.level = 1; extra.range_ft = 5;
    extra.kind = SpellEffectKind::Heal; extra.dice = DiceExpr{1, 4, 0};
    SpellEffect rider; rider.kind = SpellEffectKind::WeaponSmite;
    extra.extra_effects = {rider};
    book->add(extra);
    h.spells = book;
    if (w) {
        h.item_def = [w](uint32_t id) -> const ItemDef* { auto it = w->defs.find(id); return it == w->defs.end() ? nullptr : &it->second; };
        h.loadout_of = [w](const EncounterState&, CombatantId id) -> const LoadoutView* {
            auto it = w->views.find(id); return it == w->views.end() ? nullptr : &it->second;
        };
        h.apply_equip = [w](EncounterState&, const ActionRequest& r, uint8_t, std::vector<Event>&) {
            w->applied.push_back(r);
            if (!w->drop_removes || r.equip_op != uint8_t(EquipOp::Drop)) return;
            auto& v = w->views[r.actor];
            if (v.main.instance == r.equip_item) v.main = {};
            if (v.off.instance == r.equip_item) v.off = {};
        };
    }
    return h;
}

Combatant wiz() {
    auto w = wizard(1);
    w.spells_known = {kSmite, kSmitePlain, kSmiteRounds, kHeatFail, kHeatPass, kBlade, kVanish, kSmiteExtra, kFireBolt, kHoldPerson};
    w.spell_slots_max[4] = 1; w.spell_slots = w.spell_slots_max;
    never_react(w);
    return w;
}
Combatant mob() { auto m = monster(2); m.hp = m.hp_max = 300; m.ac = 10; never_react(m); return m; }
EncounterState start(std::shared_ptr<World> w, uint64_t seed = 1) {
    auto s = begin_encounter({wiz(), mob()}, seed, nullptr, zh_hooks(w));
    end_turns_until(s, 1);
    return s;
}
ActionRequest heat(uint32_t spell, CombatantId target, uint32_t item, uint8_t level = 2) {
    ActionRequest r = cast(1, spell, target, level); r.equip_item = item; return r;
}
bool hit_by(const std::vector<Event>& ev, CombatantId actor) {
    for (const auto& e : ev) if (e.kind == EventKind::AttackResolved && e.actor == actor) return (e.c & 1) != 0;
    return false;
}
int natural_of(const std::vector<Event>& ev, EventKind k, CombatantId actor) {
    for (const auto& e : ev) if (e.kind == k && e.actor == actor) return e.a;
    return -1;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
const ActiveSpellEffect* effect_of(const EncounterState& s, uint32_t spell) {
    for (const auto& e : s.effects) if (e.spell_id == spell) return &e;
    return nullptr;
}
void flat_weapon(EncounterState& s) {
    mut(s, 1).crit_min = int8_t(2);
    mut(s, 1).attacks[0].to_hit = int8_t(30);
    mut(s, 1).attacks[0].damage = DiceExpr{0, 0, 5};
}
}  // namespace

TEST_CASE("7.2zh weapon_smite: a weapon hit adds the dice (a crit doubles them) and brands; a spell attack does not") {
    CHECK(uint8_t(SpellEffectKind::TempHp) == 24);
    CHECK(uint8_t(SpellEffectKind::WeaponSmite) == 25);
    bool saw = false;
    for (uint64_t seed = 1; seed <= 40 && !saw; ++seed) {
        auto s = start(nullptr, seed);
        flat_weapon(s);
        REQUIRE(step(s, cast(1, kSmite, 1, 2)).ok);
        CHECK_FALSE(s.budget.bonus_action); CHECK(s.budget.action);
        CHECK(find(s, 1)->concentration_spell == kSmite);
        REQUIRE(has_smite_state(s));
        const auto* pending = effect_of(s, kSmite);
        REQUIRE(pending); CHECK(pending->smite_pending); CHECK(pending->smite_reveal); CHECK(pending->smite_dim_ft == 5);
        CHECK(pending->target == 1); CHECK(pending->damage_when == 0); CHECK(pending->damage.count == 1);
        const uint64_t digest_pending = smite_digest(s);
        REQUIRE(step(s, cast(1, kFireBolt, 2)).ok);   // a spell attack does not spend the charge
        CHECK(effect_of(s, kSmite)->smite_pending);
        (void)step(s, ActionRequest{1, ActionKind::EndTurn});
        end_turns_until(s, 1);
        ActiveSpellEffect inv{kVanish, 2, 2, Condition::Invisible, Ability::Wis, 0, RepeatSave::None, true};
        s.effects.push_back(inv);
        add_condition(mut(s, 2), Condition::Invisible);
        mut(s, 2).concentration_spell = kVanish;
        const int before = mut(s, 2).hp;
        ActionRequest atk{1, ActionKind::Attack, 2};
        auto r = step(s, atk);
        REQUIRE(r.ok);
        if (!hit_by(r.events, 1)) { CHECK(effect_of(s, kSmite)->smite_pending); continue; }
        saw = true;
        CHECK(before - mut(s, 2).hp == 7);   // flat 5, plus a crit-doubled 1d1
        CHECK(dealt_to(r.events, 2) == 7);
        const auto* brand = effect_of(s, kSmite);
        REQUIRE(brand); CHECK_FALSE(brand->smite_pending); CHECK(brand->smite_brand); CHECK(brand->smite_reveal);
        CHECK(brand->target == 2); CHECK(brand->smite_dim_ft == 5);
        CHECK(smite_digest(s) != digest_pending);
        CHECK_FALSE(find(s, 2)->has(Condition::Invisible));
        CHECK(find(s, 2)->concentration_spell == 0);
        int removed = 0;
        for (const auto& e : r.events) removed += e.kind == EventKind::ConditionRemoved && e.a == int32_t(Condition::Invisible);
        CHECK(removed == 1);
        (void)step(s, ActionRequest{1, ActionKind::EndTurn});
        end_turns_until(s, 1);
        REQUIRE(step(s, cast(1, kVanish, 2, 1)).ok);   // the brand refuses a new Invisible
        CHECK_FALSE(find(s, 2)->has(Condition::Invisible));
        (void)step(s, ActionRequest{1, ActionKind::EndTurn});
        end_turns_until(s, 1);
        REQUIRE(step(s, cast(1, kHoldPerson, 2, 2)).ok);   // a new concentration spell ends the brand
        CHECK_FALSE(has_smite_state(s));
        (void)step(s, ActionRequest{1, ActionKind::EndTurn});
        end_turns_until(s, 1);
        REQUIRE(step(s, cast(1, kVanish, 2, 1)).ok);
        CHECK(find(s, 2)->has(Condition::Invisible));
    }
    CHECK(saw);
}

TEST_CASE("7.2zh weapon_smite: no reveal and no dim ends the charge on the hit; upcast, self only, primary only") {
    auto s = start(nullptr, 1);
    const auto slots = find(s, 1)->spell_slots;
    auto other = step(s, cast(1, kSmite, 2, 2));
    CHECK_FALSE(other.ok); CHECK(other.error == "weapon_smite: self only"); CHECK(other.events.empty());
    CHECK(find(s, 1)->spell_slots == slots);
    auto extra = step(s, cast(1, kSmiteExtra, 1, 1));
    CHECK_FALSE(extra.ok); CHECK(extra.error == "weapon_smite: primary effect only");
    REQUIRE(step(s, cast(1, kSmite, 1, 3)).ok);   // one slot above 2nd: 1d1 + 1d1
    CHECK(effect_of(s, kSmite)->damage.count == 2);
    (void)step(s, ActionRequest{1, ActionKind::EndTurn});
    bool ended = false;
    for (uint64_t seed = 1; seed <= 40 && !ended; ++seed) {
        auto t = start(nullptr, seed);
        flat_weapon(t);
        REQUIRE(step(t, cast(1, kSmitePlain, 1, 2)).ok);
        (void)step(t, ActionRequest{1, ActionKind::EndTurn});
        end_turns_until(t, 1);
        auto r = step(t, ActionRequest{1, ActionKind::Attack, 2});
        REQUIRE(r.ok);
        if (!hit_by(r.events, 1)) { CHECK(has_smite_state(t)); continue; }
        ended = true;
        CHECK_FALSE(has_smite_state(t));
        CHECK(find(t, 1)->concentration_spell == 0);
    }
    CHECK(ended);
}

TEST_CASE("7.2zh heat metal: a failed save drops a held metal object and not worn armor") {
    auto w = make_world();
    auto s = start(w, 3);
    const auto slots = find(s, 1)->spell_slots;
    auto none = step(s, heat(kHeatFail, 2, kItemSword));
    CHECK_FALSE(none.ok); CHECK(none.error == "heat_metal: equipment required"); CHECK(none.events.empty());
    CHECK(find(s, 1)->spell_slots == slots);

    w->views[2] = LoadoutView{};
    w->views[2].main = HeldItem{10, kItemTorch, 1, 0};
    auto torch = step(s, heat(kHeatFail, 2, 10));
    CHECK_FALSE(torch.ok); CHECK(torch.error == "heat_metal: no metal object");
    w->views[2].main = HeldItem{12, kItemArrows, 1, 0};
    CHECK(step(s, heat(kHeatFail, 2, 12)).error == "heat_metal: no metal object");
    w->views[2].main = HeldItem{13, kItemFocus, 1, 0};
    CHECK(step(s, heat(kHeatFail, 2, 13)).error == "heat_metal: no metal object");
    CHECK(step(s, heat(kHeatFail, 2, 0)).error == "heat_metal: no metal object");
    // A hand instance that collides with the armor def id wins, and a non-metal hand does not fall through to the armor.
    w->views[2].main = HeldItem{kItemArmor, kItemTorch, 1, 0}; w->views[2].armor_def = kItemArmor;
    CHECK(step(s, heat(kHeatFail, 2, kItemArmor)).error == "heat_metal: no metal object");
    CHECK(find(s, 1)->spell_slots == slots);

    w->views[2] = LoadoutView{};
    w->views[2].main = HeldItem{10, kItemSword, 1, 0};
    REQUIRE(step(s, heat(kHeatFail, 2, 10)).ok);
    REQUIRE(effect_of(s, kHeatFail));
    CHECK(effect_of(s, kHeatFail)->heated_item == 10); CHECK(effect_of(s, kHeatFail)->heated_def == kItemSword);
    CHECK_FALSE(effect_of(s, kHeatFail)->heated_worn); CHECK_FALSE(effect_of(s, kHeatFail)->while_held);
    CHECK(has_heat_metal_state(s));
    REQUIRE(w->applied.size() == 1);
    CHECK(w->applied[0].equip_op == uint8_t(EquipOp::Drop)); CHECK(w->applied[0].equip_item == 10); CHECK(w->applied[0].actor == 2);
    CHECK(w->views[2].main.instance == 0);

    (void)step(s, ActionRequest{1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    w->applied.clear();
    w->views[2].off = HeldItem{14, kItemShield, 1, 0};
    ActionRequest sus = cast(1, kHeatFail, 2, 2); sus.sustain = true;
    REQUIRE(step(s, sus).ok);   // the effect is still there; the sword is no longer in contact, so no drop and no disadvantage
    CHECK_FALSE(effect_of(s, kHeatFail)->while_held);
    CHECK(effect_of(s, kHeatFail)->heated_item == 10);
    CHECK(w->applied.empty());
    (void)step(s, ActionRequest{1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    w->applied.clear();
    REQUIRE(step(s, heat(kHeatFail, 2, 14)).ok);   // a new cast replaces the concentration and heats the shield
    CHECK(effect_of(s, kHeatFail)->heated_def == kItemShield); CHECK_FALSE(effect_of(s, kHeatFail)->while_held);
    CHECK(w->applied.size() == 1); CHECK(w->applied[0].equip_op == uint8_t(EquipOp::Drop));
}

TEST_CASE("7.2zh heat metal: worn armor cannot drop; success does neither; disadvantage until the caster's next turn") {
    auto w = make_world();
    w->views[2].armor_def = kItemArmor;
    auto s = start(w, 5);
    REQUIRE(step(s, heat(kHeatPass, 2, kItemArmor)).ok);
    CHECK(effect_of(s, kHeatPass)->heated_worn); CHECK(effect_of(s, kHeatPass)->heated_item == kItemArmor);
    CHECK_FALSE(effect_of(s, kHeatPass)->while_held);
    CHECK(w->applied.empty()); CHECK(w->views[2].armor_def == kItemArmor);

    (void)step(s, ActionRequest{1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    ActionRequest passed = cast(1, kHeatPass, 2); passed.sustain = true;
    REQUIRE(step(s, passed).ok);   // success again: the armor stays, and there is still no disadvantage
    CHECK_FALSE(effect_of(s, kHeatPass)->while_held); CHECK(w->applied.empty());
    w->drop_removes = false;
    w->views[2] = LoadoutView{}; w->views[2].main = HeldItem{10, kItemClub, 1, 0};
    REQUIRE(step(s, heat(kHeatFail, 2, 10)).ok);   // ends the passing spell; the hook leaves the club in hand
    CHECK(effect_of(s, kHeatFail)->heated_def == kItemClub); CHECK(effect_of(s, kHeatFail)->while_held);
    CHECK(w->applied.size() == 1); CHECK(w->views[2].main.instance == 10);

    (void)step(s, ActionRequest{1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    w->views[2] = LoadoutView{}; w->views[2].armor_def = kItemArmor; w->applied.clear();
    REQUIRE(step(s, heat(kHeatFail, 2, kItemArmor, 3)).ok);   // upcast: +1d8, and worn armor stays in contact
    CHECK(effect_of(s, kHeatFail)->damage.count == 3); CHECK(effect_of(s, kHeatFail)->while_held);
    CHECK(effect_of(s, kHeatFail)->heated_worn); CHECK(w->applied.empty());
    CHECK_FALSE(step(s, [&] { ActionRequest r = cast(1, kHeatFail, 2); r.sustain = true; return r; }()).ok);
    const uint64_t stamped = heat_metal_digest(s);
    end_turns_until(s, 2);
    CHECK(effect_of(s, kHeatFail)->while_held);   // still on, until the caster's next turn starts
    end_turns_until(s, 1);
    CHECK_FALSE(effect_of(s, kHeatFail)->while_held);
    CHECK(heat_metal_digest(s) != stamped);
    ActionRequest again = cast(1, kHeatFail, 2); again.sustain = true;
    REQUIRE(step(s, again).ok);   // the same object, a new save, and the bonus action
    CHECK(effect_of(s, kHeatFail)->while_held); CHECK_FALSE(s.budget.bonus_action); CHECK(s.budget.action);

    int differ_checks = 0, differ_attacks = 0;
    for (uint64_t seed = 1; seed <= 24; ++seed) {
        auto base = start(w, seed);
        w->views[2].armor_def = kItemArmor; w->views[2].main = {}; w->views[2].off = {};
        REQUIRE(step(base, heat(kHeatFail, 2, kItemArmor)).ok);
        REQUIRE(effect_of(base, kHeatFail)->while_held);
        auto with = base; auto without = base;
        without.effects[0].while_held = false;
        auto ca = encounter_check(with, 2, Skill::Athletics, 15);
        auto cb = encounter_check(without, 2, Skill::Athletics, 15);
        REQUIRE(ca.ok); REQUIRE(cb.ok);
        if (natural_of(ca.events, EventKind::CheckRolled, 2) != natural_of(cb.events, EventKind::CheckRolled, 2)) ++differ_checks;
        auto aw = base; auto bw = base;
        bw.effects[0].while_held = false;
        mut(aw, 2).attacks[0].to_hit = int8_t(30); mut(aw, 2).attacks[0].damage = DiceExpr{1, 8, 0};
        mut(bw, 2).attacks[0] = mut(aw, 2).attacks[0];
        end_turns_until(aw, 2); end_turns_until(bw, 2);
        auto ra = step(aw, ActionRequest{2, ActionKind::Attack, 1});
        auto rb = step(bw, ActionRequest{2, ActionKind::Attack, 1});
        REQUIRE(ra.ok); REQUIRE(rb.ok);
        if (natural_of(ra.events, EventKind::AttackResolved, 2) != natural_of(rb.events, EventKind::AttackResolved, 2) ||
            dealt_to(ra.events, 1) != dealt_to(rb.events, 1)) ++differ_attacks;
    }
    CHECK(differ_checks > 0); CHECK(differ_attacks > 0);
}

TEST_CASE("7.2zh flame blade: a free hand, then the action; no move, no modifier; concentration and a recast") {
    auto w = make_world();
    auto s = start(w, 1);
    auto other = step(s, cast(1, kBlade, 2, 2));
    CHECK_FALSE(other.ok); CHECK(other.error == "summon_weapon: held is self only");
    ActionRequest tiles = cast(1, kBlade, 1, 2); tiles.summon_tiles.push_back({1, 2});
    CHECK(step(s, tiles).error == "summon_weapon: held does not move");
    w->views[1].main = HeldItem{10, kItemSword, 1, 0}; w->views[1].off = HeldItem{14, kItemShield, 1, 0};
    CHECK(step(s, cast(1, kBlade, 1, 2)).error == "summon_weapon: held needs a free hand");
    CHECK(s.summon_weapons.empty());
    w->views[1].off = {};
    const SpellDef* def = s.hooks.spells->find(kBlade);
    REQUIRE(def); CHECK(def->summon_weapon.light_bright_ft == 10); CHECK(def->summon_weapon.light_dim_ft == 10);
    CHECK(def->summon_weapon.held); CHECK(def->summon_weapon.move_ft == 0); CHECK_FALSE(def->add_spellcasting_modifier);
    REQUIRE(step(s, cast(1, kBlade, 1, 2)).ok);
    CHECK_FALSE(s.budget.bonus_action); CHECK(s.budget.action);
    CHECK(find(s, 1)->concentration_spell == kBlade);
    REQUIRE(s.summon_weapons.size() == 1);
    CHECK(s.summon_weapons[0].held); CHECK(s.summon_weapons[0].concentration); CHECK_FALSE(s.summon_weapons[0].has_tile);
    const uint32_t token = s.summon_weapons[0].token_id;
    bool listed = false;
    for (const auto& r : legal_actions(s)) if (r.kind == ActionKind::SummonWeaponAttack && r.spell_id == kBlade) {
        listed = true; CHECK_FALSE(r.as_bonus_action);
    }
    CHECK(listed);
    ActionRequest moved{1, ActionKind::SummonWeaponAttack, 2}; moved.spell_id = kBlade; moved.summon_tiles.push_back({3, 4});
    auto refused = step(s, moved);
    CHECK_FALSE(refused.ok); CHECK(refused.error == "summon_weapon: held does not move"); CHECK(s.budget.action);

    bool struck = false;
    for (uint64_t seed = 1; seed <= 30 && !struck; ++seed) {
        auto t = start(w, seed);
        w->views.erase(1);
        REQUIRE(step(t, cast(1, kBlade, 1, 2)).ok);
        CHECK_FALSE(t.budget.bonus_action); CHECK(t.budget.action);   // the cast took the bonus action
        const int before = mut(t, 2).hp;
        ActionRequest swing{1, ActionKind::SummonWeaponAttack, 2}; swing.spell_id = kBlade;
        auto r = step(t, swing);
        REQUIRE(r.ok);
        CHECK_FALSE(t.budget.action);   // the later attack takes the action, with the bonus already spent
        if (!hit_by(r.events, 1)) continue;
        if (([&] { for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved && e.actor == 1) return (e.c & 2) != 0; return false; })())
            continue;   // a natural 20 would double the 1d1
        struck = true;
        CHECK(before - mut(t, 2).hp == 1);   // 1d1, no spellcasting modifier
        CHECK(dealt_to(r.events, 2) == 1);
    }
    CHECK(struck);

    // Upcast: slot 4, every 2 levels, one extra d1.
    bool up = false;
    for (uint64_t seed = 1; seed <= 30 && !up; ++seed) {
        auto t = start(nullptr, seed);
        mut(t, 1).spell_slots[4] = 1;
        REQUIRE(step(t, cast(1, kBlade, 1, 4)).ok);
        const int before = mut(t, 2).hp;
        auto r = step(t, [&] { ActionRequest a{1, ActionKind::SummonWeaponAttack, 2}; a.spell_id = kBlade; return a; }());
        REQUIRE(r.ok);
        if (!hit_by(r.events, 1)) continue;
        bool crit = false; for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved && e.actor == 1) crit = (e.c & 2) != 0;
        if (crit) continue;
        up = true; CHECK(before - mut(t, 2).hp == 2);
    }
    CHECK(up);

    // A floating weapon's digest ignores held/concentration until some row is held.
    ActiveSummonWeapon floating; floating.token_id = 9; floating.owner = 1; floating.spell_id = 3; floating.slot_level = 2;
    floating.end_round = 4; floating.has_tile = true; floating.tile_x = 1; floating.tile_y = 2;
    s.summon_weapons = {floating};
    const uint64_t plain = summons_digest(s);
    s.summon_weapons[0].concentration = true; CHECK(summons_digest(s) == plain);
    s.summon_weapons[0].held = true; CHECK(summons_digest(s) != plain);
    s.summon_weapons[0].held = false; s.summon_weapons[0].concentration = false; CHECK(summons_digest(s) == plain);

    auto live = start(nullptr, 9);
    REQUIRE(step(live, cast(1, kBlade, 1, 2)).ok);
    const uint32_t first = live.summon_weapons[0].token_id;
    (void)step(live, ActionRequest{1, ActionKind::EndTurn});
    end_turns_until(live, 1);
    auto recast = step(live, cast(1, kBlade, 1, 2));
    REQUIRE(recast.ok);
    CHECK(live.summon_weapons.size() == 1); CHECK(live.summon_weapons[0].token_id != first);
    int ended = 0; for (const auto& e : recast.events) ended += e.kind == EventKind::SummonEnded && e.b == 1 && e.target == first;
    CHECK(ended == 1);
    mut(live, 1).concentration_spell = 0;
    (void)step(live, ActionRequest{1, ActionKind::EndTurn});
    end_turns_until(live, 1);
    auto stuck = step(live, cast(1, kBlade, 1, 2));
    CHECK_FALSE(stuck.ok); CHECK(stuck.error == "summon_weapon: held already active");
    CHECK(live.summon_weapons.size() == 1);

    auto gone = start(nullptr, 4);
    REQUIRE(step(gone, cast(1, kBlade, 1, 2)).ok);
    const uint32_t blade = gone.summon_weapons[0].token_id;
    auto hold = step(gone, cast(1, kHoldPerson, 2, 2));
    REQUIRE(hold.ok);
    CHECK(gone.summon_weapons.empty());
    int by_conc = 0; for (const auto& e : hold.events) by_conc += e.kind == EventKind::SummonEnded && e.b == 1 && e.target == blade;
    CHECK(by_conc == 1);
    (void)token;
}

TEST_CASE("7.2zh flame blade attack does not spend a pending Branding Smite") {
    bool saw = false;
    for (uint64_t seed = 1; seed <= 30 && !saw; ++seed) {
        auto s = start(nullptr, seed);
        REQUIRE(step(s, cast(1, kSmiteRounds, 1, 2)).ok);   // no concentration, so the blade does not end it
        CHECK(find(s, 1)->concentration_spell == 0);
        (void)step(s, ActionRequest{1, ActionKind::EndTurn});
        end_turns_until(s, 1);
        REQUIRE(step(s, cast(1, kBlade, 1, 2)).ok);
        CHECK(effect_of(s, kSmiteRounds)->smite_pending);
        ActionRequest swing{1, ActionKind::SummonWeaponAttack, 2}; swing.spell_id = kBlade;
        auto r = step(s, swing);
        REQUIRE(r.ok);
        if (!hit_by(r.events, 1)) continue;
        saw = true;
        CHECK(effect_of(s, kSmiteRounds)->smite_pending); CHECK(effect_of(s, kSmiteRounds)->target == 1);
    }
    CHECK(saw);
}
