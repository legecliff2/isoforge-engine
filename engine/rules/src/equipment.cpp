#include "rules5e/equipment.hpp"

#include <algorithm>
#include "rules5e/abilities.hpp"
#include "rules5e/conditions.hpp"
#include "rules5e/weapon.hpp"

namespace rules5e {

namespace {

const Combatant* combatant(const EncounterState& s, CombatantId id) {
    for (const auto& c : s.combatants) if (c.id == id) return &c;
    return nullptr;
}
const ItemDef* def_of(const EncounterState& s, uint32_t def) {
    return (def != 0 && s.hooks.item_def) ? s.hooks.item_def(def) : nullptr;
}
bool pure_ranged(const WeaponDef& w) { return w.reach_ft == 0 && w.range_ft > 0; }
bool has(const WeaponDef& w, WeaponProperty p) { return (w.properties & p) != 0; }
// Hands to hold an item: ItemDef::hands, and never fewer than 2 for a two-handed weapon (the property is authoritative even
// if content left `hands` at its default 1). Unknown def = 1.
} // namespace

int hands_needed(const ItemDef* d) {
    if (!d) return 1;
    int need = std::max<int>(1, d->hands);
    if (d->kind == ItemKind::Weapon && (d->weapon.properties & kWeaponTwoHanded) != 0) need = std::max(need, 2);
    return need;
}

namespace {
int empty_hands(const LoadoutView& v) {
    if (v.hand_count == 0) return 0;
    return int(v.main.instance == 0) + int(v.off.instance == 0);
}
const HeldItem* held(const LoadoutView& v, uint32_t instance) {
    if (instance == 0) return nullptr;
    if (v.main.instance == instance) return &v.main;
    if (v.off.instance == instance) return &v.off;
    return nullptr;
}
const HeldItem* held_def(const LoadoutView& v, uint32_t def) {
    if (def == 0) return nullptr;
    if (v.main.instance != 0 && v.main.def == def) return &v.main;
    if (v.off.instance != 0 && v.off.def == def) return &v.off;
    return nullptr;
}
const HeldItem* carried(const LoadoutView& v, uint32_t instance) {
    for (const auto& h : v.carried) if (h.instance == instance && instance != 0) return &h;
    return nullptr;
}
const HeldItem* carried_def(const LoadoutView& v, uint32_t def) {
    for (const auto& h : v.carried) if (h.def == def && h.count > 0) return &h;
    return nullptr;
}
// The other hand (relative to `h`) is empty.
bool other_hand_free(const LoadoutView& v, const HeldItem* h) {
    if (v.hand_count == 0) return false;
    if (h == &v.main) return v.off.instance == 0;
    if (h == &v.off) return v.main.instance == 0;
    return empty_hands(v) >= 1;
}
int clamp_dice_count(int n) { return std::clamp(n, 0, 255); }

} // namespace

// ---------------------------------------------------------------- weapon attacks

Ability weapon_ability(const WeaponDef& w, const WielderView& v) {
    if (v.ability) return *v.ability;
    const int s = modifier(v.abilities, Ability::Str), d = modifier(v.abilities, Ability::Dex);
    if (has(w, kWeaponFinesse)) return d > s ? Ability::Dex : Ability::Str;
    if (pure_ranged(w)) return Ability::Dex;
    return Ability::Str;   // melee, including a thrown melee weapon (SRD "Thrown")
}

DerivedAttack derive_weapon_attack(const WeaponDef& w0, const WielderView& v, WeaponUse use) {
    DerivedAttack out;
    WeaponDef w = w0;
    if (use == WeaponUse::TwoHands && !has(w, kWeaponVersatile) && !has(w, kWeaponTwoHanded)) { out.error = "weapon: not a versatile weapon"; return out; }
    if ((use == WeaponUse::OneHand || use == WeaponUse::OffHand) && has(w, kWeaponTwoHanded) && !pure_ranged(w)) {
        out.error = "weapon: a two-handed weapon needs both hands"; return out;
    }
    if (use == WeaponUse::Thrown && !has(w, kWeaponThrown) && !w.improvised) { out.error = "weapon: not a thrown weapon"; return out; }
    if (use == WeaponUse::OffHand && (!has(w, kWeaponLight) || pure_ranged(w))) {
        out.error = "weapon: the off-hand weapon must be a light melee weapon"; return out;
    }
    // (A pure ranged weapon swung in melee is an improvised weapon, SRD "Ammunition": the caller passes an improvised WeaponDef.)
    const bool melee_use = use != WeaponUse::Thrown && !pure_ranged(w);
    const bool improvised = w.improvised;
    const Ability ab = weapon_ability(w, v);
    const int mod = modifier(v.abilities, ab);
    const int atk_magic = w.attack_magic >= 0 ? w.attack_magic : w.magic_bonus;
    const int dmg_magic = w.damage_magic >= 0 ? w.damage_magic : w.magic_bonus;
    const bool proficient = v.proficient && !improvised;
    int to_hit = mod + (proficient ? v.pb : 0) + atk_magic + v.attack_rows + v.ammo_magic;
    if (v.to_hit_override) to_hit = *v.to_hit_override;
    DiceExpr dice = (use == WeaponUse::TwoHands && w.versatile_dice.count > 0) ? w.versatile_dice : w.dice;
    if (dice.count > 0 && !w.natural) {
        int n = int(dice.count) * std::max<int>(1, v.dice_multiplier);
        if (melee_use && v.extra_melee_dice > 0) n += v.extra_melee_dice;   // Brute: melee weapon hits only (not thrown)
        dice.count = uint8_t(clamp_dice_count(n));
    }
    int ab_term = v.damage_ability_none ? 0 : mod;
    if (use == WeaponUse::OffHand && !v.twf_style) ab_term = std::min(0, ab_term);   // SRD Two-Weapon Fighting: unless negative
    dice.bonus = int16_t(dice.bonus + ab_term + dmg_magic + v.damage_rows + v.ammo_magic);
    Attack& a = out.attack;
    a.id = w.id;
    a.to_hit = int8_t(std::clamp(to_hit, -128, 127));
    a.damage = dice;
    a.type = w.type;
    a.ranged = use == WeaponUse::Thrown || pure_ranged(w);
    a.finesse = has(w, kWeaponFinesse);
    a.properties = w.properties;
    a.qualifiers = uint8_t(w.qualifiers | ((atk_magic > 0 || dmg_magic > 0) ? kAttackMagical : 0));
    if (a.ranged) {
        a.range_ft = w.range_ft; a.long_range_ft = w.long_range_ft;
        a.reach_or_range_ft = uint8_t(std::min<int>(w.range_ft, 255));
    } else {
        a.reach_or_range_ft = w.reach_ft;
    }
    return out;
}

Attack unarmed_strike(const AbilityScores& ab, int pb) {
    const int s = modifier(ab, Ability::Str);
    Attack a{"unarmed_strike", int8_t(s + pb), DiceExpr{0, 0, int16_t(1 + s)}, DamageType::Bludgeoning, 5, false, false};
    return a;
}

// ---------------------------------------------------------------- hands and components

const LoadoutView* loadout_view(const EncounterState& s, CombatantId id) {
    return s.hooks.loadout_of ? s.hooks.loadout_of(s, id) : nullptr;
}

int free_hands(const EncounterState& s, CombatantId id) {
    const LoadoutView* v = loadout_view(s, id);
    return v ? empty_hands(*v) : 2;
}

namespace {

std::string_view weapon_refusal(const EncounterState& s, const LoadoutView& v, uint32_t def, WeaponUse use, const HeldItem* h) {
    const ItemDef* d = def_of(s, def);
    if (!d || !is_weapon(*d)) return {};
    const WeaponDef& w = d->weapon;
    if (use == WeaponUse::Thrown) {
        // Drawing a thrown weapon is part of each attack (Director, Q2): held, or carried with a hand to draw it into.
        if (h) return {};
        const HeldItem* c = carried_def(v, def);
        if (!c) return "attack: nothing to throw";
        if (empty_hands(v) < 1) return "attack: the weapon is not in hand";
        return {};
    }
    if (!h) return "attack: the weapon is not in hand";
    if (d->weapon_requires_lit && (h->flags & kHeldLit) == 0) return "attack: the torch is not lit";
    const bool needs_both = use == WeaponUse::TwoHands || has(w, kWeaponTwoHanded);
    if (needs_both && !other_hand_free(v, h)) return "attack: needs both hands";
    if (has(w, kWeaponAmmunition) && use != WeaponUse::Thrown && pure_ranged(w)) {
        if (!needs_both && !other_hand_free(v, h)) return "attack: loading needs a free hand";   // SRD: one-handed weapon
        bool any_stack = false; int left = 0;   // no stack of that kind = unlimited (Q10)
        for (const auto& c : v.carried) {
            const ItemDef* cd = def_of(s, c.def);
            if (cd && cd->kind == ItemKind::Ammunition && cd->ammo_kind == w.ammo_kind && w.ammo_kind != kAmmoNone) {
                any_stack = true; left += c.count;
            }
        }
        if (any_stack && left <= 0) return "attack: no ammunition";
    }
    return {};
}

} // namespace

std::string_view attack_refusal(const EncounterState& s, CombatantId id, uint8_t attack_index) {
    const LoadoutView* v = loadout_view(s, id);
    if (!v || attack_index >= v->attacks.size()) return {};
    const AttackLink& l = v->attacks[attack_index];
    if (l.def == 0) return {};
    return weapon_refusal(s, *v, l.def, l.use, held_def(*v, l.def));
}

bool can_attack_with(const EncounterState& s, CombatantId id, uint32_t instance, WeaponUse use) {
    const LoadoutView* v = loadout_view(s, id);
    if (!v) return true;
    const HeldItem* h = held(*v, instance);
    const HeldItem* c = h ? h : carried(*v, instance);
    if (!c) return false;
    return weapon_refusal(s, *v, c->def, use, h).empty();
}

std::string_view components_refusal(const EncounterState& s, CombatantId id, const SpellDef& def) {
    const LoadoutView* v = loadout_view(s, id);
    if (!v) return {};
    const bool S = (def.components & kComponentS) != 0, M = (def.components & kComponentM) != 0;
    if (!S && !M) return {};
    const int free = empty_hands(*v);
    const bool priced = def.material_cost_cp > 0;
    auto focus_in = [&](const HeldItem& h) {
        if (h.instance == 0) return false;
        const ItemDef* d = def_of(s, h.def);
        if (!d) return false;
        return d->kind == ItemKind::Focus || (d->kind == ItemKind::Shield && (h.flags & kHeldEmblem) != 0);
    };
    const bool focus_hand = v->hand_count > 0 && (focus_in(v->main) || focus_in(v->off));
    bool worn_symbol = false;
    for (const auto& c : v->carried) {
        const ItemDef* d = def_of(s, c.def);
        if (d && d->kind == ItemKind::Focus && d->holy_symbol && (c.flags & kHeldWorn) != 0) worn_symbol = true;
    }
    if (S) {
        // S+M with the material provided by a focus: the hand holding the focus (or the shield bearing the symbol) also performs
        // the gestures (SRD Components, Q4). Priced materials and S alone need an empty hand.
        if (free >= 1) return {};
        if (M && !priced && focus_hand) return {};
        return "cast: somatic needs a free hand";
    }
    if (free >= 1) return {};
    if (!priced && (focus_hand || worn_symbol)) return {};
    return "cast: material needs a free hand or a focus";
}

bool components_ok(const EncounterState& s, CombatantId id, const SpellDef& def) { return components_refusal(s, id, def).empty(); }

// ---------------------------------------------------------------- equip operations

uint16_t armor_change_rounds(ArmorCategory c, bool don, bool helper) {
    int r = 0;   // SRD: Light 1 / 1 minute, Medium 5 / 1, Heavy 10 / 5 (don / doff); 1 minute = 10 rounds
    switch (c) {
    case ArmorCategory::Light: r = 10; break;
    case ArmorCategory::Medium: r = don ? 50 : 10; break;
    case ArmorCategory::Heavy: r = don ? 100 : 50; break;
    }
    return uint16_t(helper ? r / 2 : r);
}

EquipCost equip_legal(const EncounterState& s, const ActionRequest& r) {
    auto no = [](std::string_view why) { return EquipCost{EquipCost::Refused, why}; };
    const LoadoutView* v = loadout_view(s, r.actor);
    if (!v) return no("equip: equipment play is off");
    const auto op = EquipOp(r.equip_op);
    if (op == EquipOp::None || r.equip_op > uint8_t(EquipOp::DoffArmor)) return no("equip: unknown equip op");
    if (s.ended || s.order.empty() || current_actor(s) != r.actor) return no("equip: not your turn");
    const Combatant* a = combatant(s, r.actor);
    if (!a || a->dead || a->hp <= 0 || is_incapacitated(*a)) return no("equip: the creature cannot act");
    if (op == EquipOp::DonArmor || op == EquipOp::DoffArmor) return no("equip: armor cannot be changed in combat");
    const TurnBudget& b = s.budget;
    auto interaction = [&]() -> EquipCost {
        if (!v->interaction_used) return {EquipCost::Interaction, {}};
        if (b.action) return {EquipCost::Action, {}};
        return no("equip: no object interaction left");
    };
    auto hand_ok = [&](const ItemDef* d) -> bool {
        if (v->hand_count == 0) return false;
        const int need = hands_needed(d);
        if (r.equip_hand == 1) return v->main.instance == 0 && (need == 1 || v->off.instance == 0);
        if (r.equip_hand == 2) return v->off.instance == 0 && (need == 1 || v->main.instance == 0);
        return empty_hands(*v) >= need;
    };
    const HeldItem* h = held(*v, r.equip_item);
    const HeldItem* c = carried(*v, r.equip_item);
    switch (op) {
    case EquipOp::Draw: {
        if (h) return no("equip: the item is already in hand");
        if (!c) return no("equip: the item is not carried");
        const ItemDef* d = def_of(s, c->def);
        if (d && d->kind == ItemKind::Shield) return no("equip: use don_shield for a shield");
        if (d && d->kind == ItemKind::Armor) return no("equip: armor cannot be changed in combat");
        if (!hand_ok(d)) return no("equip: no free hand");
        return interaction();
    }
    case EquipOp::Stow: {
        if (!h) return no("equip: the item is not in hand");
        const ItemDef* d = def_of(s, h->def);
        if (d && d->kind == ItemKind::Shield) return no("equip: use doff_shield for a shield");
        return interaction();
    }
    case EquipOp::Drop:
        if (!h) return no("equip: the item is not in hand");
        return {EquipCost::Free, {}};   // Director Q1: free, not an interaction, own turn only
    case EquipOp::PickUp: {
        if (h || c) return no("equip: the item is already carried");
        if (!r.equip_in_reach) return no("equip: the item is out of reach");
        // equip_item is a ground instance id: resolve its def through Engine Core, never through item_def
        const ItemDef* d = s.hooks.ground_item_def ? s.hooks.ground_item_def(s, r.equip_item) : nullptr;
        if (!d) return no("equip: unknown ground item");
        if (!hand_ok(d)) return no("equip: no free hand");
        return interaction();
    }
    case EquipOp::DonShield: {
        if (h) return no("equip: the item is already in hand");
        if (!c) return no("equip: the item is not carried");
        const ItemDef* d = def_of(s, c->def);
        if (!d || d->kind != ItemKind::Shield) return no("equip: not a shield");
        for (const HeldItem* x : {&v->main, &v->off}) {
            const ItemDef* xd = x->instance ? def_of(s, x->def) : nullptr;
            if (xd && xd->kind == ItemKind::Shield) return no("equip: a shield is already wielded");
        }
        if (!hand_ok(d)) return no("equip: no free hand");
        if (!b.action) return no("equip: no action left");
        return {EquipCost::Action, {}};   // SRD: 1 action
    }
    case EquipOp::DoffShield: {
        if (!h) return no("equip: the item is not in hand");
        const ItemDef* d = def_of(s, h->def);
        if (!d || d->kind != ItemKind::Shield) return no("equip: not a shield");
        if (!b.action) return no("equip: no action left");
        return {EquipCost::Action, {}};
    }
    case EquipOp::HandOver: {
        if (!h) return no("equip: the item is not in hand");
        const Combatant* to = combatant(s, r.equip_to);
        if (!to || to == a || to->dead) return no("equip: bad recipient");
        if (!r.equip_in_reach) return no("equip: the item is out of reach");
        const LoadoutView* tv = loadout_view(s, r.equip_to);
        const ItemDef* d = def_of(s, h->def);
        const int need = hands_needed(d);
        if (!tv || empty_hands(*tv) < need) return no("equip: the recipient has no free hand");
        return interaction();
    }
    default: break;
    }
    return no("equip: unknown equip op");
}

std::vector<ActionRequest> equip_legal_actions(const EncounterState& s) {
    std::vector<ActionRequest> out;
    if (s.ended || s.order.empty()) return out;
    const CombatantId id = current_actor(s);
    const LoadoutView* v = loadout_view(s, id);
    if (!v) return out;
    auto push = [&](EquipOp op, uint32_t instance, uint8_t hand) {
        ActionRequest r{id, ActionKind::UseObject, 0};
        r.equip_op = uint8_t(op); r.equip_item = instance; r.equip_hand = hand;
        if (equip_legal(s, r).kind != EquipCost::Refused) out.push_back(r);
    };
    std::vector<uint32_t> carried_ids, held_ids;
    for (const auto& c : v->carried) if (c.instance) carried_ids.push_back(c.instance);
    for (const HeldItem* x : {&v->main, &v->off}) if (x->instance) held_ids.push_back(x->instance);
    std::sort(carried_ids.begin(), carried_ids.end());
    std::sort(held_ids.begin(), held_ids.end());
    for (uint32_t i : carried_ids) for (uint8_t hand : {uint8_t(1), uint8_t(2)}) push(EquipOp::Draw, i, hand);
    for (uint32_t i : held_ids) push(EquipOp::Stow, i, 0);
    for (uint32_t i : held_ids) push(EquipOp::Drop, i, 0);
    for (uint32_t i : carried_ids) for (uint8_t hand : {uint8_t(1), uint8_t(2)}) push(EquipOp::DonShield, i, hand);
    for (uint32_t i : held_ids) push(EquipOp::DoffShield, i, 0);
    return out;
}

// ---------------------------------------------------------------- monsters

int proficiency_for_cr(int num, int den) {
    if (den <= 0) den = 1;
    // smallest integer CR >= num / den: fractional CRs (1/8 .. 1/2) are below 1
    const int cr = (num + den - 1) / den;
    if (cr <= 4) return 2;
    if (cr >= 29) return 9;
    return 2 + (cr - 1) / 4;   // 5..8 +3, 9..12 +4, ... 25..28 +8
}

std::string format_dice(DiceExpr d) {
    if (d.count == 0) return std::to_string(int(d.bonus));
    std::string s = std::to_string(int(d.count)) + "d" + std::to_string(int(d.sides));
    if (d.bonus > 0) s += "+" + std::to_string(int(d.bonus));
    else if (d.bonus < 0) s += std::to_string(int(d.bonus));
    return s;
}

namespace {
std::string signed_str(int v) { return (v >= 0 ? "+" : "") + std::to_string(v); }
// A reason that is empty or whitespace-only counts as missing (the schema's minLength 1 already rejects empty ones).
bool blank(const std::string& r) {
    for (char ch : r) if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v') return false;
    return true;
}
}

int recovered_ammo(int expended) { return expended > 0 ? expended / 2 : 0; }

std::string hands_overfull_error(std::string_view id, int held, int hands) {
    return std::string(id) + ": holds " + std::to_string(held) + " items but has " + std::to_string(hands) + " hands";
}
std::string hand_slot_error(std::string_view id, std::string_view hand, int count) {
    return std::string(id) + ": hand '" + std::string(hand) + "' holds " + std::to_string(count) + " items";
}

MonsterDerivation derive_monster(const MonsterSpec& m) {
    MonsterDerivation out;
    const int table = proficiency_for_cr(m.cr_num, m.cr_den);
    out.pb = int8_t(m.has_pb ? m.pb : table);
    if (m.has_pb && m.pb != table && blank(m.pb_reason))
        out.errors.push_back(m.id + ": proficiency_bonus " + std::to_string(int(m.pb)) + " != CR table " + std::to_string(table));
    if (m.hands >= 0 && m.held > m.hands) out.errors.push_back(hands_overfull_error(m.id, m.held, m.hands));
    // AC (1.2)
    AcInput in;
    in.abilities = m.abilities;
    in.has_armor = m.has_armor; in.armor = m.armor;
    in.has_shield = m.has_shield; in.shield = m.shield;
    if (m.has_natural_armor) in.bases.push_back(AcBaseCandidate{AcBaseKind::Natural, m.natural_base, Ability::Con, m.natural_dex, false, 0});
    const int without_adjust = compute_ac(in).ac;
    if (m.ac_adjust.present) in.ac_adjust = m.ac_adjust.value;
    out.ac = compute_ac(in).ac;
    if (m.ac_adjust.present && blank(m.ac_adjust.reason)) out.errors.push_back(m.id + ": ac_adjust needs a reason");
    if (m.ac_adjust.present && without_adjust == m.printed_ac) out.errors.push_back(m.id + ": ac_adjust is not needed");
    if (m.has_natural_armor) {
        AcInput plain = in; plain.bases.clear(); plain.ac_adjust = 0;
        AcInput with = in; with.ac_adjust = 0;
        if (m.has_armor || compute_ac(plain).ac == compute_ac(with).ac) out.errors.push_back(m.id + ": natural_armor is not needed");
    }
    if (out.ac != m.printed_ac)
        out.errors.push_back(m.id + ": derived AC " + std::to_string(int(out.ac)) + " != printed " + std::to_string(int(m.printed_ac)));
    // Attacks (2.1, 5.3..5.5)
    bool multiplier_used = false;
    for (const MonsterAttackSpec& as : m.attacks) {
        WielderView v;
        v.abilities = m.abilities; v.pb = out.pb; v.proficient = true;
        v.dice_multiplier = m.dice_multiplier; v.extra_melee_dice = m.brute_dice;
        v.ability = as.ability; v.damage_ability_none = as.damage_ability_none;
        const std::string pre = m.id + "." + as.id;
        DerivedAttack base = derive_weapon_attack(as.weapon, v, as.use);
        if (!base.error.empty()) { out.errors.push_back(pre + ": " + std::string(base.error)); out.attacks.push_back(base.attack); continue; }
        if (as.attack_bonus.present) {
            if (blank(as.attack_bonus.reason)) out.errors.push_back(pre + ": attack_bonus needs a reason");
            if (base.attack.to_hit == as.printed_to_hit) out.errors.push_back(pre + ": attack_bonus is not needed");
            v.to_hit_override = as.attack_bonus.value;
        }
        DerivedAttack d = derive_weapon_attack(as.weapon, v, as.use);
        if (d.attack.to_hit != as.printed_to_hit)
            out.errors.push_back(pre + ": derived to-hit " + signed_str(d.attack.to_hit) + " != printed " + signed_str(as.printed_to_hit));
        if (!as.weapon.natural && m.dice_multiplier > 1) {
            WielderView one = v; one.dice_multiplier = 1;
            if (!(derive_weapon_attack(as.weapon, one, as.use).attack.damage == as.printed_damage)) multiplier_used = true;
        }
        if (!(d.attack.damage == as.printed_damage)) {
            std::string e = pre + ": derived damage " + format_dice(d.attack.damage) + " != printed " + format_dice(as.printed_damage);
            if (!as.weapon.natural)
                for (uint8_t k = 2; k <= 4; ++k) {
                    WielderView mk = v; mk.dice_multiplier = k;
                    if (derive_weapon_attack(as.weapon, mk, as.use).attack.damage == as.printed_damage) {
                        e += " (weapon_dice_multiplier " + std::to_string(int(k)) + " would match)"; break;
                    }
                }
            out.errors.push_back(e);
        }
        out.attacks.push_back(d.attack);
    }
    if (m.dice_multiplier > 1 && !multiplier_used) out.errors.push_back(m.id + ": weapon_dice_multiplier is not needed");
    return out;
}

} // namespace rules5e
