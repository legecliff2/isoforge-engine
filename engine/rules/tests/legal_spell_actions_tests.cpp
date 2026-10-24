// legal_actions spell casts must be requests cast_spell_impl accepts: a self-range modifier (Blur) is offered on the
// caster, slot or innate, and Heat Metal is offered only with an equip_item pick_heated_object accepts.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kBlur = spell_id_of("test.legal.blur");
const uint32_t kRetreat = spell_id_of("test.legal.retreat");
const uint32_t kHeat = spell_id_of("test.legal.heat_metal");
const uint32_t kHands = spell_id_of("test.legal.hands");

enum : uint32_t { kItemSword = 1, kItemTorch = 2, kItemArmor = 5, kItemShield = 6, kItemClub = 7 };

struct World {
    std::map<uint32_t, ItemDef> defs;
    std::map<CombatantId, LoadoutView> views;
};

std::shared_ptr<World> make_world() {
    auto w = std::make_shared<World>();
    auto add = [&](uint32_t id, ItemKind k, ItemMaterial mat = ItemMaterial::Absent) {
        ItemDef d; d.id = id; d.kind = k; d.material = mat; w->defs[id] = d;
    };
    add(kItemSword, ItemKind::Weapon, ItemMaterial::Metal); add(kItemTorch, ItemKind::Light);
    add(kItemArmor, ItemKind::Armor, ItemMaterial::Metal); add(kItemShield, ItemKind::Shield, ItemMaterial::Metal);
    add(kItemClub, ItemKind::Weapon, ItemMaterial::Wood);
    return w;
}

SpellDef self_mod(uint32_t id, const char* content, uint8_t level, CastingTime time, uint8_t components, ModStat stat,
                  ModAppliesTo who) {
    SpellDef d; d.id = id; d.content_id = content; d.level = level; d.range_ft = 0; d.casting_time = time;
    d.components = components; d.concentration = true; d.kind = SpellEffectKind::ApplyModifier;
    d.modifier.id = spell_id_of(std::string(content) + "#modifier"); d.modifier.stat = stat; d.modifier.applies_to = who;
    d.modifier.concentration = true; d.duration = SpellDuration::Minutes; d.duration_amount = 1;
    if (stat == ModStat::AttacksAgainst) d.modifier.mode = RollMode::Disadvantage;
    if (stat == ModStat::BonusDash) { d.modifier.expiry = 1; d.modifier.rounds = 600; d.modifier.concentration = false; }
    return d;
}
SpellDef heat_def() {
    SpellDef d; d.id = kHeat; d.content_id = "test.legal.heat_metal"; d.level = 2; d.range_ft = 60;
    d.components = kComponentV; d.concentration = true; d.kind = SpellEffectKind::SustainedDamage; d.held_object = true;
    d.dice = DiceExpr{2, 8, 0}; d.damage_type = DamageType::Fire; d.fixed_dc = 10;
    d.duration = SpellDuration::Minutes; d.duration_amount = 1;
    return d;
}

EncounterHooks hooks_for(std::shared_ptr<World> w, std::initializer_list<SpellDef> extra, bool block_components = false) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    for (const auto& d : extra) book->add(d);
    h.spells = book;
    h.spell_target_ok = [](const EncounterState&, const Combatant& caster, const SpellDef& d, const Combatant& t) {
        if (d.range_ft == 0) return caster.id == t.id;   // same rule as Combat::spell_in_reach
        return true;
    };
    if (block_components)
        h.can_provide_components = [](const EncounterState&, const Combatant&, const SpellDef&) { return false; };
    if (w) {
        h.item_def = [w](uint32_t id) -> const ItemDef* {
            auto it = w->defs.find(id); return it == w->defs.end() ? nullptr : &it->second;
        };
        h.loadout_of = [w](const EncounterState&, CombatantId id) -> const LoadoutView* {
            auto it = w->views.find(id); return it == w->views.end() ? nullptr : &it->second;
        };
        h.apply_equip = [](EncounterState&, const ActionRequest&, uint8_t, std::vector<Event>&) {};
    }
    return h;
}

SpellDef blur_def() {
    return self_mod(kBlur, "test.legal.blur", 2, CastingTime::Action, kComponentV, ModStat::AttacksAgainst, ModAppliesTo::Target);
}
SpellDef retreat_def() {
    return self_mod(kRetreat, "test.legal.retreat", 1, CastingTime::BonusAction, kComponentV, ModStat::BonusDash, ModAppliesTo::Self);
}
SpellDef hands_def() {
    return self_mod(kHands, "test.legal.hands", 1, CastingTime::Action, kComponentV | kComponentS, ModStat::Ac, ModAppliesTo::Self);
}

InnateSpell at_will(uint32_t id, bool listed) { InnateSpell i; i.spell_id = id; i.as_listed = listed; return i; }
InnateSpell per_day(uint32_t id, uint8_t n, bool listed) {
    InnateSpell i; i.spell_id = id; i.uses_max = n; i.uses_left = n; i.as_listed = listed; return i;
}

std::vector<ActionRequest> casts_of(const std::vector<ActionRequest>& la, uint32_t spell) {
    std::vector<ActionRequest> o;
    for (const auto& r : la)
        if (r.kind == ActionKind::CastSpell && r.spell_id == spell && !r.move_mark && r.zone_move == 0 && !r.sustain)
            o.push_back(r);
    return o;
}
void submit_all(const EncounterState& s) {
    const auto la = legal_actions(s);
    REQUIRE_FALSE(la.empty());
    int spells = 0;
    for (const auto& r : la) {
        auto c = s;
        auto res = step(c, r);
        INFO("kind " << int(r.kind) << " spell " << r.spell_id << " target " << r.target << " level " << int(r.spell_level)
             << " item " << r.equip_item << " innate " << int(r.innate) << " " << std::string(res.error));
        CHECK(res.ok);
        spells += r.kind == ActionKind::CastSpell;
    }
    CHECK(spells > 0);
}

}  // namespace

TEST_CASE("legal_actions spell specimens submit: self-range modifiers and heat metal items") {
    auto w = make_world();
    // 2: sword, shield, and worn armor (three eligible items). 3: a torch. 4: no loadout. 5: an ally in mail.
    // 6: a non-metal hand whose instance equals the armor def (the hand wins; the armor is not eligible).
    w->views[2].main = HeldItem{10, kItemSword, 1, 0};
    w->views[2].off = HeldItem{14, kItemShield, 1, 0};
    w->views[2].armor_def = kItemArmor;
    w->views[3].main = HeldItem{20, kItemTorch, 1, 0};
    w->views[5].main = HeldItem{30, kItemSword, 1, 0};
    w->views[6].main = HeldItem{kItemArmor, kItemTorch, 1, 0};
    w->views[6].armor_def = kItemArmor;
    w->views[7].main = HeldItem{40, kItemClub, 1, 0};   // wood: not a manufactured metal object

    auto wiz = wizard(1);
    wiz.spells_known = {kBlur, kRetreat, kHeat, kFireBolt};
    wiz.hp = wiz.hp_max = 40;
    never_react(wiz);
    auto foe = [](CombatantId id) { auto m = monster(id); m.hp = m.hp_max = 80; m.ac = 10; never_react(m); return m; };
    auto ally = foe(5); ally.team = 0;
    auto s = begin_encounter({wiz, foe(2), foe(3), foe(4), ally, foe(6), foe(7)}, 1, nullptr,
                             hooks_for(w, {blur_def(), retreat_def(), heat_def()}));
    end_turns_until(s, 1);
    mut(s, 1).concentration_spell = kFireBolt;   // already concentrating: still legal, the cast replaces it

    const auto la = legal_actions(s);
    const auto blur = casts_of(la, kBlur);
    REQUIRE(blur.size() == 2);   // 2nd and 3rd (the slots the wizard has), never an enemy
    for (const auto& r : blur) {
        CHECK(r.target == 1); CHECK_FALSE(r.innate); CHECK(r.equip_item == 0);
        CHECK((r.spell_level == 2 || r.spell_level == 3));
    }
    const auto retreat = casts_of(la, kRetreat);
    REQUIRE(retreat.size() == 3);   // bonus action, levels 1..3, self
    for (const auto& r : retreat) { CHECK(r.target == 1); CHECK_FALSE(r.as_bonus_action); CHECK((r.spell_level >= 1 && r.spell_level <= 3)); }
    std::vector<uint32_t> heat2, heat3;
    for (const auto& r : casts_of(la, kHeat)) {
        CHECK(r.target == 2);
        (r.spell_level == 2 ? heat2 : heat3).push_back(r.equip_item);
        CHECK((r.spell_level == 2 || r.spell_level == 3));
    }
    CHECK(heat2 == std::vector<uint32_t>{10, 14, kItemArmor});   // 10 is the longsword
    CHECK(heat3 == std::vector<uint32_t>{10, 14, kItemArmor});
    for (CombatantId id : {CombatantId{3}, CombatantId{4}, CombatantId{5}, CombatantId{6}, CombatantId{7}})
        CHECK(std::none_of(la.begin(), la.end(), [&](const ActionRequest& r) { return r.spell_id == kHeat && r.target == id; }));
    auto club = cast(1, kHeat, 7, 2); club.equip_item = 40;
    CHECK(std::string(step(s, club).error) == "heat_metal: no metal object");

    auto bare = cast(1, kHeat, 4, 2); bare.equip_item = 10;
    auto no_kit = step(s, bare);
    CHECK_FALSE(no_kit.ok); CHECK(std::string(no_kit.error) == "heat_metal: equipment required");
    auto torch = cast(1, kHeat, 3, 2); torch.equip_item = 20;
    CHECK(std::string(step(s, torch).error) == "heat_metal: no metal object");
    auto collide = cast(1, kHeat, 6, 2); collide.equip_item = kItemArmor;
    CHECK(std::string(step(s, collide).error) == "heat_metal: no metal object");

    submit_all(s);
    {
        auto c = s;
        auto cast_blur = step(c, cast(1, kBlur, 1, 2));
        REQUIRE(cast_blur.ok);
        CHECK(find(c, 1)->concentration_spell == kBlur);
    }

    auto broke = s;
    mut(broke, 1).spell_slots = {}; mut(broke, 1).spell_slots_max = {};
    const auto left = legal_actions(broke);
    for (const auto& r : left) {
        CHECK(r.spell_id != kBlur);
        CHECK(r.spell_id != kRetreat);
        CHECK(r.spell_id != kHeat);
    }
    CHECK(std::any_of(left.begin(), left.end(), [](const ActionRequest& r) { return r.spell_id == kFireBolt; }));
    submit_all(broke);

    // Innate: the same spells, no slots. A spent per-day copy is absent. as_listed still pays the component gates.
    auto innate = monster(1);
    innate.spells_known.clear(); innate.spell_slots = {}; innate.spell_slots_max = {};
    innate.spellcasting_ability = Ability::Cha; innate.hp = innate.hp_max = 80;
    InnateSpell spent = per_day(kRetreat, 1, true); spent.uses_left = 0;
    innate.innate = {at_will(kBlur, true), per_day(kHeat, 1, true), spent};
    never_react(innate);
    auto wearer = wizard(2); wearer.spells_known.clear(); wearer.team = 0; never_react(wearer);
    auto iw = make_world();
    iw->views[2].armor_def = kItemArmor;
    auto inn = begin_encounter({innate, wearer}, 1, nullptr, hooks_for(iw, {blur_def(), retreat_def(), heat_def()}));
    end_turns_until(inn, 1);
    const auto ila = legal_actions(inn);
    const auto ib = casts_of(ila, kBlur);
    REQUIRE(ib.size() == 1);
    CHECK(ib[0].target == 1); CHECK(ib[0].innate); CHECK(ib[0].spell_level == 2);
    const auto ih = casts_of(ila, kHeat);
    REQUIRE(ih.size() == 1);
    CHECK(ih[0].target == 2); CHECK(ih[0].innate); CHECK(ih[0].spell_level == 2); CHECK(ih[0].equip_item == kItemArmor);
    CHECK(casts_of(ila, kRetreat).empty());
    submit_all(inn);

    // Somatic self-range modifier: both hands full, so the shared component refusal keeps it off the list.
    auto hw = make_world();
    hw->views[1].main = HeldItem{10, kItemSword, 1, 0};
    hw->views[1].off = HeldItem{14, kItemShield, 1, 0};
    auto hands = wizard(1);
    hands.spells_known = {kBlur, kHands};
    never_react(hands);
    auto hs = begin_encounter({hands, foe(2)}, 1, nullptr, hooks_for(hw, {blur_def(), hands_def()}));
    end_turns_until(hs, 1);
    const auto hla = legal_actions(hs);
    CHECK(casts_of(hla, kHands).empty());
    CHECK(casts_of(hla, kBlur).size() == 2);
    auto denied = step(hs, cast(1, kHands, 1, 1));
    CHECK_FALSE(denied.ok); CHECK(std::string(denied.error) == "cast: somatic needs a free hand");
    submit_all(hs);

    // Innate components are skipped unless as_listed. A hook that refuses every component still leaves the quiet cast.
    auto quiet = monster(1);
    quiet.spells_known.clear(); quiet.spell_slots = {}; quiet.spell_slots_max = {};
    quiet.innate = {at_will(kBlur, false), at_will(kHands, true)};
    never_react(quiet);
    auto other = foe(2); other.team = 0;
    auto qs = begin_encounter({quiet, other}, 1, nullptr, hooks_for(nullptr, {blur_def(), hands_def()}, true));
    end_turns_until(qs, 1);
    const auto qla = legal_actions(qs);
    REQUIRE(casts_of(qla, kBlur).size() == 1);
    CHECK(casts_of(qla, kBlur)[0].innate);
    CHECK(casts_of(qla, kHands).empty());
    auto blocked = cast(1, kHands, 1, 1); blocked.innate = true;
    CHECK(std::string(step(qs, blocked).error) == "components unavailable");
    submit_all(qs);
}
