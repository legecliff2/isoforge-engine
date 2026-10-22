// docs/proposals/EQUIPMENT.md (Director-approved): compute_ac's five steps, derive_weapon_attack, hands / components, equip_legal,
// the legal_actions entries, step()'s gated checks and the monster derivation with its exact strings.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {

AbilityScores scores(int s, int d, int c = 10, int i = 10, int w = 10, int ch = 10) {
    return AbilityScores{{int8_t(s), int8_t(d), int8_t(c), int8_t(i), int8_t(w), int8_t(ch)}};
}
ArmorPiece armor(ArmorCategory c, int base, bool dex, int cap = 0) {
    ArmorPiece a; a.category = c; a.base_ac = int16_t(base); a.dex_bonus = dex; a.has_dex_cap = cap > 0; a.dex_cap = int8_t(cap);
    return a;
}
WeaponDef weapon(const char* id, DiceExpr d, uint16_t props, uint8_t reach = 5, uint16_t range = 0, uint16_t lr = 0) {
    WeaponDef w; w.id = id; w.dice = d; w.properties = props; w.reach_ft = reach; w.range_ft = range; w.long_range_ft = lr;
    return w;
}
WielderView wielder(AbilityScores a, int pb = 2) { WielderView v; v.abilities = a; v.pb = int8_t(pb); return v; }

// ---- item table + loadout views for the predicates (Engine Core's state, faked here) ----
enum : uint32_t { dLongsword = 1, dShieldDef, dHolySymbol, dArcaneFocus, dHandCrossbow, dLongbow, dJavelin, dArrows, dTorch, dGreatsword };
struct World {
    std::map<uint32_t, ItemDef> defs;
    std::map<CombatantId, LoadoutView> views;
    std::vector<ActionRequest> applied;
};
std::shared_ptr<World> make_world() {
    auto w = std::make_shared<World>();
    auto add = [&](uint32_t id, ItemKind k) -> ItemDef& { ItemDef d; d.id = id; d.kind = k; return w->defs[id] = d; };
    add(dLongsword, ItemKind::Weapon).weapon = weapon("longsword", {1, 8, 0}, kWeaponVersatile);
    w->defs[dLongsword].weapon.versatile_dice = {1, 10, 0};
    add(dShieldDef, ItemKind::Shield);
    add(dHolySymbol, ItemKind::Focus).holy_symbol = true;
    add(dArcaneFocus, ItemKind::Focus);
    auto& hc = add(dHandCrossbow, ItemKind::Weapon);
    hc.weapon = weapon("hand_crossbow", {1, 6, 0}, kWeaponAmmunition | kWeaponLight | kWeaponLoading, 0, 30, 120);
    hc.weapon.ammo_kind = kAmmoBolt;
    auto& lb = add(dLongbow, ItemKind::Weapon);
    lb.weapon = weapon("longbow", {1, 8, 0}, kWeaponAmmunition | kWeaponHeavy | kWeaponTwoHanded, 0, 150, 600);
    lb.weapon.ammo_kind = kAmmoArrow;
    add(dJavelin, ItemKind::Weapon).weapon = weapon("javelin", {1, 6, 0}, kWeaponThrown, 5, 30, 120);
    add(dArrows, ItemKind::Ammunition).ammo_kind = kAmmoArrow;
    auto& t = add(dTorch, ItemKind::Light);
    t.has_weapon = true; t.weapon_requires_lit = true; t.weapon = weapon("torch", {0, 0, 1}, 0); t.weapon.improvised = true;
    t.weapon.type = DamageType::Fire; t.light = LightDef{20, 20, LightShape::Radius, 600, false, 0};
    add(dGreatsword, ItemKind::Weapon).weapon = weapon("greatsword", {2, 6, 0}, kWeaponHeavy | kWeaponTwoHanded);
    return w;
}
EncounterHooks equip_hooks(std::shared_ptr<World> w) {
    auto h = spell_hooks();
    h.item_def = [w](uint32_t id) -> const ItemDef* { auto it = w->defs.find(id); return it == w->defs.end() ? nullptr : &it->second; };
    h.loadout_of = [w](const EncounterState&, CombatantId id) -> const LoadoutView* {
        auto it = w->views.find(id); return it == w->views.end() ? nullptr : &it->second;
    };
    h.apply_equip = [w](EncounterState&, const ActionRequest& r, uint8_t, std::vector<Event>&) { w->applied.push_back(r); };
    return h;
}
HeldItem item(uint32_t inst, uint32_t def, uint16_t count = 1, uint8_t flags = 0) { return HeldItem{inst, def, count, flags}; }
ActionRequest equip(CombatantId a, EquipOp op, uint32_t inst, uint8_t hand = 0) {
    ActionRequest r{a, ActionKind::UseObject, 0}; r.equip_op = uint8_t(op); r.equip_item = inst; r.equip_hand = hand;
    return r;
}

} // namespace

TEST_CASE("compute_ac: new fields at their defaults reproduce the legacy result") {
    for (int dex = 6; dex <= 20; dex += 2) {
        AcInput in; in.abilities = scores(14, dex, 16);
        const int plain = compute_ac(in).ac;
        CHECK(plain == 10 + modifier(dex));
        in.has_alt_ability = true; in.alt_ability = Ability::Con;
        CHECK(compute_ac(in).ac == 10 + modifier(dex) + 3);
        CHECK(compute_ac(in).base == AcBaseKind::UnarmoredDefense);
        in.has_armor = true; in.armor = armor(ArmorCategory::Medium, 14, true, 2); in.has_shield = true; in.armored_bonus = 1; in.flat_bonus = 1;
        CHECK(compute_ac(in).ac == 14 + std::min(2, modifier(dex)) + 2 + 1 + 1);
        CHECK(compute_ac(in).base == AcBaseKind::Armor);
    }
}

TEST_CASE("compute_ac: base pick-one-highest, shield, rows by source, adjust, floor") {
    AcInput in; in.abilities = scores(10, 14, 10, 10, 16);   // Dex +2, Wis +3
    AcBaseCandidate monk{AcBaseKind::UnarmoredDefense, 10, Ability::Wis, true, true, 1};
    AcBaseCandidate mage{AcBaseKind::FixedPlusDex, 13, Ability::Con, true, false, 2};
    in.bases = {monk, mage};
    CHECK(compute_ac(in).ac == 15);   // monk 10 + 2 + 3 beats Mage Armor 13 + 2: they do not stack
    CHECK(compute_ac(in).base == AcBaseKind::UnarmoredDefense);
    in.has_shield = true;             // a shield makes the Monk formula ineligible; Mage Armor + shield
    CHECK(compute_ac(in).ac == 13 + 2 + 2);
    CHECK(compute_ac(in).base == AcBaseKind::FixedPlusDex);
    CHECK(compute_ac(in).shield_counted);
    in.has_shield = false;
    in.has_armor = true; in.armor = armor(ArmorCategory::Heavy, 18, false);   // armor: no other base is eligible
    in.armor.str_min = 15; in.base_speed_ft = 30; in.armor_proficient = false;
    auto r = compute_ac(in);
    CHECK(r.ac == 18); CHECK(r.str_shortfall); CHECK(r.speed_ft == 20); CHECK(r.nonproficient_armor);
    in.bonuses = {{7, 1, true}, {9, 2, false}, {9, 5, false}, {11, 1, false}};   // Defense (armor), Shield of Faith 2 vs Shield 5 same source?
    CHECK(compute_ac(in).ac == 18 + 1 + 5 + 1);   // source 9: only the highest row applies
    in.has_armor = false; in.bases.clear(); in.bonuses = {{7, 1, true}};
    CHECK(compute_ac(in).ac == 12);   // Defense needs armor
    in.ac_adjust = 1; CHECK(compute_ac(in).ac == 13);
    in.floor = 16; CHECK(compute_ac(in).ac == 16);   // Barkskin
    AcInput nat; nat.abilities = scores(10, 14); nat.bases = {AcBaseCandidate{AcBaseKind::Natural, 11, Ability::Con, true, false, 0}};
    CHECK(compute_ac(nat).ac == 13);
    nat.bases[0].natural_dex = false; CHECK(compute_ac(nat).ac == 12);   // 10 + Dex beats 11 flat
}

TEST_CASE("derive_weapon_attack: abilities, versatile, thrown, ranged, magic") {
    const auto ab = scores(16, 14);   // Str +3, Dex +2
    auto ls = weapon("longsword", {1, 8, 0}, kWeaponVersatile); ls.versatile_dice = {1, 10, 0};
    auto a = derive_weapon_attack(ls, wielder(ab), WeaponUse::OneHand);
    CHECK(a.error.empty()); CHECK(a.attack.to_hit == 5); CHECK(a.attack.damage == DiceExpr{1, 8, 3}); CHECK_FALSE(a.attack.ranged);
    CHECK(derive_weapon_attack(ls, wielder(ab), WeaponUse::TwoHands).attack.damage == DiceExpr{1, 10, 3});
    CHECK(derive_weapon_attack(ls, wielder(ab), WeaponUse::Thrown).error == "weapon: not a thrown weapon");
    auto rapier = weapon("rapier", {1, 8, 0}, kWeaponFinesse);
    CHECK(derive_weapon_attack(rapier, wielder(scores(8, 16)), WeaponUse::OneHand).attack.to_hit == 5);   // finesse: Dex
    auto bow = weapon("longbow", {1, 8, 0}, kWeaponAmmunition | kWeaponHeavy | kWeaponTwoHanded, 0, 150, 600);
    auto b = derive_weapon_attack(bow, wielder(ab), WeaponUse::TwoHands).attack;
    CHECK(b.ranged); CHECK(b.to_hit == 4); CHECK(b.range_ft == 150); CHECK(b.long_range_ft == 600);
    auto jav = weapon("javelin", {1, 6, 0}, kWeaponThrown, 5, 30, 120);
    auto t = derive_weapon_attack(jav, wielder(ab), WeaponUse::Thrown).attack;
    CHECK(t.ranged); CHECK(t.to_hit == 5); CHECK(t.damage == DiceExpr{1, 6, 3});   // thrown melee weapon: Str
    auto gs = weapon("greatsword", {2, 6, 0}, kWeaponTwoHanded | kWeaponHeavy);
    CHECK(derive_weapon_attack(gs, wielder(ab), WeaponUse::OneHand).error == "weapon: a two-handed weapon needs both hands");
    auto plus = ls; plus.magic_bonus = 2;
    auto p = derive_weapon_attack(plus, wielder(ab), WeaponUse::OneHand).attack;
    CHECK(p.to_hit == 7); CHECK(p.damage == DiceExpr{1, 8, 5}); CHECK((p.qualifiers & kAttackMagical) != 0);
    auto split = ls; split.attack_magic = 1; split.damage_magic = 3;
    auto q = derive_weapon_attack(split, wielder(ab), WeaponUse::OneHand).attack;
    CHECK(q.to_hit == 6); CHECK(q.damage == DiceExpr{1, 8, 6});
    auto np = wielder(ab); np.proficient = false;
    CHECK(derive_weapon_attack(ls, np, WeaponUse::OneHand).attack.to_hit == 3);
    auto u = unarmed_strike(ab, 2); CHECK(u.to_hit == 5); CHECK(u.damage == DiceExpr{0, 0, 4});
}

TEST_CASE("derive_weapon_attack: off-hand modifier only when negative, TWF style, Brute, multiplier, override") {
    auto ss = weapon("shortsword", {1, 6, 0}, kWeaponFinesse | kWeaponLight);
    CHECK(derive_weapon_attack(ss, wielder(scores(10, 16)), WeaponUse::OffHand).attack.damage == DiceExpr{1, 6, 0});
    CHECK(derive_weapon_attack(ss, wielder(scores(6, 8)), WeaponUse::OffHand).attack.damage == DiceExpr{1, 6, -1});   // negative applies
    auto twf = wielder(scores(10, 16)); twf.twf_style = true;
    CHECK(derive_weapon_attack(ss, twf, WeaponUse::OffHand).attack.damage == DiceExpr{1, 6, 3});
    auto ls = weapon("longsword", {1, 8, 0}, kWeaponVersatile); ls.versatile_dice = {1, 10, 0};
    CHECK(derive_weapon_attack(ls, wielder(scores(16, 10)), WeaponUse::OffHand).error == "weapon: the off-hand weapon must be a light melee weapon");
    auto brute = wielder(scores(15, 14)); brute.extra_melee_dice = 1;   // Bugbear
    auto jav = weapon("javelin", {1, 6, 0}, kWeaponThrown, 5, 30, 120);
    CHECK(derive_weapon_attack(jav, brute, WeaponUse::OneHand).attack.damage == DiceExpr{2, 6, 2});
    CHECK(derive_weapon_attack(jav, brute, WeaponUse::Thrown).attack.damage == DiceExpr{1, 6, 2});   // thrown: no Brute die
    auto ogre = wielder(scores(19, 8)); ogre.dice_multiplier = 2;
    CHECK(derive_weapon_attack(jav, ogre, WeaponUse::Thrown).attack.damage == DiceExpr{2, 6, 4});
    auto bite = weapon("bite", {2, 4, 0}, 0); bite.natural = true;
    auto nat = ogre; nat.ability = Ability::Dex;
    CHECK(derive_weapon_attack(bite, nat, WeaponUse::OneHand).attack.damage == DiceExpr{2, 4, -1});   // natural: no multiplier
    auto none = wielder(scores(14, 3)); none.damage_ability_none = true;
    auto pseudo = weapon("pseudopod", {3, 6, 0}, 0); pseudo.natural = true;
    CHECK(derive_weapon_attack(pseudo, none, WeaponUse::OneHand).attack.damage == DiceExpr{3, 6, 0});
    auto ov = wielder(scores(15, 10)); ov.to_hit_override = int8_t{3};
    CHECK(derive_weapon_attack(bite, ov, WeaponUse::OneHand).attack.to_hit == 3);
}

TEST_CASE("hands and components: free hand, focus, holy symbol on a shield, priced materials") {
    auto w = make_world();
    auto s = begin_encounter({wizard(1), monster(2)}, 7, nullptr, equip_hooks(w));
    end_turns_until(s, 1);
    SpellDef sm; sm.components = kComponentV | kComponentS | kComponentM;
    SpellDef so; so.components = kComponentV | kComponentS;
    SpellDef mo; mo.components = kComponentV | kComponentM;
    SpellDef priced = sm; priced.material_cost_cp = 30000;
    CHECK(components_ok(s, 1, so));   // no view: unconstrained
    CHECK(free_hands(s, 1) == 2);
    LoadoutView v; v.main = item(10, dLongsword); v.off = item(11, dShieldDef);
    w->views[1] = v;
    CHECK(free_hands(s, 1) == 0);
    CHECK(components_refusal(s, 1, so) == "cast: somatic needs a free hand");
    CHECK(components_refusal(s, 1, sm) == "cast: somatic needs a free hand");
    CHECK(components_refusal(s, 1, mo) == "cast: material needs a free hand or a focus");
    w->views[1].off.flags = kHeldEmblem;   // holy symbol borne on the shield (Q4)
    CHECK(components_ok(s, 1, sm));
    CHECK(components_ok(s, 1, mo));
    CHECK(components_refusal(s, 1, so) == "cast: somatic needs a free hand");
    CHECK(components_refusal(s, 1, priced) == "cast: somatic needs a free hand");
    w->views[1].off = item(12, dArcaneFocus);   // a held focus performs S for S+M
    CHECK(components_ok(s, 1, sm));
    w->views[1].off = {};
    CHECK(components_ok(s, 1, so)); CHECK(components_ok(s, 1, priced));
    w->views[1].off = item(11, dShieldDef);
    w->views[1].carried = {item(13, dHolySymbol, 1, kHeldWorn)};   // worn symbol: M only
    CHECK(components_ok(s, 1, mo));
    CHECK(components_refusal(s, 1, sm) == "cast: somatic needs a free hand");
    // step() refuses the cast with the same string
    auto r = step(s, cast(1, kFireBolt, 2));
    CHECK_FALSE(r.ok); CHECK(r.error == "cast: somatic needs a free hand");
}

TEST_CASE("attack-time checks: in hand, both hands, loading, ammunition, thrown draw per attack, torch") {
    auto w = make_world();
    auto a = pc(1);
    a.attacks = {enc::kLongsword, enc::kLongsword, enc::kLongsword, enc::kLongsword, enc::kLongsword, enc::kLongsword};
    auto s = begin_encounter({a, monster(2)}, 7, nullptr, equip_hooks(w));
    end_turns_until(s, 1);
    LoadoutView v;
    v.attacks = {{dLongsword, WeaponUse::OneHand}, {dLongsword, WeaponUse::TwoHands}, {dHandCrossbow, WeaponUse::OneHand},
                 {dLongbow, WeaponUse::TwoHands}, {dJavelin, WeaponUse::Thrown}, {dTorch, WeaponUse::OneHand}};
    v.carried = {item(20, dLongsword)};
    w->views[1] = v;
    CHECK(attack_refusal(s, 1, 0) == "attack: the weapon is not in hand");   // stowed weapon: listed, refused at attack time
    auto r = step(s, ActionRequest{1, ActionKind::Attack, 2});
    CHECK_FALSE(r.ok); CHECK(r.error == "attack: the weapon is not in hand");
    w->views[1].main = item(20, dLongsword); w->views[1].carried.clear();
    CHECK(attack_refusal(s, 1, 0).empty());
    CHECK(attack_refusal(s, 1, 1).empty());
    w->views[1].off = item(21, dShieldDef);
    CHECK(attack_refusal(s, 1, 1) == "attack: needs both hands");
    w->views[1].main = item(22, dHandCrossbow);
    CHECK(attack_refusal(s, 1, 2) == "attack: loading needs a free hand");
    w->views[1].off = {};
    CHECK(attack_refusal(s, 1, 2).empty());   // no bolt stack: unlimited
    w->views[1].main = item(23, dLongbow);
    CHECK(attack_refusal(s, 1, 3).empty());
    w->views[1].carried = {item(24, dArrows, 0)};
    CHECK(attack_refusal(s, 1, 3) == "attack: no ammunition");
    w->views[1].carried = {item(24, dArrows, 20)};
    CHECK(attack_refusal(s, 1, 3).empty());
    w->views[1].off = item(21, dShieldDef);
    CHECK(attack_refusal(s, 1, 3) == "attack: needs both hands");
    // thrown: drawn as part of each attack (Q2) while a javelin is carried and a hand is free
    w->views[1].main = {}; w->views[1].carried = {item(25, dJavelin, 2)};
    CHECK(attack_refusal(s, 1, 4).empty());
    CHECK(can_attack_with(s, 1, 25, WeaponUse::Thrown));
    w->views[1].carried = {};
    CHECK(attack_refusal(s, 1, 4) == "attack: nothing to throw");
    w->views[1].main = item(26, dTorch);
    CHECK(attack_refusal(s, 1, 5) == "attack: the torch is not lit");
    w->views[1].main.flags = kHeldLit;
    CHECK(attack_refusal(s, 1, 5).empty());
}

TEST_CASE("equip_legal: costs, refusals, armor rounds, the step path and legal_actions") {
    auto w = make_world();
    auto s = begin_encounter({pc(1), monster(2)}, 7, nullptr, equip_hooks(w));
    end_turns_until(s, 1);
    CHECK(equip_legal(s, equip(1, EquipOp::Draw, 30, 1)).refusal == "equip: equipment play is off");
    LoadoutView v; v.carried = {item(30, dLongsword), item(31, dShieldDef), item(32, dGreatsword)};
    w->views[1] = v;
    w->views[2] = LoadoutView{};
    CHECK(equip_legal(s, equip(2, EquipOp::Draw, 30)).refusal == "equip: not your turn");
    CHECK(equip_legal(s, equip(1, EquipOp::Draw, 30, 1)).kind == EquipCost::Interaction);
    CHECK(equip_legal(s, equip(1, EquipOp::Draw, 99, 1)).refusal == "equip: the item is not carried");
    CHECK(equip_legal(s, equip(1, EquipOp::Draw, 31, 2)).refusal == "equip: use don_shield for a shield");
    CHECK(equip_legal(s, equip(1, EquipOp::DonShield, 31, 2)).kind == EquipCost::Action);
    CHECK(equip_legal(s, equip(1, EquipOp::DonArmor, 0)).refusal == "equip: armor cannot be changed in combat");
    CHECK(equip_legal(s, equip(1, EquipOp::Drop, 30)).refusal == "equip: the item is not in hand");
    w->views[1].interaction_used = true;
    CHECK(equip_legal(s, equip(1, EquipOp::Draw, 30, 1)).kind == EquipCost::Action);   // then Use an Object
    w->views[1].main = item(40, dLongsword); w->views[1].off = item(41, dShieldDef);
    CHECK(equip_legal(s, equip(1, EquipOp::Draw, 30)).refusal == "equip: no free hand");
    CHECK(equip_legal(s, equip(1, EquipOp::Drop, 40)).kind == EquipCost::Free);
    CHECK(equip_legal(s, equip(1, EquipOp::Stow, 41)).refusal == "equip: use doff_shield for a shield");
    CHECK(equip_legal(s, equip(1, EquipOp::DoffShield, 41)).kind == EquipCost::Action);
    CHECK(equip_legal(s, equip(1, EquipOp::DonShield, 31)).refusal == "equip: a shield is already wielded");
    auto pick = equip(1, EquipOp::PickUp, 50);
    CHECK(equip_legal(s, pick).refusal == "equip: the item is out of reach");
    pick.equip_in_reach = true;
    CHECK(equip_legal(s, pick).refusal == "equip: unknown ground item");   // no ground_item_def hook
    s.hooks.ground_item_def = [](const EncounterState&, uint32_t inst) -> const ItemDef* {
        static const ItemDef gs = [] { ItemDef d; d.id = dGreatsword; d.kind = ItemKind::Weapon; d.hands = 2; return d; }();
        static const ItemDef ls = [] { ItemDef d; d.id = dLongsword; d.kind = ItemKind::Weapon; d.hands = 1; return d; }();
        return inst == 50 ? &gs : inst == 51 ? &ls : nullptr;
    };
    w->views[1].main = HeldItem{}; w->views[1].off = HeldItem{}; w->views[1].interaction_used = false;
    CHECK(equip_legal(s, pick).kind == EquipCost::Interaction);              // two-handed, both hands free
    w->views[1].main = item(40, dLongsword); w->views[1].off = HeldItem{};
    CHECK(equip_legal(s, pick).refusal == "equip: no free hand");            // two-handed, one free hand
    auto pick1 = equip(1, EquipOp::PickUp, 51); pick1.equip_in_reach = true;
    CHECK(equip_legal(s, pick1).kind == EquipCost::Interaction);             // one-handed, one free hand
    auto pick_ghost = equip(1, EquipOp::PickUp, 52); pick_ghost.equip_in_reach = true;
    CHECK(equip_legal(s, pick_ghost).refusal == "equip: unknown ground item");
    auto pick_def = equip(1, EquipOp::PickUp, dGreatsword); pick_def.equip_in_reach = true;
    CHECK(equip_legal(s, pick_def).refusal == "equip: unknown ground item");  // a def id is not a ground instance
    w->views[1].off = item(41, dShieldDef); w->views[1].interaction_used = true;
    auto hand = equip(1, EquipOp::HandOver, 40); hand.equip_to = 2; hand.equip_in_reach = true;
    CHECK(equip_legal(s, hand).kind == EquipCost::Action);
    w->views[2].main = item(60, dLongsword); w->views[2].off = item(61, dShieldDef);
    CHECK(equip_legal(s, hand).refusal == "equip: the recipient has no free hand");
    w->views[2].off = HeldItem{};
    CHECK(equip_legal(s, hand).kind == EquipCost::Action);                   // longsword, one free hand
    w->views[1].main = item(42, dGreatsword);
    auto hand2 = equip(1, EquipOp::HandOver, 42); hand2.equip_to = 2; hand2.equip_in_reach = true;
    CHECK(equip_legal(s, hand2).refusal == "equip: the recipient has no free hand");   // two-handed by its def, not instance
    w->views[2].main = HeldItem{};
    CHECK(equip_legal(s, hand2).kind == EquipCost::Action);
    w->views[1].main = item(40, dLongsword); w->views[2].main = item(60, dLongsword); w->views[2].off = item(61, dShieldDef);
    CHECK(armor_change_rounds(ArmorCategory::Light, true, false) == 10);
    CHECK(armor_change_rounds(ArmorCategory::Medium, true, false) == 50);
    CHECK(armor_change_rounds(ArmorCategory::Medium, false, false) == 10);
    CHECK(armor_change_rounds(ArmorCategory::Heavy, true, false) == 100);
    CHECK(armor_change_rounds(ArmorCategory::Heavy, false, true) == 25);
    // step: Action cost spends the action, then Engine Core's apply_equip runs
    auto r = step(s, equip(1, EquipOp::DoffShield, 41));
    CHECK(r.ok); CHECK_FALSE(s.budget.action); REQUIRE(w->applied.size() == 1);
    CHECK(w->applied[0].equip_op == uint8_t(EquipOp::DoffShield));
    auto r2 = step(s, equip(1, EquipOp::DonShield, 31, 2));
    CHECK_FALSE(r2.ok);
    // legal_actions: equip entries come after every existing entry, in op / instance / hand order
    w->views[1] = v;
    auto la = legal_actions(s);
    std::size_t first = la.size();
    for (std::size_t i = 0; i < la.size(); ++i) if (la[i].equip_op != 0) { first = i; break; }
    REQUIRE(first < la.size());
    for (std::size_t i = first; i < la.size(); ++i) CHECK(la[i].equip_op != 0);
    CHECK(la[first].equip_op == uint8_t(EquipOp::Draw)); CHECK(la[first].equip_item == 30); CHECK(la[first].equip_hand == 1);
    // no hooks: no equip entries, identical legacy list
    auto legacy = begin_encounter({pc(1), monster(2)}, 7, nullptr, spell_hooks());
    end_turns_until(legacy, 1);
    for (const auto& x : legal_actions(legacy)) CHECK(x.equip_op == 0);
}

TEST_CASE("derive_monster: SRD reproductions and every exact string") {
    auto hide = armor(ArmorCategory::Medium, 12, true, 2);
    auto jav = weapon("javelin", {1, 6, 0}, kWeaponThrown, 5, 30, 120);
    auto greatclub = weapon("greatclub", {1, 8, 0}, kWeaponTwoHanded);
    MonsterSpec ogre; ogre.id = "srd51.monster.ogre"; ogre.abilities = scores(19, 8, 16, 5, 7, 7); ogre.cr_num = 2;
    ogre.has_armor = true; ogre.armor = hide; ogre.printed_ac = 11; ogre.dice_multiplier = 2;
    ogre.attacks = {{"greatclub", greatclub, WeaponUse::TwoHands, {}, false, {}, 6, {2, 8, 4}},
                    {"javelin_thrown", jav, WeaponUse::Thrown, {}, false, {}, 6, {2, 6, 4}}};
    auto d = derive_monster(ogre);
    CHECK(d.errors.empty()); CHECK(d.ac == 11); CHECK(d.pb == 2);
    ogre.dice_multiplier = 1;
    d = derive_monster(ogre);
    REQUIRE(d.errors.size() == 2);
    CHECK(d.errors[0] == "srd51.monster.ogre.greatclub: derived damage 1d8+4 != printed 2d8+4 (weapon_dice_multiplier 2 would match)");
    ogre.dice_multiplier = 2; ogre.attacks[0].printed_damage = {1, 8, 4}; ogre.attacks[1].printed_damage = {1, 6, 4};
    d = derive_monster(ogre);
    CHECK(d.errors.back() == "srd51.monster.ogre: weapon_dice_multiplier is not needed");

    MonsterSpec skel; skel.id = "srd51.monster.skeleton"; skel.abilities = scores(10, 14, 15, 6, 8, 5); skel.cr_num = 1; skel.cr_den = 4;
    skel.printed_ac = 13;
    CHECK(derive_monster(skel).errors == std::vector<std::string>{"srd51.monster.skeleton: derived AC 12 != printed 13"});
    skel.ac_adjust = {true, 1, "SRD Skeleton 'armor scraps': 13 = 10 + Dex 2 + 1; no item@1 armor"};
    CHECK(derive_monster(skel).errors.empty());
    skel.ac_adjust.reason.clear();
    CHECK(derive_monster(skel).errors == std::vector<std::string>{"srd51.monster.skeleton: ac_adjust needs a reason"});
    skel.ac_adjust.reason = "   ";   // schema minLength 1 passes; whitespace-only still needs a reason
    CHECK(derive_monster(skel).errors == std::vector<std::string>{"srd51.monster.skeleton: ac_adjust needs a reason"});
    skel.ac_adjust = {true, 1, "x"}; skel.printed_ac = 12;
    auto e = derive_monster(skel).errors;
    REQUIRE(e.size() == 2); CHECK(e[0] == "srd51.monster.skeleton: ac_adjust is not needed");

    MonsterSpec bear; bear.id = "srd51.monster.black_bear"; bear.abilities = scores(15, 10, 14, 2, 12, 7); bear.cr_num = 1; bear.cr_den = 2;
    bear.has_natural_armor = true; bear.natural_base = 11; bear.printed_ac = 11;
    auto bite = weapon("bite", {1, 6, 0}, 0); bite.natural = true;
    bear.attacks = {{"bite", bite, WeaponUse::OneHand, Ability::Str, false, {}, 3, {1, 6, 2}}};
    e = derive_monster(bear).errors;
    REQUIRE(e.size() == 1);
    CHECK(e[0] == "srd51.monster.black_bear.bite: derived to-hit +4 != printed +3");
    bear.attacks[0].attack_bonus = {true, 3, "SRD Black Bear prints +3; Str 15 + PB 2 = +4"};
    CHECK(derive_monster(bear).errors.empty());
    bear.attacks[0].attack_bonus.reason.clear();
    CHECK(derive_monster(bear).errors == std::vector<std::string>{"srd51.monster.black_bear.bite: attack_bonus needs a reason"});
    bear.attacks[0].attack_bonus.reason = "\n";
    CHECK(derive_monster(bear).errors == std::vector<std::string>{"srd51.monster.black_bear.bite: attack_bonus needs a reason"});
    bear.attacks[0].attack_bonus = {true, 4, "x"}; bear.attacks[0].printed_to_hit = 4;
    CHECK(derive_monster(bear).errors == std::vector<std::string>{"srd51.monster.black_bear.bite: attack_bonus is not needed"});
    bear.attacks[0].attack_bonus = {}; bear.natural_base = 10; bear.printed_ac = 10;   // 10 + Dex 0 already matches
    CHECK(derive_monster(bear).errors == std::vector<std::string>{"srd51.monster.black_bear: natural_armor is not needed"});

    MonsterSpec wolf; wolf.id = "srd51.monster.wolf"; wolf.abilities = scores(12, 15, 12, 3, 12, 6); wolf.cr_num = 1; wolf.cr_den = 4;
    wolf.has_natural_armor = true; wolf.natural_base = 11; wolf.printed_ac = 13;
    auto wb = weapon("bite", {2, 4, 0}, 0); wb.natural = true;
    wolf.attacks = {{"bite", wb, WeaponUse::OneHand, Ability::Dex, false, {}, 4, {2, 4, 2}}};
    CHECK(derive_monster(wolf).errors.empty());
    wolf.has_pb = true; wolf.pb = 3;
    CHECK(derive_monster(wolf).errors[0] == "srd51.monster.wolf: proficiency_bonus 3 != CR table 2");
    wolf.pb_reason = " \t";                                              // whitespace-only = absent
    CHECK(derive_monster(wolf).errors[0] == "srd51.monster.wolf: proficiency_bonus 3 != CR table 2");
    wolf.pb_reason = "SRD prints +3"; CHECK(derive_monster(wolf).errors[0] == "srd51.monster.wolf.bite: derived to-hit +5 != printed +4");
    wolf.has_pb = false;
    wolf.hands = 0; wolf.held = 1;   // #249 checks_v4 strings, byte for byte
    CHECK(derive_monster(wolf).errors == std::vector<std::string>{"srd51.monster.wolf: holds 1 items but has 0 hands"});
    wolf.hands = 2; wolf.held = 2; CHECK(derive_monster(wolf).errors.empty());
    wolf.held = 3; CHECK(derive_monster(wolf).errors[0] == "srd51.monster.wolf: holds 3 items but has 2 hands");
    CHECK(hands_overfull_error("test.v4.bugbear5", 3, 2) == "test.v4.bugbear5: holds 3 items but has 2 hands");
    CHECK(hand_slot_error("test.v4.fighter5", "main", 2) == "test.v4.fighter5: hand 'main' holds 2 items");
    CHECK(proficiency_for_cr(0, 1) == 2); CHECK(proficiency_for_cr(1, 8) == 2); CHECK(proficiency_for_cr(5, 1) == 3);
    CHECK(proficiency_for_cr(12, 1) == 4); CHECK(proficiency_for_cr(13, 1) == 5); CHECK(proficiency_for_cr(30, 1) == 9);
    CHECK(format_dice({1, 4, -1}) == "1d4-1"); CHECK(format_dice({3, 6, 0}) == "3d6"); CHECK(format_dice({0, 0, 1}) == "1");
}

TEST_CASE("hands_needed and recovered_ammo") {
    auto w = make_world();
    CHECK(hands_needed(nullptr) == 1);
    CHECK(hands_needed(&w->defs.at(dLongsword)) == 1);
    CHECK(hands_needed(&w->defs.at(dGreatsword)) == 2);   // two_handed with hands left at 1
    CHECK(hands_needed(&w->defs.at(dLongbow)) == 2);
    CHECK(recovered_ammo(0) == 0); CHECK(recovered_ammo(1) == 0); CHECK(recovered_ammo(2) == 1);
    CHECK(recovered_ammo(7) == 3); CHECK(recovered_ammo(20) == 10); CHECK(recovered_ammo(-3) == 0);
}

TEST_CASE("ground_item_def receives the state being evaluated (AI lookahead copies)") {
    auto w = make_world();
    auto s = begin_encounter({pc(1), monster(2)}, 7, nullptr, equip_hooks(w));
    end_turns_until(s, 1);
    w->views[1] = LoadoutView{};
    const EncounterState* seen = nullptr;
    s.hooks.ground_item_def = [&seen, w](const EncounterState& st, uint32_t) -> const ItemDef* { seen = &st; return &w->defs.at(dLongsword); };
    auto copy = s;
    auto pick = equip(1, EquipOp::PickUp, 50); pick.equip_in_reach = true;
    CHECK(equip_legal(copy, pick).kind == EquipCost::Interaction);
    CHECK(seen == &copy);
    equip_legal(s, pick); CHECK(seen == &s);
}

TEST_CASE("derive_monster: the action `use` (#252 spelling) is used as authored; absent = one_hand") {
    CHECK(weapon_use_from_string("one_hand") == WeaponUse::OneHand);
    CHECK(weapon_use_from_string("two_hands") == WeaponUse::TwoHands);
    CHECK(weapon_use_from_string("thrown") == WeaponUse::Thrown);
    CHECK(weapon_use_from_string("off_hand") == WeaponUse::OffHand);
    CHECK_FALSE(weapon_use_from_string("two_hand").has_value());
    CHECK_FALSE(weapon_use_from_string("Thrown").has_value());
    for (auto u : {WeaponUse::OneHand, WeaponUse::TwoHands, WeaponUse::Thrown, WeaponUse::OffHand})
        CHECK(weapon_use_from_string(weapon_use_name(u)) == u);
    auto ls = weapon("longsword", {1, 8, 0}, kWeaponVersatile); ls.versatile_dice = {1, 10, 0};
    MonsterSpec k; k.id = "test.knight"; k.abilities = scores(16, 10); k.cr_num = 1; k.printed_ac = 10;
    k.attacks = {{"longsword", ls, WeaponUse::TwoHands, {}, false, {}, 5, {1, 10, 3}}};
    CHECK(derive_monster(k).errors.empty());                 // two_hands: the versatile die
    k.attacks[0].has_use = false;                            // `use` absent: one_hand, not inferred from the printed die
    CHECK(derive_monster(k).errors == std::vector<std::string>{"test.knight.longsword: derived damage 1d8+3 != printed 1d10+3"});
    k.attacks[0].printed_damage = {1, 8, 3};
    CHECK(derive_monster(k).errors.empty());
}
