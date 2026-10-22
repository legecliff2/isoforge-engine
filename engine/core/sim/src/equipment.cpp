#include "sim/equipment.hpp"

#include <algorithm>

#include "base/canonical_hash.hpp"
#include "rules5e/armor.hpp"
#include "rules5e/equipment.hpp"
#include "rules5e/weapon.hpp"
#include "sim/content.hpp"

namespace eng::sim {

using rules5e::ItemDef;
using rules5e::ItemKind;
using assets::Json;

uint32_t item_def_id(std::string_view content_id) {
    uint32_t h = 2166136261u;
    for (const char ch : content_id) { h ^= uint8_t(ch); h *= 16777619u; }
    return h == 0 ? 1u : h;
}

namespace {

std::string last_segment(const std::string& id) {
    const std::size_t dot = id.rfind('.');
    return dot == std::string::npos ? id : id.substr(dot + 1);
}

int8_t clamp8(int v) { return int8_t(std::clamp(v, -128, 127)); }

std::optional<rules5e::DiceExpr> damage_of(const Json& arr, rules5e::DamageType* type) {
    if (!arr.is_array() || arr.empty() || !arr[0].is_object()) return std::nullopt;
    const Json& d = arr[0];
    if (type)
        if (auto t = d.find("type"); t != d.end() && t->is_string())
            if (auto dt = damage_type_from_name(t->get<std::string>())) *type = *dt;
    if (auto f = d.find("flat"); f != d.end() && f->is_number_integer())
        return rules5e::DiceExpr{0, 0, int16_t(std::clamp<int64_t>(f->get<int64_t>(), 0, 32767))};
    if (auto x = d.find("dice"); x != d.end() && x->is_string()) return parse_dice(x->get<std::string>());
    return std::nullopt;
}

uint8_t ammo_kind_of(const std::string& seg) {
    if (seg.find("crossbow") != std::string::npos || seg.find("bolt") != std::string::npos) return rules5e::kAmmoBolt;
    if (seg.find("bow") != std::string::npos || seg.find("arrow") != std::string::npos) return rules5e::kAmmoArrow;
    if (seg.find("sling") != std::string::npos) return rules5e::kAmmoSlingBullet;
    if (seg.find("blowgun") != std::string::npos || seg.find("needle") != std::string::npos) return rules5e::kAmmoNeedle;
    return rules5e::kAmmoNone;
}

int int_or(const Json& doc, const char* key, int dflt) {
    auto it = doc.find(key);
    return it != doc.end() && it->is_number_integer() ? int(std::clamp<int64_t>(it->get<int64_t>(), -32768, 32767)) : dflt;
}

// Attack ids and item ids name the same weapon with the words in either order ("light_crossbow" / "crossbow_light").
std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : s) {
        if (ch == '_' || ch == '-' || ch == ' ' || ch == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur.push_back(char(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch));
    }
    if (!cur.empty()) out.push_back(cur);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

std::optional<std::string> ItemDefTable::add(const Json& doc) {
    const std::string cid = doc.value("id", "");
    if (cid.empty()) return std::string("item: missing id");
    const std::string cat = doc.value("category", "");
    ItemDef d;
    if (cat == "weapon") {
        d.kind = ItemKind::Weapon;
        rules5e::WeaponDef& w = d.weapon;
        uint16_t props = 0;
        if (auto p = doc.find("properties"); p != doc.end() && p->is_array())
            for (const Json& x : *p)
                if (x.is_string())
                    if (auto bit = rules5e::weapon_property_from_string(x.get<std::string>())) props = uint16_t(props | *bit);
        w.properties = props;
        if (auto r = doc.find("range"); r != doc.end() && r->is_array() && r->size() == 2 && (*r)[0].is_number_integer() &&
                                        (*r)[1].is_number_integer()) {
            w.range_ft = uint16_t(std::clamp<int64_t>((*r)[0].get<int64_t>(), 0, 65535));
            w.long_range_ft = uint16_t(std::clamp<int64_t>((*r)[1].get<int64_t>(), 0, 65535));
        }
        const bool has_reach = doc.contains("reach");
        w.reach_ft = uint8_t(std::clamp(int_or(doc, "reach", (w.range_ft > 0 && !has_reach) ? 0 : 5), 0, 255));
        if (auto dm = doc.find("damage"); dm != doc.end())
            if (auto dice = damage_of(*dm, &w.type)) w.dice = *dice;
        if (auto vd = doc.find("versatile_damage"); vd != doc.end())
            if (auto dice = damage_of(*vd, nullptr)) w.versatile_dice = *dice;
        uint8_t q = 0;
        if (auto qs = doc.find("qualifiers"); qs != doc.end() && qs->is_array())
            for (const Json& x : *qs) {
                const std::string s = x.is_string() ? x.get<std::string>() : std::string();
                if (s == "magical") q = uint8_t(q | 1u);
                else if (s == "silvered") q = uint8_t(q | 2u);
                else if (s == "adamantine") q = uint8_t(q | 4u);
            }
        w.qualifiers = q;
        w.magic_bonus = int8_t(std::clamp(int_or(doc, "magic_bonus", 0), 0, 3));
        w.martial = doc.value("weapon_class", "") == "martial";
        if (props & rules5e::kWeaponAmmunition) w.ammo_kind = ammo_kind_of(last_segment(cid));
        d.hands = (props & rules5e::kWeaponTwoHanded) ? 2 : 1;
    } else if (cat == "armor") {
        d.kind = ItemKind::Armor;
        d.hands = 0;
        const std::string ac = doc.value("armor_category", "light");
        d.armor.category = ac == "heavy" ? rules5e::ArmorCategory::Heavy
                         : ac == "medium" ? rules5e::ArmorCategory::Medium : rules5e::ArmorCategory::Light;
        d.armor.base_ac = int16_t(int_or(doc, "base_ac", 10));
        d.armor.dex_bonus = doc.value("dex_bonus", d.armor.category != rules5e::ArmorCategory::Heavy);
        if (doc.contains("dex_cap")) { d.armor.has_dex_cap = true; d.armor.dex_cap = clamp8(int_or(doc, "dex_cap", 2)); }
        d.armor.str_min = clamp8(int_or(doc, "str_min", 0));
        d.armor.stealth_disadvantage = doc.value("stealth_disadvantage", false);
        d.armor.magic_bonus = clamp8(int_or(doc, "magic_bonus", 0));
    } else if (cat == "shield") {
        d.kind = ItemKind::Shield;
        d.shield.ac_bonus = clamp8(int_or(doc, "ac_bonus", 2));
        d.shield.magic_bonus = clamp8(int_or(doc, "magic_bonus", 0));
    } else if (cat == "gear") {
        d.kind = ItemKind::Other;
        d.hands = uint8_t(std::clamp(int_or(doc, "hands", 1), 0, 2));
        if (auto lj = doc.find("light"); lj != doc.end() && lj->is_object()) {
            d.kind = ItemKind::Light;
            rules5e::LightDef& L = d.light;
            L.states.push_back({uint16_t(std::clamp(int_or(*lj, "bright_ft", 0), 0, 65535)),
                                uint16_t(std::clamp(int_or(*lj, "dim_ft", 0), 0, 65535))});   // state 0: top-level radii
            L.shape = lj->value("shape", "radius") == "cone" ? rules5e::LightShape::Cone : rules5e::LightShape::Radius;
            if (auto du = lj->find("duration_rounds"); du != lj->end() && du->is_number_integer())
                L.duration_rounds = uint32_t(std::clamp<int64_t>(du->get<int64_t>(), 0, 0xFFFFFFFFll));
            L.lit_by_default = lj->value("lit_by_default", false);
            if (auto st = lj->find("states"); st != lj->end() && st->is_array())
                for (const Json& x : *st)
                    if (x.is_object() && L.states.size() < rules5e::kMaxLightStates)
                        L.states.push_back({uint16_t(std::clamp(int_or(x, "bright_ft", 0), 0, 65535)),
                                                   uint16_t(std::clamp(int_or(x, "dim_ft", 0), 0, 65535))});
        }
        if (auto ta = doc.find("torch_attack"); ta != doc.end() && ta->is_object()) {
            d.has_weapon = true;
            d.weapon.improvised = true;
            d.weapon.reach_ft = 5;
            d.weapon_requires_lit = ta->value("requires_lit", false);
            if (auto dm = ta->find("damage"); dm != ta->end())
                if (auto dice = damage_of(*dm, &d.weapon.type)) d.weapon.dice = *dice;
        }
    } else {
        d.kind = ItemKind::Other;   // valuables (gems): carried, never held
    }
    return insert(cid, d);
}

std::optional<std::string> ItemDefTable::add(const std::string& content_id, ItemDef def) {
    return insert(content_id, def);
}

std::optional<std::string> ItemDefTable::insert(const std::string& content_id, ItemDef def) {
    const uint32_t key = item_def_id(content_id);
    if (auto it = by_id_.find(key); it != by_id_.end()) {
        if (it->second.content_id == content_id) return content_id + ": duplicate item id";
        return content_id + ": item def id collides with '" + it->second.content_id + "'";
    }
    def.id = key;
    strings_.push_back(last_segment(content_id));
    def.weapon.id = strings_.back();
    if (def.kind == ItemKind::Weapon) def.has_weapon = true;
    by_id_.emplace(key, Entry{content_id, def});
    return std::nullopt;
}

const ItemDef* ItemDefTable::find(uint32_t def) const {
    auto it = by_id_.find(def);
    return it == by_id_.end() ? nullptr : &it->second.def;
}

const std::string* ItemDefTable::content_id(uint32_t def) const {
    auto it = by_id_.find(def);
    return it == by_id_.end() ? nullptr : &it->second.content_id;
}

rules5e::Loadout* find_loadout(rules5e::EncounterState& st, rules5e::CombatantId id) {
    for (auto& l : st.loadouts) if (l.owner == id) return &l;
    return nullptr;
}
const rules5e::Loadout* find_loadout(const rules5e::EncounterState& st, rules5e::CombatantId id) {
    for (const auto& l : st.loadouts) if (l.owner == id) return &l;
    return nullptr;
}

namespace {

const rules5e::Combatant* combatant_of(const rules5e::EncounterState& st, rules5e::CombatantId id) {
    for (const auto& c : st.combatants) if (c.id == id) return &c;
    return nullptr;
}

const ItemDef* held_shield(const rules5e::LoadoutView& v, const ItemDefTable& items) {
    for (const rules5e::HeldItem* h : {&v.main, &v.off})
        if (h->instance != 0)
            if (const ItemDef* d = items.find(h->def); d && d->kind == ItemKind::Shield) return d;
    return nullptr;
}

// The derived numbers before the setup adjustments.
struct Raw {
    int ac = 10;
    struct A { bool ok = false; int to_hit = 0, dmg = 0; };
    std::vector<A> attacks;
};

Raw derive_raw(const rules5e::Combatant& c, const rules5e::Loadout& l, const ItemDefTable& items) {
    Raw r;
    rules5e::AcInput in;
    in.abilities = c.abilities;
    if (const ItemDef* a = items.find(l.view.armor_def); a && a->kind == ItemKind::Armor) { in.has_armor = true; in.armor = a->armor; }
    if (const ItemDef* s = held_shield(l.view, items)) { in.has_shield = true; in.shield = s->shield; }
    r.ac = rules5e::compute_ac(in).ac;
    r.attacks.resize(l.view.attacks.size());
    for (std::size_t i = 0; i < l.view.attacks.size(); ++i) {
        const rules5e::AttackLink& link = l.view.attacks[i];
        const ItemDef* d = link.def ? items.find(link.def) : nullptr;
        if (!d || !rules5e::is_weapon(*d)) continue;
        rules5e::WielderView wv;
        wv.abilities = c.abilities;
        wv.pb = c.proficiency_bonus;
        const rules5e::DerivedAttack da = rules5e::derive_weapon_attack(d->weapon, wv, link.use);
        if (!da.error.empty()) continue;
        r.attacks[i] = {true, int(da.attack.to_hit), int(da.attack.damage.bonus)};
    }
    return r;
}

} // namespace

bool build_loadout(rules5e::EncounterState& st, rules5e::CombatantId owner, const std::vector<LoadoutSeed>& seeds,
                   const ItemDefTable& items, uint32_t two_handed_grip, std::string_view content_id,
                   std::vector<std::string>* errors) {
    int held_slots = 0;   // hand slots the equipped holdables take (#249 _held_slots)
    const rules5e::Combatant* c = combatant_of(st, owner);
    if (!c || find_loadout(st, owner)) return false;
    rules5e::Loadout l;
    l.owner = owner;
    rules5e::LoadoutView& v = l.view;
    bool any = false;
    for (const LoadoutSeed& sd : seeds) {
        const ItemDef* d = items.find(sd.def);
        if (!d) continue;
        any = true;
        rules5e::HeldItem h{st.next_item_id++, sd.def, sd.count, 0};
        if (d->kind == ItemKind::Light && d->light.lit_by_default && sd.equipped) h.flags = rules5e::kHeldLit;
        if (sd.equipped && d->kind == ItemKind::Armor && v.armor_def == 0) { v.armor_def = sd.def; continue; }
        const bool holdable = d->kind == ItemKind::Weapon || d->kind == ItemKind::Shield || d->kind == ItemKind::Light ||
                              d->kind == ItemKind::Focus || d->has_weapon;
        if (sd.equipped && holdable) held_slots += d->kind == ItemKind::Shield ? 1 : rules5e::hands_needed(d);
        if (sd.equipped && holdable && v.hand_count > 0) {
            const bool both = rules5e::hands_needed(d) >= 2;
            if (d->kind == ItemKind::Shield && v.off.instance == 0) { v.off = h; continue; }
            if (v.main.instance == 0 && (!both || v.off.instance == 0)) { v.main = h; continue; }
            if (!both && v.off.instance == 0) { v.off = h; continue; }
        }
        v.carried.push_back(h);
    }
    if (!any) return false;
    if (errors && held_slots > v.hand_count)
        errors->push_back(rules5e::hands_overfull_error(content_id, held_slots, int(v.hand_count)));
    // Attack links: Combatant::attacks[i] uses the loadout weapon whose id words match the attack id (held first).
    std::vector<rules5e::AttackLink> links(c->attacks.size());
    bool linked = false;
    std::vector<const rules5e::HeldItem*> pool{&v.main, &v.off};
    for (const auto& x : v.carried) pool.push_back(&x);
    for (std::size_t i = 0; i < c->attacks.size(); ++i) {
        const rules5e::Attack& at = c->attacks[i];
        const std::vector<std::string> aw = words(at.id);
        for (const rules5e::HeldItem* h : pool) {
            if (h->instance == 0) continue;
            const ItemDef* d = items.find(h->def);
            if (!d || !rules5e::is_weapon(*d) || words(d->weapon.id) != aw) continue;
            const uint16_t p = d->weapon.properties;
            rules5e::WeaponUse use = rules5e::WeaponUse::OneHand;
            if (at.ranged && (p & rules5e::kWeaponThrown) && d->weapon.reach_ft > 0) use = rules5e::WeaponUse::Thrown;
            else if (p & rules5e::kWeaponTwoHanded) use = rules5e::WeaponUse::TwoHands;
            else if ((p & rules5e::kWeaponVersatile) && i < 32 && (two_handed_grip & (1u << i))) use = rules5e::WeaponUse::TwoHands;
            links[i] = {h->def, use};
            linked = true;
            break;
        }
    }
    if (linked) v.attacks = std::move(links);
    // Calibration: printed minus derived, so the setup recompute is the identity.
    const Raw r = derive_raw(*c, l, items);
    l.ac_adjust = clamp8(int(c->ac) - r.ac);
    l.hit_adjust.assign(v.attacks.size(), 0);
    l.dmg_adjust.assign(v.attacks.size(), 0);
    for (std::size_t i = 0; i < v.attacks.size(); ++i)
        if (r.attacks[i].ok) {
            l.hit_adjust[i] = clamp8(int(c->attacks[i].to_hit) - r.attacks[i].to_hit);
            l.dmg_adjust[i] = clamp8(int(c->attacks[i].damage.bonus) - r.attacks[i].dmg);
        }
    st.loadouts.push_back(std::move(l));
    return true;
}

void recompute_derived(rules5e::EncounterState& st, rules5e::CombatantId id, const ItemDefTable& items) {
    const rules5e::Loadout* l = find_loadout(st, id);
    rules5e::Combatant* c = nullptr;
    for (auto& x : st.combatants) if (x.id == id) c = &x;
    if (!l || !c) return;
    const Raw r = derive_raw(*c, *l, items);
    c->ac = clamp8(r.ac + l->ac_adjust);
    for (std::size_t i = 0; i < r.attacks.size() && i < c->attacks.size(); ++i) {
        if (!r.attacks[i].ok) continue;
        const int adj_hit = i < l->hit_adjust.size() ? l->hit_adjust[i] : 0;
        const int adj_dmg = i < l->dmg_adjust.size() ? l->dmg_adjust[i] : 0;
        c->attacks[i].to_hit = clamp8(r.attacks[i].to_hit + adj_hit);
        c->attacks[i].damage.bonus = int16_t(r.attacks[i].dmg + adj_dmg);
    }
}

DeriveInputs derive_inputs(const rules5e::EncounterState& st, rules5e::CombatantId id) {
    DeriveInputs d;
    if (const rules5e::Combatant* c = combatant_of(st, id)) {
        d.abilities = c->abilities.score;
        d.pb = c->proficiency_bonus;
        d.caster_level = c->caster_level;
        d.hit_dice_max = c->hit_dice_max;
    }
    for (const auto& m : st.modifiers)
        if (m.target == id && m.spec.stat == rules5e::ModStat::Ac) ++d.ac_effects;
    return d;
}

uint64_t loadout_digest(const rules5e::EncounterState& st) {
    CanonicalHasher h;
    auto item = [&](const rules5e::HeldItem& x) { h.u32(x.instance); h.u32(x.def); h.u16(x.count); h.byte(x.flags); };
    h.u32(st.next_item_id);
    h.u32(uint32_t(st.loadouts.size()));
    for (const auto& l : st.loadouts) {
        h.u32(l.owner);
        h.byte(l.view.hand_count);
        item(l.view.main);
        item(l.view.off);
        h.u32(l.view.armor_def);
        h.u32(uint32_t(l.view.carried.size()));
        for (const auto& x : l.view.carried) item(x);
        h.u32(uint32_t(l.view.attacks.size()));
        for (std::size_t i = 0; i < l.view.attacks.size(); ++i) {
            h.u32(l.view.attacks[i].def);
            h.byte(uint8_t(l.view.attacks[i].use));
            h.byte(uint8_t(i < l.hit_adjust.size() ? l.hit_adjust[i] : 0));
            h.byte(uint8_t(i < l.dmg_adjust.size() ? l.dmg_adjust[i] : 0));
        }
        h.byte(uint8_t(l.ac_adjust));
    }
    // Expended ammunition (EQUIPMENT.md 2.5.1): only once some loadout has spent any, so earlier digests are unchanged.
    bool any = false;
    for (const auto& l : st.loadouts) any = any || !l.expended.empty();
    if (any) {
        h.byte(0xE5);
        for (const auto& l : st.loadouts) {
            h.u32(uint32_t(l.expended.size()));
            for (const auto& x : l.expended) { h.u32(x.def); h.u16(x.count); }
        }
    }
    return h.value();
}

} // namespace eng::sim
