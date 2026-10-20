#include "rules5e_content/spell_loader.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <map>

#include "assets/content_migrations.hpp"
#include "rules5e/dice.hpp"

namespace rules5e_content {

using eng::assets::Json;
using namespace rules5e;

namespace {

template <class E>
bool lookup(const std::map<std::string, E>& m, const Json& v, E& out) {
    if (!v.is_string()) return false;
    auto it = m.find(v.get<std::string>());
    if (it == m.end()) return false;
    out = it->second;
    return true;
}

const std::map<std::string, Ability> kAbility{{"str", Ability::Str}, {"dex", Ability::Dex}, {"con", Ability::Con},
                                              {"int", Ability::Int}, {"wis", Ability::Wis}, {"cha", Ability::Cha}};
const std::map<std::string, DamageType> kDamage{
    {"acid", DamageType::Acid}, {"bludgeoning", DamageType::Bludgeoning}, {"cold", DamageType::Cold},
    {"fire", DamageType::Fire}, {"force", DamageType::Force}, {"lightning", DamageType::Lightning},
    {"necrotic", DamageType::Necrotic}, {"piercing", DamageType::Piercing}, {"poison", DamageType::Poison},
    {"psychic", DamageType::Psychic}, {"radiant", DamageType::Radiant}, {"slashing", DamageType::Slashing},
    {"thunder", DamageType::Thunder}};
const std::map<std::string, Condition> kCondition{
    {"blinded", Condition::Blinded}, {"charmed", Condition::Charmed}, {"deafened", Condition::Deafened},
    {"frightened", Condition::Frightened}, {"grappled", Condition::Grappled}, {"incapacitated", Condition::Incapacitated},
    {"invisible", Condition::Invisible}, {"paralyzed", Condition::Paralyzed}, {"petrified", Condition::Petrified},
    {"poisoned", Condition::Poisoned}, {"prone", Condition::Prone}, {"restrained", Condition::Restrained},
    {"stunned", Condition::Stunned}, {"unconscious", Condition::Unconscious}, {"exhaustion", Condition::Exhaustion}};

const std::map<std::string, CreatureType> kCreatureType{
    {"aberration", CreatureType::Aberration}, {"beast", CreatureType::Beast}, {"celestial", CreatureType::Celestial},
    {"construct", CreatureType::Construct}, {"dragon", CreatureType::Dragon}, {"elemental", CreatureType::Elemental},
    {"fey", CreatureType::Fey}, {"fiend", CreatureType::Fiend}, {"giant", CreatureType::Giant},
    {"humanoid", CreatureType::Humanoid}, {"monstrosity", CreatureType::Monstrosity}, {"ooze", CreatureType::Ooze},
    {"plant", CreatureType::Plant}, {"undead", CreatureType::Undead}};

SpellLoad err(std::string m) { return {std::nullopt, std::move(m)}; }

bool small_int(const Json& v, int lo, int hi, int& out) {
    if (!v.is_number_integer()) return false;
    auto x = v.get<int64_t>();
    if (x < lo || x > hi) return false;
    out = int(x);
    return true;
}

bool read_damage(const Json& d, SpellDef& s) {
    if (!d.is_object() || !d.contains("dice") || !d.contains("type") || !d["dice"].is_string()) return false;
    auto e = parse_dice(d["dice"].get<std::string>());
    if (!e) return false;
    s.dice = *e;
    return lookup(kDamage, d["type"], s.damage_type);
}

// ---- R-G11 zones (docs/CONTENT_CONVENTIONS.md section 4; schemas/spell/v1.schema.json $defs zone / zone_trigger /
// zone_effect; tools/validate check_spell_zone) ----
bool only_keys(const Json& o, std::initializer_list<const char*> keys, std::string& bad) {
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (std::none_of(keys.begin(), keys.end(), [&](const char* k) { return it.key() == k; })) { bad = it.key(); return false; }
    }
    return true;
}
bool feet_5(const Json& v, int& out) { return small_int(v, 5, 65530, out) && out % 5 == 0; }
bool bool_key(const Json& o, const char* k, bool& out) {
    if (!o.contains(k)) return true;
    if (!o[k].is_boolean()) return false;
    out = o[k].get<bool>();
    return true;
}

// One trigger row's effect: save / damage (no save) / apply_condition (4.3).
std::string parse_zone_effect(const Json& e, const std::string& where, SpellEffect& fx) {
    if (!e.is_object() || !e.contains("kind") || !e["kind"].is_string()) return where + ".effect.kind missing";
    const std::string k = e["kind"].get<std::string>();
    if (k != "save" && k != "damage" && k != "apply_condition")
        return where + ".effect.kind '" + k + "' is not a zone trigger effect (save, damage, apply_condition)";
    std::string bad;
    if (k == "save" && !only_keys(e, {"kind", "ability", "damage", "on_success", "condition", "description"}, bad))
        return where + ".effect: unknown key '" + bad + "'";
    if (k == "damage" && !only_keys(e, {"kind", "damage", "condition", "description"}, bad))
        return where + ".effect: unknown key '" + bad + "'";
    if (k == "apply_condition" && !only_keys(e, {"kind", "condition", "description"}, bad))
        return where + ".effect: unknown key '" + bad + "'";
    SpellDef scratch;   // read_damage writes a SpellDef
    if (e.contains("damage")) {
        if (!read_damage(e["damage"], scratch)) return where + ".effect damage invalid";
        fx.dice = scratch.dice; fx.damage_type = scratch.damage_type;
    }
    if (e.contains("condition")) {
        if (!lookup(kCondition, e["condition"], fx.condition)) return where + ".effect condition invalid";
        fx.applies_condition = true;
    }
    if (k == "save") {
        fx.kind = SpellEffectKind::Save;
        if (!e.contains("ability") || !lookup(kAbility, e["ability"], fx.save_ability)) return where + ".effect ability invalid";
        if (e.contains("damage") != e.contains("on_success"))
            return where + (e.contains("damage") ? ".effect: 'on_success' is a dependency of 'damage'" : ".effect: 'damage' is a dependency of 'on_success'");
        if (e.contains("on_success")) {
            const std::string os = e.value("on_success", "");
            if (os != "half" && os != "none") return where + ".effect on_success must be half or none";
            fx.on_save = os == "half" ? OnSave::Half : OnSave::None;
        }
        if (!e.contains("damage") && !fx.applies_condition) return where + ".effect: save needs damage or condition";
    } else {   // no save: rules applies the damage and the condition outright (any non-Save kind)
        fx.kind = SpellEffectKind::ApplyCondition;
        if (k == "damage" && !e.contains("damage")) return where + ".effect damage invalid";
        if (k == "apply_condition" && !e.contains("condition")) return where + ".effect condition invalid";
    }
    return "";
}

// The `zone` object of a kind-zone effect (4.1, 4.2) into s.zone. Cross-field rules that need the rest of the document are
// checked by the caller.
std::string parse_zone(const Json& z, SpellDef& s) {
    if (!z.is_object()) return "zone missing (kind zone needs a zone object)";
    std::string bad;
    if (!only_keys(z, {"shape", "size_ft", "height_ft", "anchor", "flags", "move", "chooses_excluded", "needs_conscious_owner",
                       "triggers", "length_ft", "upcast_size_ft", "ring_size_ft", "sided", "side_ft", "aura_modifier",
                       "aura_bonus_ability", "aura_immunity", "aura_allies_only"}, bad))
        return "zone: unknown key '" + bad + "'";
    static const std::map<std::string, AreaShape> kZoneShape{{"sphere", AreaShape::Sphere}, {"cylinder", AreaShape::Cylinder},
                                                             {"cube", AreaShape::Cube}, {"line", AreaShape::Line}};
    ZoneSpec& zs = s.zone;
    if (!lookup(kZoneShape, z.value("shape", Json()), zs.shape)) return "zone.shape must be sphere, cylinder, cube or line";
    const bool line = zs.shape == AreaShape::Line;
    int v = 0;
    if (line) {
        if (z.contains("size_ft")) return "zone.size_ft not allowed on a line (length_ft / height_ft give a wall's geometry)";
    } else {
        if (!z.contains("size_ft") || !feet_5(z["size_ft"], v)) return "zone.size_ft invalid (5..65530, a multiple of 5)";
        zs.size_ft = uint16_t(v);
    }
    if (line != z.contains("length_ft")) return "zone.length_ft is required for (and only for) lines";
    if (line) {
        if (!feet_5(z["length_ft"], v)) return "zone.length_ft invalid (5..65530, a multiple of 5)";
        zs.length_ft = uint16_t(v);
    }
    if ((zs.shape == AreaShape::Cylinder || line) != z.contains("height_ft"))
        return "zone.height_ft is required for (and only for) cylinders and lines";
    if (z.contains("height_ft")) {
        if (!feet_5(z["height_ft"], v)) return "zone.height_ft invalid (5..65530, a multiple of 5)";
        zs.height_ft = uint16_t(v);
    }
    static const std::map<std::string, ZoneAnchor> kAnchor{{"point", ZoneAnchor::Point}, {"creature", ZoneAnchor::Creature}};
    if (!lookup(kAnchor, z.value("anchor", Json()), zs.anchor)) return "zone.anchor must be point or creature";
    if (z.contains("flags")) {
        static const std::map<std::string, uint8_t> kFlag{{"difficult", kZoneDifficult}, {"light_obscured", kZoneLightObscured},
            {"heavy_obscured", kZoneHeavyObscured}, {"magical_darkness", kZoneMagicalDarkness}, {"silence", kZoneSilence},
            {"halve_speed", kZoneHalveSpeed}, {"opaque", kZoneOpaque}};
        const Json& f = z["flags"];
        if (!f.is_array() || f.empty()) return "zone.flags must be a non-empty array of flag names";
        for (const Json& x : f) {
            uint8_t b = 0;
            if (!lookup(kFlag, x, b)) return "zone.flags: unknown flag";
            if (zs.flags & b) return "zone.flags: duplicate flag";
            zs.flags = uint8_t(zs.flags | b);
        }
    }
    if (z.contains("move")) {
        const Json& m = z["move"];
        if (!m.is_object() || !only_keys(m, {"cost", "feet"}, bad)) return "zone.move must be {cost, feet}";
        const std::string cost = m.value("cost", "");
        if (cost != "action" && cost != "bonus_action") return "zone.move.cost must be action or bonus_action";
        zs.move_cost = cost == "action" ? 1 : 2;
        if (!m.contains("feet") || !feet_5(m["feet"], v)) return "zone.move.feet invalid (5..65530, a multiple of 5)";
        zs.move_ft = uint16_t(v);
    }
    if (z.contains("upcast_size_ft")) {
        if (!feet_5(z["upcast_size_ft"], v)) return "zone.upcast_size_ft invalid (5..65530, a multiple of 5)";
        if (line) return "zone: upcast_size_ft not allowed on a line";
        zs.upcast_size_ft = uint16_t(v);
    }
    if (z.contains("ring_size_ft")) {
        if (!feet_5(z["ring_size_ft"], v)) return "zone.ring_size_ft invalid (5..65530, a multiple of 5)";
        if (!line) return "zone: ring_size_ft needs shape line";
        zs.ring_size_ft = uint16_t(v);
    }
    if (!bool_key(z, "sided", zs.sided)) return "zone.sided must be a boolean";
    if (zs.sided && !line) return "zone: sided needs shape line";
    if (zs.sided != z.contains("side_ft")) return "zone: side_ft is required for (and only for) sided zones";
    if (z.contains("side_ft")) {
        if (!feet_5(z["side_ft"], v)) return "zone.side_ft invalid (5..65530, a multiple of 5)";
        zs.side_ft = uint16_t(v);
    }
    if (z.contains("aura_modifier")) {
        // A passive aura has no duration of its own (it lasts while the creature is in the zone): a closed {stat, bonus, ability}.
        const Json& am = z["aura_modifier"];
        if (!am.is_object() || !only_keys(am, {"stat", "bonus", "ability"}, bad))
            return "zone.aura_modifier must be {stat, bonus, ability}";
        if (am.value("stat", Json()) != "save") return "zone.aura_modifier stat must be save";
        zs.aura_modifier.stat = ModStat::Save;
        if (am.contains("bonus")) {
            if (!small_int(am["bonus"], -30, 30, v)) return "zone.aura_modifier bonus invalid (-30..30)";
            zs.aura_modifier.bonus = int16_t(v);
        }
        if (am.contains("ability")) {
            if (!lookup(kAbility, am["ability"], zs.aura_modifier.ability)) return "zone.aura_modifier ability invalid";
            zs.aura_modifier.has_ability = true;
        }
        zs.aura_modifier.id = spell_id_of(s.content_id + "#aura");
    }
    if (z.contains("aura_bonus_ability")) {
        if (!lookup(kAbility, z["aura_bonus_ability"], zs.aura_bonus_ability)) return "zone.aura_bonus_ability invalid";
        if (!z.contains("aura_modifier")) return "zone: aura_bonus_ability needs aura_modifier";
        zs.has_aura_bonus_ability = true;
    }
    if (z.contains("aura_immunity")) {
        const Json& ai = z["aura_immunity"];
        if (!ai.is_array() || ai.empty()) return "zone.aura_immunity must be a non-empty array of unique conditions";
        for (const Json& x : ai) {
            Condition c{};
            if (!lookup(kCondition, x, c) || (zs.aura_immunity & bit(c))) return "zone.aura_immunity must be a non-empty array of unique conditions";
            zs.aura_immunity |= bit(c);
        }
    }
    if (!bool_key(z, "aura_allies_only", zs.aura_allies_only)) return "zone.aura_allies_only must be a boolean";
    if ((z.contains("aura_modifier") || z.contains("aura_immunity") || zs.aura_allies_only) && zs.anchor != ZoneAnchor::Creature)
        return "zone: aura fields need anchor creature";
    if (!bool_key(z, "chooses_excluded", zs.chooses_excluded)) return "zone.chooses_excluded must be a boolean";
    if (!bool_key(z, "needs_conscious_owner", zs.needs_conscious_owner)) return "zone.needs_conscious_owner must be a boolean";
    if (z.contains("triggers")) {
        static const std::map<std::string, ZoneTrigger> kOn{{"zone_appear", ZoneTrigger::Appear}, {"zone_enter", ZoneTrigger::Enter},
            {"zone_start_turn", ZoneTrigger::StartTurn}, {"zone_end_turn", ZoneTrigger::EndTurn},
            {"zone_move_within", ZoneTrigger::MoveWithin}};
        const Json& ts = z["triggers"];
        if (!ts.is_array() || ts.empty()) return "zone.triggers must be a non-empty array";
        for (std::size_t i = 0; i < ts.size(); ++i) {
            const std::string where = "zone.triggers[" + std::to_string(i) + "]";
            const Json& t = ts[i];
            if (!t.is_object()) return where + " must be an object";
            if (!only_keys(t, {"on", "group", "own_turn_only", "completely_within", "counts_aura_move_as_enter", "while_in_zone",
                               "effect", "region", "loses_action", "auto_success_damage", "auto_success_conditions",
                               "auto_success_no_breath", "shapechanger_disadvantage", "evil_damage_type"}, bad))
                return where + ": unknown key '" + bad + "'";
            ZoneTriggerRow r;
            if (!lookup(kOn, t.value("on", Json()), r.on))
                return where + ".on must be zone_appear, zone_enter, zone_start_turn, zone_end_turn or zone_move_within";
            if (t.contains("group")) {
                if (!small_int(t["group"], 0, 255, v)) return where + ".group invalid (0..255)";
                r.group = uint8_t(v);
            }
            if (!bool_key(t, "own_turn_only", r.own_turn_only) || !bool_key(t, "completely_within", r.completely_within) ||
                !bool_key(t, "counts_aura_move_as_enter", r.counts_aura_move_as_enter) || !bool_key(t, "while_in_zone", r.while_in_zone) ||
                !bool_key(t, "loses_action", r.loses_action) || !bool_key(t, "auto_success_no_breath", r.auto_success_no_breath) ||
                !bool_key(t, "shapechanger_disadvantage", r.shapechanger_disadvantage))
                return where + ": flags must be booleans";
            if (!t.contains("effect")) return where + ".effect missing";
            if (auto m = parse_zone_effect(t["effect"], where, r.effect); !m.empty()) return m;
            if (t.contains("region")) {
                const Json& rg = t["region"];
                if (!rg.is_string() || (rg != "zone" && rg != "zone_or_side")) return where + ".region must be zone or zone_or_side";
                r.region = rg == "zone_or_side" ? 1 : 0;
                if (r.region == 1 && !zs.sided) return where + ": region zone_or_side needs a sided zone";
                if (r.region == 1 && r.on != ZoneTrigger::Appear && r.on != ZoneTrigger::StartTurn && r.on != ZoneTrigger::EndTurn)
                    return where + ": region zone_or_side is only for zone_appear / zone_start_turn / zone_end_turn";
            }
            if (t.contains("auto_success_damage")) {
                const Json& a = t["auto_success_damage"];
                const std::string em = where + ".auto_success_damage must be a non-empty array of unique damage types";
                if (!a.is_array() || a.empty()) return em;
                for (const Json& x : a) {
                    DamageType d{};
                    if (!lookup(kDamage, x, d)) return em;
                    const uint32_t b = bit(d);
                    if (r.auto_success_damage & b) return em;
                    r.auto_success_damage |= b;
                }
            }
            if (t.contains("auto_success_conditions")) {
                const Json& a = t["auto_success_conditions"];
                const std::string em = where + ".auto_success_conditions must be a non-empty array of unique conditions";
                if (!a.is_array() || a.empty()) return em;
                for (const Json& x : a) {
                    Condition c{};
                    if (!lookup(kCondition, x, c) || (r.auto_success_conditions & bit(c))) return em;
                    r.auto_success_conditions |= bit(c);
                }
            }
            if ((r.auto_success_damage || r.auto_success_conditions || r.auto_success_no_breath || r.shapechanger_disadvantage) &&
                r.effect.kind != SpellEffectKind::Save)
                return where + ": auto_success / shapechanger_disadvantage need effect kind save";
            if (t.contains("evil_damage_type")) {
                if (!lookup(kDamage, t["evil_damage_type"], r.evil_damage_type)) return where + ".evil_damage_type invalid";
                if (r.effect.dice.count == 0) return where + ": evil_damage_type needs effect damage";
                r.has_evil_damage_type = true;
            }
            zs.triggers.push_back(r);
        }
    }
    return "";
}

// The cross-field rules of check_spell_zone (4.4), in the validator's order.
std::string check_zone(const Json& j, const SpellDef& s) {
    const ZoneSpec& z = s.zone;
    if (j.contains("area")) return "zone: area not allowed (zone.shape / size_ft give the zone's geometry)";
    if (s.duration == SpellDuration::Instantaneous) return "zone: needs a lasting duration (not instantaneous)";
    if (z.anchor == ZoneAnchor::Creature && s.range_ft != 0) return "zone: anchor creature (an aura on the caster) needs range self";
    if (z.move_cost != 0 && z.anchor != ZoneAnchor::Point) return "zone: move needs anchor point";
    if ((z.flags & kZoneLightObscured) && (z.flags & kZoneHeavyObscured)) return "zone: flags light_obscured and heavy_obscured are exclusive";
    if (z.flags == 0 && z.triggers.empty() && z.aura_modifier.id == 0 && z.aura_immunity == 0) return "zone: needs triggers or flags (it would do nothing)";
    for (std::size_t i = 0; i < z.triggers.size(); ++i) {
        const ZoneTriggerRow& r = z.triggers[i];
        const std::string where = "zone.triggers[" + std::to_string(i) + "]";
        if (r.own_turn_only && r.on != ZoneTrigger::Enter && r.on != ZoneTrigger::MoveWithin)
            return where + ": own_turn_only is only for zone_enter / zone_move_within";
        if (r.counts_aura_move_as_enter && r.on != ZoneTrigger::Enter) return where + ": counts_aura_move_as_enter is only for zone_enter";
        if (r.while_in_zone && !r.effect.applies_condition) return where + ": while_in_zone needs an effect condition";
        if (r.counts_aura_move_as_enter && z.anchor == ZoneAnchor::Point && z.move_cost == 0)
            return where + ": counts_aura_move_as_enter on a zone that never moves";
    }
    return "";
}

} // namespace

std::string parse_predicate(const Json& p, Predicate& out) {
    if (!p.is_object() || !p.contains("kind") || !p["kind"].is_string()) return "bad predicate";
    const std::string k = p["kind"].get<std::string>();
    using PK = PredicateKind;
    out = Predicate{};
    auto types_mask = [&](const char* key, bool creature) -> std::optional<uint32_t> {
        auto it = p.find(key);
        if (it == p.end() || !it->is_array() || it->empty()) return std::nullopt;
        uint32_t m = 0;
        for (const Json& t : *it) {
            if (creature) { CreatureType c; if (!lookup(kCreatureType, t, c)) return std::nullopt; m |= bit(c); }
            else { DamageType d; if (!lookup(kDamage, t, d)) return std::nullopt; m |= bit(d); }
        }
        return m;
    };
    auto geti = [&](const char* key, int def) {
        auto it = p.find(key);
        return (it != p.end() && it->is_number_integer()) ? it->get<int>() : def;
    };
    if (k == "weapon") {
        const std::string is = p.value("is", "");
        out.kind = PK::Weapon;
        if (is == "melee") out.a = int16_t(WeaponIs::Melee);
        else if (is == "ranged") out.a = int16_t(WeaponIs::Ranged);
        else if (is == "finesse") out.a = int16_t(WeaponIs::Finesse);
        else if (is == "finesse_or_ranged") out.a = int16_t(WeaponIs::FinesseOrRanged);
        else return "bad weapon predicate";
    } else if (k == "advantage_or_ally_adjacent") out.kind = PK::AdvantageOrAllyAdjacent;
    else if (k == "target_creature_type" || k == "not_creature_type") {
        auto m = types_mask("creature_types", true);
        if (!m) return "bad creature_types";
        out.kind = k == "target_creature_type" ? PK::TargetCreatureType : PK::NotCreatureType;
        out.mask = *m;
    } else if (k == "damage_type_is" || k == "damage_type_not") {
        auto m = types_mask("types", false);
        if (!m) return "bad damage types";
        out.kind = k == "damage_type_is" ? PK::DamageTypeIs : PK::DamageTypeNot;
        out.mask = *m;
    } else if (k == "not_crit") out.kind = PK::NotCrit;
    else if (k == "moved_at_least_ft") {
        const int ft = geti("ft", 0);
        if (ft < 5 || ft > 32767) return "bad ft";
        out.kind = PK::MovedAtLeastFt;
        out.a = int16_t(ft);
    } else if (k == "self_hp_below") {
        const int n = geti("num", 0), d = geti("den", 0);
        if (n < 1 || d < 1 || n > 32767 || d > 32767) return "bad self_hp_below";
        out.kind = PK::SelfHpBelow;
        out.a = int16_t(n);
        out.b = int16_t(d);
    } else if (k == "save") {   // the owner rolls: a = ability, b = DC (or the base with dc_from_damage), mask bit 0 = dc_from_damage
        Ability ab;
        const bool from_dmg = p.value("dc_from_damage", false);
        if (!lookup(kAbility, p.value("ability", Json()), ab) || p.contains("dc") == from_dmg) return "bad save predicate";
        const int dc = from_dmg ? geti("base", 0) : geti("dc", 0);
        if (dc < 0 || dc > 30) return "bad save DC";
        out.kind = PK::Save;
        out.a = int16_t(int(ab));
        out.b = int16_t(dc);
        out.mask = from_dmg ? 1u : 0u;
    } else return "unknown predicate '" + k + "'";
    return "";
}

std::string parse_modifier(const Json& m, ModifierSpec& out, const Json** table) {
    if (!m.is_object()) return "modifier must be an object";
    static const std::map<std::string, ModStat> kStat{{"attack_roll", ModStat::AttackRoll}, {"save", ModStat::Save},
        {"ability_check", ModStat::AbilityCheck}, {"ac", ModStat::Ac}, {"speed", ModStat::Speed}, {"damage", ModStat::Damage},
        {"temp_hp", ModStat::TempHp}, {"resistance", ModStat::Resistance}, {"attacks_against", ModStat::AttacksAgainst}};
    out = ModifierSpec{};
    if (!lookup(kStat, m.value("stat", Json()), out.stat)) return "modifier stat invalid";
    if (m.contains("bonus")) { int b = 0; if (!small_int(m["bonus"], -32767, 32767, b)) return "modifier bonus invalid"; out.bonus = int16_t(b); }
    if (m.contains("bonus_table")) {
        if (!table) return "modifier bonus_table is not allowed here";
        if (m.contains("bonus")) return "modifier bonus and bonus_table together";
        *table = &m["bonus_table"];
    }
    if (m.contains("die")) {
        if (!m["die"].is_string()) return "modifier die invalid";
        std::string d = m["die"].get<std::string>();
        if (!d.empty() && d[0] == '-') { out.die_mult = -1; d.erase(0, 1); }
        auto pd = parse_dice(d);
        if (!pd) return "modifier die invalid";
        out.die = *pd;
    }
    if (m.contains("mode")) {
        const std::string md = m["mode"].is_string() ? m["mode"].get<std::string>() : "";
        if (md == "advantage") out.mode = RollMode::Advantage;
        else if (md == "disadvantage") out.mode = RollMode::Disadvantage;
        else return "modifier mode invalid";
    }
    if (m.contains("speed_scale")) {
        const Json& sc = m["speed_scale"];
        int n = 1, d = 1;
        if (sc.is_string() && sc.get<std::string>() == "half") { n = 1; d = 2; }
        else if (sc.is_string() && sc.get<std::string>() == "double") { n = 2; d = 1; }
        else if (!sc.is_object() || !sc.contains("num") || !sc.contains("den") || !small_int(sc["num"], 1, 255, n) || !small_int(sc["den"], 1, 255, d))
            return "modifier speed_scale invalid";
        out.speed_num = uint8_t(n);
        out.speed_den = uint8_t(d);
    } else if (out.stat == ModStat::Speed) return "speed modifier needs speed_scale";
    if (m.contains("damage_type")) {
        if (!lookup(kDamage, m["damage_type"], out.damage_type)) return "modifier damage_type invalid";
        out.has_damage_type = true;
    } else if (out.stat == ModStat::Resistance) return "resistance modifier needs damage_type";
    if (m.contains("ability")) {
        if (!lookup(kAbility, m["ability"], out.ability)) return "modifier ability invalid";
        out.has_ability = true;
    }
    static const std::map<std::string, ModAppliesTo> kApplies{{"self", ModAppliesTo::Self}, {"target", ModAppliesTo::Target},
                                                              {"allies_within_30", ModAppliesTo::AlliesWithin30}};
    out.applies_to = ModAppliesTo::Target;
    if (m.contains("applies_to") && !lookup(kApplies, m["applies_to"], out.applies_to)) return "modifier applies_to invalid";
    const std::string st = m.value("stacks", "replace");
    if (st != "replace" && st != "stack") return "modifier stacks invalid";
    out.stack = st == "stack";
    if (!m.contains("duration") || !m["duration"].is_object()) return "modifier duration required";
    const Json& du = m["duration"];
    const std::string dk = du.value("kind", "");
    if (dk == "rounds") {
        int n = 0;
        if (!du.contains("n") || !small_int(du["n"], 1, 65535, n)) return "modifier duration n invalid";
        out.expiry = 1;
        out.rounds = uint16_t(n);
    } else if (dk == "until_start_of_caster_turn") out.until_caster_turn = true;
    else if (dk == "until_end_of_target_turn") out.expiry = 2;
    else if (dk == "concentration") out.concentration = true;
    else if (dk != "until_rest") return "modifier duration kind invalid";
    if (m.contains("consume")) {   // next_attack | next_attack_against | until_damaged
        const std::string cn = m["consume"].is_string() ? m["consume"].get<std::string>() : "";
        if (cn == "next_attack") out.consume = 1;
        else if (cn == "next_attack_against") out.consume = 2;
        else if (cn == "until_damaged") out.consume = 3;
        else return "modifier consume invalid";
    }
    if (m.contains("speed_sub")) {
        int v = 0;
        if (out.stat != ModStat::Speed || !small_int(m["speed_sub"], 1, 255, v)) return "modifier speed_sub invalid";
        out.speed_sub = uint8_t(v);
    }
    if (m.contains("ac_floor")) {
        int v = 0;
        if (out.stat != ModStat::Ac || !small_int(m["ac_floor"], 1, 40, v)) return "modifier ac_floor invalid";
        out.ac_floor = int16_t(v);
        out.ac_floor_dex = m.value("ac_floor_dex", false);
    } else if (m.contains("ac_floor_dex")) return "modifier ac_floor_dex needs ac_floor";
    if (m.contains("per_turn")) {
        if (out.stat != ModStat::TempHp || !m["per_turn"].is_boolean()) return "modifier per_turn invalid";
        out.per_turn = m["per_turn"].get<bool>();
    }
    if (m.contains("when")) {
        if (!m["when"].is_array()) return "modifier when invalid";
        if (out.stat == ModStat::Save || out.stat == ModStat::AbilityCheck || out.stat == ModStat::Speed ||
            out.stat == ModStat::Resistance || out.stat == ModStat::TempHp)
            return "modifier when is not available on this stat";
        for (const Json& p : m["when"]) {
            Predicate pr;
            if (auto e = parse_predicate(p, pr); !e.empty()) return "modifier when: " + e;
            out.when.push_back(pr);
        }
    }
    const bool has_bonus = m.contains("bonus") || m.contains("bonus_table");
    switch (out.stat) {
        case ModStat::Ac: if (!has_bonus && out.ac_floor == 0) return "modifier needs bonus or ac_floor"; break;
        case ModStat::TempHp: if (!has_bonus) return "modifier needs bonus"; break;
        case ModStat::AttacksAgainst: if (out.mode == RollMode::Normal) return "attacks_against needs mode"; break;
        case ModStat::AttackRoll: case ModStat::Save: case ModStat::AbilityCheck:
            if (!has_bonus && out.die.count == 0 && out.mode == RollMode::Normal) return "modifier needs bonus, die or mode";
            break;
        case ModStat::Damage: if (!has_bonus && out.die.count == 0) return "damage modifier needs bonus, bonus_table or die"; break;
        default: break;
    }
    return "";
}

SpellLoad spell_from_json(const Json& j) {
    // Mirrors schemas/spell/v1.schema.json plus tools/validate check_spell (same rules, same wording).
    if (!j.is_object() || j.value("$schema", "") != "spell@1") return err("not a spell@1 document");
    SpellDef s;
    std::vector<std::pair<std::string, std::string>> refs;
    if (!j.contains("id") || !j["id"].is_string()) return err("id missing");
    s.content_id = j["id"].get<std::string>();
    if (s.content_id.find(".spell.") == std::string::npos) return err("id must be '<ns>.spell.<name>'");
    s.id = spell_id_of(s.content_id);
    int lvl = 0;
    if (!j.contains("level") || !small_int(j["level"], 0, 9, lvl)) return err("level must be 0..9");
    s.level = uint8_t(lvl);
    s.ritual = j.value("ritual", false);
    s.evocation = j.value("school", "") == "evocation";   // R-G14 Sculpt Spells (rules #109 hand-off a); spell@1 requires school

    const Json ct = j.value("casting_time", Json::object());
    const std::string ck = ct.value("kind", "");
    const bool has_trigger = ct.contains("trigger");
    if (ck == "action") s.casting_time = CastingTime::Action;
    else if (ck == "bonus_action") s.casting_time = CastingTime::BonusAction;
    else if (ck == "reaction") s.casting_time = CastingTime::Reaction;
    else if (ck == "minute" || ck == "hour") {
        s.casting_time = ck == "minute" ? CastingTime::Minute : CastingTime::Hour;
        int a = 0;
        if (!ct.contains("amount") || !small_int(ct["amount"], 1, 255, a)) return err("casting_time.amount required for minute/hour");
        s.casting_amount = uint8_t(a);
    } else return err("casting_time.kind invalid");
    if ((s.casting_time == CastingTime::Reaction) != has_trigger) return err("casting_time.trigger is required for (and only for) reactions");
    if (has_trigger) {
        const Json& tr = ct["trigger"];
        const std::string tk = tr.value("kind", "");
        if (tk == "being_hit") s.reaction_trigger = ReactionTrigger::BeingHit;
        else if (tk == "spell_cast") s.reaction_trigger = ReactionTrigger::SpellCast;
        else return err("casting_time.trigger.kind must be being_hit or spell_cast");
        if (tr.contains("or_targeted_by")) {
            if (tk != "being_hit") return err("casting_time.trigger.or_targeted_by is only for being_hit");
            for (const Json& r : tr["or_targeted_by"]) {
                if (!r.is_string() || r.get<std::string>().find(".spell.") == std::string::npos)
                    return err("casting_time.trigger.or_targeted_by entries must be spell ids");
                s.or_targeted_by.push_back(spell_id_of(r.get<std::string>()));
                refs.emplace_back("casting_time.trigger.or_targeted_by", r.get<std::string>());
            }
        }
        s.requires_sight = tr.value("requires_sight", false);
    }

    // range: self = 0 ft, touch = 5 ft (adjacent; sim checks reach), ranged = feet (>= 5).
    const Json rg = j.value("range", Json::object());
    const std::string rk = rg.value("kind", "");
    int ft = 0;
    if (rk == "self" && !rg.contains("feet")) s.range_ft = 0;
    else if (rk == "touch" && !rg.contains("feet")) s.range_ft = 5;
    else if (rk == "ranged" && rg.contains("feet") && small_int(rg["feet"], 5, 65535, ft) && ft % 5 == 0) s.range_ft = uint16_t(ft);
    else return err("range invalid (ranged needs feet >= 5, a multiple of 5)");

    if (j.contains("area")) {
        const Json& a = j["area"];
        static const std::map<std::string, AreaShape> kShape{{"cone", AreaShape::Cone}, {"cube", AreaShape::Cube},
            {"cylinder", AreaShape::Cylinder}, {"line", AreaShape::Line}, {"sphere", AreaShape::Sphere}};
        if (!lookup(kShape, a.value("shape", Json()), s.area)) return err("area.shape invalid");
        int v = 0;
        if (!a.contains("size_ft") || !small_int(a["size_ft"], 5, 65535, v)) return err("area.size_ft invalid");
        s.area_size_ft = uint16_t(v);
        if ((s.area == AreaShape::Line) != a.contains("width_ft")) return err("area.width_ft is required for (and only for) lines");
        if ((s.area == AreaShape::Cylinder) != a.contains("height_ft")) return err("area.height_ft is required for (and only for) cylinders");
        if (a.contains("width_ft")) { if (!small_int(a["width_ft"], 5, 65535, v)) return err("area.width_ft invalid"); s.area_width_ft = uint16_t(v); }
        if (a.contains("height_ft")) { if (!small_int(a["height_ft"], 5, 65535, v)) return err("area.height_ft invalid"); s.area_height_ft = uint16_t(v); }
    }

    const Json c = j.value("components", Json::object());
    if (c.value("v", false)) s.components |= kComponentV;
    if (c.value("s", false)) s.components |= kComponentS;
    if (c.contains("m")) {
        const Json& m = c["m"];
        if (!m.is_object() || !m.contains("text")) return err("components.m needs text");
        s.components |= kComponentM;
        int cost = 0;
        if (m.contains("cost_cp")) { if (!small_int(m["cost_cp"], 1, 2147483647, cost)) return err("components.m.cost_cp invalid"); s.material_cost_cp = uint32_t(cost); }
        s.material_consumed = m.value("consumed", false);
        if (m.contains("item")) {
            if (!m["item"].is_string() || m["item"].get<std::string>().empty()) return err("components.m.item invalid");
            s.material_item = m["item"].get<std::string>();
        }
        if (m.contains("consumed") && !m.contains("cost_cp")) return err("'cost_cp' is a dependency of 'consumed'");
    }
    if (s.components == 0) return err("components: at least one of v, s, m");

    const Json du = j.value("duration", Json::object());
    static const std::map<std::string, SpellDuration> kDur{{"instantaneous", SpellDuration::Instantaneous},
        {"rounds", SpellDuration::Rounds}, {"minutes", SpellDuration::Minutes}, {"hours", SpellDuration::Hours},
        {"until_start_of_caster_turn", SpellDuration::UntilStartOfCasterTurn},
        {"until_dispelled", SpellDuration::UntilDispelled}, {"special", SpellDuration::Special}};
    if (!lookup(kDur, du.value("kind", Json()), s.duration)) return err("duration.kind invalid");
    const bool timed = s.duration == SpellDuration::Rounds || s.duration == SpellDuration::Minutes || s.duration == SpellDuration::Hours;
    int amt = 0;
    if (timed != du.contains("amount")) return err("duration.amount is required for (and only for) rounds/minutes/hours");
    if (timed) { if (!small_int(du["amount"], 1, 65535, amt)) return err("duration.amount invalid"); s.duration_amount = uint16_t(amt); }
    s.concentration = du.value("concentration", false);
    if (du.contains("concentration") && (s.duration == SpellDuration::Instantaneous || s.duration == SpellDuration::UntilStartOfCasterTurn))
        return err("duration.concentration needs a lasting duration");

    // targets: omitted = one creature (Save/Heal), or per dart/beam, or self.
    const bool has_targets = j.contains("targets");
    if (has_targets) {
        const Json& t = j["targets"];
        int n = 0;
        if (!t.contains("count") || !small_int(t["count"], 1, 255, n)) return err("targets.count invalid");
        s.targets = uint8_t(n);
        if (t.contains("creature_type")) {
            for (const Json& ty : t["creature_type"]) {
                CreatureType ct2;
                if (!lookup(kCreatureType, ty, ct2)) return err("targets.creature_type invalid");
                s.target_types |= bit(ct2);
            }
        }
        if (t.contains("max_spread_ft")) {
            int v = 0;
            if (!small_int(t["max_spread_ft"], 5, 65535, v)) return err("targets.max_spread_ft invalid");
            s.max_spread_ft = uint16_t(v);
        }
    }

    if (!j.contains("effects") || !j["effects"].is_array() || j["effects"].empty()) return err("effects missing");
    // The spellcasting ability check of counter and dispel: {ability: spellcasting, dc_base, plus: spell_level}, required,
    // dc_base 1..99 (DC = dc_base + the target spell's level). False on any other shape.
    auto read_spell_check = [](const Json& fx, uint8_t& dc_base_out) -> bool {
        const Json chk = fx.value("check", Json::object());
        int base = 0;
        if (chk.value("ability", "") != "spellcasting" || chk.value("plus", "") != "spell_level" ||
            !chk.contains("dc_base") || !small_int(chk["dc_base"], 1, 99, base))
            return false;
        dc_base_out = uint8_t(base);
        return true;
    };
    // One effect object -> the per-effect fields of `t` (kind, damage, save, condition, ...). Returns "" or an error.
    auto parse_effect = [&](const Json& e, SpellDef& s, std::string& kind) -> std::string {
        kind = e.is_object() ? e.value("kind", "") : "";
        if (kind == "spell_attack") {
            s.kind = SpellEffectKind::SpellAttack;
            const std::string atk = e.value("attack", "");
            if (atk != "melee" && atk != "ranged") return ("effect attack: '" + atk + "' is not one of ['melee', 'ranged']");
            s.melee = atk == "melee";
            if (!e.contains("damage") || !read_damage(e["damage"], s)) return ("effect damage invalid");
            int b = 1;
            if (e.contains("beams") && !small_int(e["beams"], 1, 255, b)) return ("effect beams invalid");
            s.beams = uint8_t(b);
        } else if (kind == "save") {
            s.kind = SpellEffectKind::Save;
            if (!e.contains("ability") || !lookup(kAbility, e["ability"], s.save_ability)) return ("effect ability invalid");
            if (e.contains("damage") != e.contains("on_success"))
                return (e.contains("damage") ? "'on_success' is a dependency of 'damage'" : "effect on_success needs damage ('damage' is a dependency of 'on_success')");
            if (e.contains("damage") && !read_damage(e["damage"], s)) return ("effect damage invalid");
            if (e.contains("on_success")) {
                const std::string os = e.value("on_success", "");
                if (os != "half" && os != "none") return ("effect on_success must be half or none");
                s.on_save = os == "half" ? OnSave::Half : OnSave::None;
            }
            if (e.contains("condition")) {
                if (!lookup(kCondition, e["condition"], s.condition)) return ("effect condition invalid");
                s.applies_condition = true;
            }
            if (e.contains("repeat_save")) {
                if (!s.applies_condition) return ("effect repeat_save needs condition ('condition' is a dependency of 'repeat_save')");
                static const std::map<std::string, RepeatSave> kRep{{"start_of_turn", RepeatSave::StartOfTurn},
                    {"end_of_turn", RepeatSave::EndOfTurn}, {"on_damage", RepeatSave::OnDamage}};
                if (!lookup(kRep, e["repeat_save"], s.repeat_save)) return ("effect repeat_save invalid");
            }
            if (!e.contains("damage") && !s.applies_condition) return ("save effect needs damage or condition");
        } else if (kind == "heal") {
            s.kind = SpellEffectKind::Heal;
            auto d = e.contains("dice") && e["dice"].is_string() ? parse_dice(e["dice"].get<std::string>()) : std::nullopt;
            if (!d) return ("effect dice invalid");
            s.dice = *d;
            s.add_spellcasting_modifier = e.value("add_spellcasting_modifier", false);
            if (e.contains("excludes_creature_type")) {
                for (const Json& ty : e["excludes_creature_type"]) {
                    CreatureType ct2;
                    if (!lookup(kCreatureType, ty, ct2)) return ("effect excludes_creature_type invalid");
                    s.excluded_types |= bit(ct2);
                }
            }
        } else if (kind == "auto_hit") {
            s.kind = SpellEffectKind::AutoHit;
            int darts = 0;
            if (!e.contains("darts") || !small_int(e["darts"], 1, 255, darts)) return ("effect darts invalid");
            s.darts = uint8_t(darts);
            if (!e.contains("damage") || !read_damage(e["damage"], s)) return ("effect damage invalid");
            static const std::map<std::string, DartDistribution> kDist{{"any", DartDistribution::Any},
                {"single", DartDistribution::Single}, {"distinct", DartDistribution::Distinct}};
            if (!lookup(kDist, e.value("distribution", Json()), s.distribution)) return ("effect distribution must be any, single or distinct");
            if (!e.contains("simultaneous") || !e["simultaneous"].is_boolean()) return ("effect simultaneous required");
            s.simultaneous = e["simultaneous"].get<bool>();
            s.targets = 0;   // per dart (distribution), not a creature count
        } else if (kind == "ac_bonus") {
            s.kind = SpellEffectKind::AcBonus;
            int b = 0;
            if (!e.contains("bonus") || !small_int(e["bonus"], 1, 20, b)) return ("effect bonus invalid");
            s.ac_bonus = int8_t(b);
            if (e.contains("blocks_spell")) {
                if (!e["blocks_spell"].is_string() || e["blocks_spell"].get<std::string>().find(".spell.") == std::string::npos)
                    return ("effect blocks_spell must be a spell id");
                s.blocks_spell = spell_id_of(e["blocks_spell"].get<std::string>());
                refs.emplace_back("effect blocks_spell", e["blocks_spell"].get<std::string>());
            }
        } else if (kind == "counter") {
            s.kind = SpellEffectKind::Counter;
            if (e.value("auto_max_level", "") != "slot") return ("effect auto_max_level must be slot");
            if (!read_spell_check(e, s.counter_dc_base)) return ("effect check must be {ability: spellcasting, dc_base, plus: spell_level}");
        } else if (kind == "apply_modifier") {   // row 12: a timed modifier; primary effect only, no bonus_table (no class level)
            s.kind = SpellEffectKind::ApplyModifier;
            if (!e.contains("modifier")) return ("effect modifier required");
            if (auto m = parse_modifier(e["modifier"], s.modifier); !m.empty()) return ("effect " + m);
            s.modifier.id = spell_id_of(s.content_id + "#modifier");
        } else if (kind == "apply_condition") {
            s.kind = SpellEffectKind::ApplyCondition;
            if (!e.contains("condition") || !lookup(kCondition, e["condition"], s.condition)) return ("effect condition invalid");
            s.applies_condition = true;
        } else if (kind == "hp_pool") {   // R-G15 (VERSION_BUMP_BATCH 7.2f): Sleep, Color Spray; timer from the spell's duration
            s.kind = SpellEffectKind::HpPool;
            auto d = e.contains("dice") && e["dice"].is_string() ? parse_dice(e["dice"].get<std::string>()) : std::nullopt;
            if (!d || d->count == 0) return ("effect dice invalid");
            s.dice = *d;
            if (!e.contains("condition") || !lookup(kCondition, e["condition"], s.condition)) return ("effect condition invalid");
            if (e.contains("ends_on_damage")) {
                if (!e["ends_on_damage"].is_boolean()) return ("effect ends_on_damage must be a boolean");
                s.ends_on_damage = e["ends_on_damage"].get<bool>();
            }
            if (e.contains("excludes_creature_type")) {   // same key and meaning as heal's (SpellDef::excluded_types)
                if (!e["excludes_creature_type"].is_array() || e["excludes_creature_type"].empty()) return ("effect excludes_creature_type invalid");
                for (const Json& ty : e["excludes_creature_type"]) {
                    CreatureType ct2;
                    if (!lookup(kCreatureType, ty, ct2)) return ("effect excludes_creature_type invalid");
                    s.excluded_types |= bit(ct2);
                }
            }
        } else if (kind == "forced_move") {   // R-G9 (7.2k): push / pull the targets (Thunderwave, Repelling Blast)
            s.kind = SpellEffectKind::ForcedMove;
            const std::string mode = e.value("mode", "push");
            if (mode == "push") s.forced.mode = 1;
            else if (mode == "pull") s.forced.mode = 2;
            else return ("effect mode must be push or pull");   // line (Gust of Wind) is data only in R-G9
            int ft = 0;
            if (!e.contains("feet") || !feet_5(e["feet"], ft)) return ("effect feet must be a positive multiple of 5");
            s.forced.feet = uint16_t(ft);
            s.forced.up_to = e.value("up_to", false);
            if (e.contains("max_size")) {
                static const std::map<std::string, Size> kSizes = {{"tiny", Size::Tiny}, {"small", Size::Small}, {"medium", Size::Medium},
                                                                   {"large", Size::Large}, {"huge", Size::Huge}, {"gargantuan", Size::Gargantuan}};
                Size sz;
                if (!lookup(kSizes, e["max_size"], sz)) return ("effect max_size invalid");
                s.forced.max_size = int8_t(sz);
            }
            if (e.value("provokes_oa", false)) return ("effect provokes_oa: true is not supported (R-G9: forced moves never provoke)");
        } else if (kind == "teleport") {   // R-G9 (7.2k): self-teleport (Misty Step); the destination is the caller's (sim)
            s.kind = SpellEffectKind::Teleport;
            int ft = 0;
            if (!e.contains("max_feet") || !feet_5(e["max_feet"], ft)) return ("effect max_feet must be a positive multiple of 5");
            s.teleport.max_feet = uint16_t(ft);
            s.teleport.needs_sight = e.value("needs_sight", true);
        } else if (kind == "dispel") {   // R-G15 (7.2f): Dispel Magic; spells above the slot end on the check (DC dc_base + level)
            s.kind = SpellEffectKind::Dispel;
            if (!read_spell_check(e, s.dispel_dc_base)) return ("effect check must be {ability: spellcasting, dc_base, plus: spell_level}");
        } else if (kind == "revive") {   // R-G15 (7.2f): Revivify; dead for at most within_rounds rounds
            s.kind = SpellEffectKind::Revive;
            if (e.contains("within_rounds")) {
                int n = 0;
                if (!small_int(e["within_rounds"], 1, 255, n)) return ("effect within_rounds invalid");
                s.revive_within_rounds = uint8_t(n);
            }
        } else if (kind == "end_condition") {   // R-G15 (7.2g): Lesser Restoration; ends the lowest listed condition present
            s.kind = SpellEffectKind::EndCondition;
            if (!e.contains("conditions") || !e["conditions"].is_array() || e["conditions"].empty()) return ("effect conditions invalid");
            for (const Json& cj : e["conditions"]) {
                Condition cd;
                if (!lookup(kCondition, cj, cd)) return ("effect conditions invalid");
                s.ends_conditions |= bit(cd);
                if (std::find(s.ends_condition_list.begin(), s.ends_condition_list.end(), cd) == s.ends_condition_list.end())
                    s.ends_condition_list.push_back(cd);   // JSON order: the cast-time condition_choice indexes it
            }
        } else if (kind == "delayed_damage" || kind == "sustained_damage") {   // R-G15 (7.2g): Acid Arrow's later hit, Heat Metal
            const bool delayed = kind == "delayed_damage";
            s.kind = delayed ? SpellEffectKind::DelayedDamage : SpellEffectKind::SustainedDamage;
            if (!e.contains("damage") || !read_damage(e["damage"], s)) return ("effect damage invalid");
            if (delayed) {   // its own timer: {kind: rounds, n} or {kind: until_end_of_target_next_turn} (the common@2 duration words)
                const Json ex = e.value("expiry", Json());
                const std::string xk = ex.is_object() ? ex.value("kind", "") : "";
                if (xk == "until_end_of_target_next_turn") s.expiry = 2;   // rules5e::Expiry::EndOfTargetNextTurn
                else if (xk == "rounds") {
                    int n = 0;
                    if (!ex.contains("n") || !small_int(ex["n"], 1, 65535, n)) return ("effect expiry n invalid");
                    s.expiry = 1;   // rules5e::Expiry::Rounds
                    s.duration_rounds = uint16_t(n);
                } else return ("effect expiry must be {kind: rounds, n} or {kind: until_end_of_target_next_turn}");
            } else if (e.contains("expiry")) return ("effect expiry is only for delayed_damage (sustained_damage lasts the spell's duration)");
        } else if (kind == "zone") {   // R-G11 (CONTENT_CONVENTIONS.md section 4): a persistent zone / aura
            s.kind = SpellEffectKind::Zone;
            std::string bad;
            if (!only_keys(e, {"kind", "zone", "description"}, bad)) return ("zone effect: unknown key '" + bad + "'");
            if (!e.contains("zone")) return ("zone missing (kind zone needs a zone object)");
            if (auto m = parse_zone(e["zone"], s); !m.empty()) return m;
        } else return ("effect kind invalid");
        return "";
    };
    if (!j["effects"].is_array()) return err("effects missing");
    std::string kind;
    if (auto m = parse_effect(j["effects"][0], s, kind); !m.empty()) return err(m);
    // Effects 2..n: parsed into a scratch SpellDef, then copied into SpellEffect. Reaction kinds stand alone.
    const std::size_t n_effects = j["effects"].size();
    for (std::size_t i = 1; i < n_effects; ++i) {
        const Json& xe = j["effects"][i];
        if (xe.is_object() && xe.value("kind", "") == "zone")   // the cast refuses an extra effect of kind Zone
            return err("zone must be the primary effect (effects[" + std::to_string(i) + "] is a zone)");
        SpellDef x;
        std::string xk;
        if (auto m = parse_effect(j["effects"][i], x, xk); !m.empty()) return err("effects[" + std::to_string(i) + "]: " + m);
        if (x.kind == SpellEffectKind::AcBonus || x.kind == SpellEffectKind::Counter)
            return err("effects: ac_bonus and counter reactions cannot be combined with other effects");
        // R-G15: of the new kinds only delayed_damage rides another effect (Acid Arrow); the others read spell-level fields
        // (pool, dispel DC, revive window, condition mask, sustain) that an extra effect does not carry.
        if (x.kind == SpellEffectKind::HpPool || x.kind == SpellEffectKind::Dispel || x.kind == SpellEffectKind::Revive ||
            x.kind == SpellEffectKind::EndCondition || x.kind == SpellEffectKind::SustainedDamage || x.kind == SpellEffectKind::Teleport)
            return err("effects[" + std::to_string(i) + "]: " + xk + " is only allowed as the first effect");
        SpellEffect fx;
        fx.kind = x.kind; fx.melee = x.melee; fx.beams = x.beams; fx.dice = x.dice; fx.damage_type = x.damage_type;
        fx.add_spellcasting_modifier = x.add_spellcasting_modifier; fx.excluded_types = x.excluded_types;
        fx.save_ability = x.save_ability; fx.on_save = x.on_save; fx.applies_condition = x.applies_condition;
        fx.condition = x.condition; fx.repeat_save = x.repeat_save; fx.darts = x.darts;
        fx.distribution = x.distribution; fx.simultaneous = x.simultaneous;
        fx.forced = x.forced; fx.teleport = x.teleport;   // R-G9
        fx.expiry = x.expiry; fx.duration_rounds = x.duration_rounds;   // delayed_damage's timer (0 for every other spell@1 kind)
        if (x.kind == SpellEffectKind::ApplyModifier) {   // rider (Guiding Bolt): its own modifier id
            fx.modifier = x.modifier;
            fx.modifier.id = spell_id_of(s.content_id + "#modifier" + std::to_string(i));
        }
        fx.affected_only = j["effects"][i].value("applies_to", "all") == "affected";
        s.extra_effects.push_back(fx);
    }
    if (n_effects > 1 && (s.kind == SpellEffectKind::AcBonus || s.kind == SpellEffectKind::Counter))
        return err("effects: ac_bonus and counter reactions cannot be combined with other effects");
    if (j["effects"][0].value("applies_to", "all") != "all")
        return err("effects[0].applies_to needs a previous effect");
    const bool reaction_kind = s.kind == SpellEffectKind::AcBonus || s.kind == SpellEffectKind::Counter;
    if (reaction_kind != (s.casting_time == CastingTime::Reaction))
        return err("effect kind " + kind + " and casting_time.kind " + ck + " disagree (ac_bonus/counter are reactions, and only they)");
    if (reaction_kind && s.reaction_trigger != (s.kind == SpellEffectKind::AcBonus ? ReactionTrigger::BeingHit : ReactionTrigger::SpellCast))
        return err("effect kind " + kind + " needs casting_time.trigger.kind " + (s.kind == SpellEffectKind::AcBonus ? "being_hit" : "spell_cast"));
    if ((s.kind == SpellEffectKind::AcBonus) != (s.duration == SpellDuration::UntilStartOfCasterTurn))
        return err("ac_bonus effects last until_start_of_caster_turn (and only they use it)");
    if (has_targets && s.kind != SpellEffectKind::Save && s.kind != SpellEffectKind::Heal && s.kind != SpellEffectKind::ApplyCondition)
        return err("targets not allowed on a " + kind + " effect (one target, per dart/beam, or self)");
    // R-G15: an HP pool's condition and a sustained effect last the spell's duration (Sleep 1 minute = 10 rounds; Color Spray
    // 1 round, the proposal's approximation of "until the end of your next turn"; Heat Metal 1 minute, and also ends with
    // concentration).
    if (s.kind == SpellEffectKind::Zone) {   // R-G11: cross-field rules; the spell's duration is the zone's lifetime
        if (auto m = check_zone(j, s); !m.empty()) return err(m);
        int rounds = 0;
        if (s.duration == SpellDuration::Rounds) rounds = s.duration_amount;
        else if (s.duration == SpellDuration::Minutes) rounds = s.duration_amount * 10;
        else if (s.duration == SpellDuration::Hours) rounds = s.duration_amount * 600;
        if (rounds > 0) { s.expiry = 1; s.duration_rounds = uint16_t(std::min(rounds, 65535)); }   // rules5e::Expiry::Rounds
    }
    if (s.kind == SpellEffectKind::HpPool || s.kind == SpellEffectKind::SustainedDamage) {
        int rounds = 0;
        if (s.duration == SpellDuration::Rounds) rounds = s.duration_amount;
        else if (s.duration == SpellDuration::Minutes) rounds = s.duration_amount * 10;
        else if (s.duration == SpellDuration::Hours) rounds = s.duration_amount * 600;
        else return err(kind + " needs a rounds, minutes or hours duration");
        s.expiry = 1;   // rules5e::Expiry::Rounds
        s.duration_rounds = uint16_t(std::min(rounds, 65535));
    }

    const Json sc = j.value("scaling", Json::object());
    if (sc.contains("cantrip")) {
        if (s.level != 0) return err("scaling.cantrip is only for cantrips (level 0)");
        const Json lv = sc["cantrip"].value("levels", Json::array());
        if (!lv.is_array() || lv.empty() || lv.size() > 3) return err("scaling.cantrip.levels invalid");
        s.cantrip_tiers = {0, 0, 0};
        for (std::size_t i = 0; i < lv.size(); ++i) {
            int t = 0;
            if (!small_int(lv[i], 2, 20, t)) return err("scaling.cantrip.levels invalid");
            if (i > 0 && t <= s.cantrip_tiers[i - 1]) return err("scaling.cantrip.levels must be ascending");
            s.cantrip_tiers[i] = uint8_t(t);
        }
        const std::string adds = sc["cantrip"].value("adds", "");
        if (adds != "dice" && adds != "beams") return err("scaling.cantrip.adds must be dice or beams");
        if (adds == "beams" && s.kind != SpellEffectKind::SpellAttack) return err("scaling.cantrip.adds beams needs a spell_attack effect");
        s.cantrip_adds_beams = adds == "beams";
        s.cantrip_scales = true;
    }
    if (sc.contains("upcast")) {
        if (s.level == 0) return err("scaling.upcast is only for leveled spells");
        const Json& u = sc["upcast"];
        auto allowed = [&](const char* key) {
            const std::string k = key;
            if (k == "dice") return s.kind == SpellEffectKind::SpellAttack || s.kind == SpellEffectKind::Save ||
                                    s.kind == SpellEffectKind::Heal || s.kind == SpellEffectKind::AutoHit ||
                                    s.kind == SpellEffectKind::HpPool || s.kind == SpellEffectKind::DelayedDamage ||
                                    s.kind == SpellEffectKind::SustainedDamage || s.kind == SpellEffectKind::Zone;
            if (k == "darts") return s.kind == SpellEffectKind::AutoHit;
            if (k == "beams") return s.kind == SpellEffectKind::SpellAttack;
            return s.kind == SpellEffectKind::Save || s.kind == SpellEffectKind::Heal || s.kind == SpellEffectKind::ApplyCondition;   // targets
        };
        for (const char* key : {"beams", "darts", "dice", "targets"}) {   // sorted, like the validator
            if (!u.contains(key)) continue;
            if (!allowed(key)) return err(std::string("scaling.upcast.") + key + " not allowed on a " + kind + " effect");
            int v = 0;
            if (!small_int(u[key], 1, 255, v)) return err(std::string("scaling.upcast.") + key + " invalid");
            const std::string k = key;
            if (k == "dice") {
                const bool zone_dice = s.kind == SpellEffectKind::Zone &&   // R-G11: a trigger row with damage
                    std::any_of(s.zone.triggers.begin(), s.zone.triggers.end(), [](const ZoneTriggerRow& r) { return r.effect.dice.count > 0; });
                if (s.dice.count == 0 && !zone_dice) return err("scaling.upcast.dice needs effect dice or damage");
                s.upcast_dice = uint8_t(v);
            } else if (k == "darts") s.upcast_darts = uint8_t(v);
            else if (k == "beams") s.upcast_beams = uint8_t(v);
            else s.upcast_targets = uint8_t(v);
        }
    }
    // #120 follow-up: Acid Arrow's later damage also gains scaling.upcast.dice per slot level (SRD: both increase by 1d4).
    for (auto& x : s.extra_effects)
        if (x.kind == SpellEffectKind::DelayedDamage && x.dice.count > 0) x.upcast_dice = s.upcast_dice;
    return {std::move(s), {}, std::move(refs)};
}

std::string collision_error(const SpellBook& book, const SpellDef& d) {
    const SpellDef* other = book.find(d.id);
    char hex[11];
    std::snprintf(hex, sizeof hex, "0x%08x", unsigned(d.id));
    return "spell id collision: '" + d.content_id + "' and '" + (other ? other->content_id : std::string("?")) +
           "' both hash to " + hex + " (rename one)";
}

bool add_spell(SpellBook& book, SpellDef d, std::string& error) {
    if (const SpellDef* other = book.find(d.id)) {
        error = other->content_id == d.content_id ? "duplicate spell id '" + d.content_id + "'" : collision_error(book, d);
        return false;
    }
    book.add(std::move(d));
    return true;
}

SpellBookLoad load_spellbook(const std::filesystem::path& dir) {
    SpellBookLoad out;
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
    if (ec) { out.errors.push_back(dir.string() + ": " + ec.message()); return out; }
    std::sort(files.begin(), files.end());
    eng::assets::SchemaRegistry reg;
    eng::assets::register_content_schemas(reg);
    auto book = std::make_shared<SpellBook>();
    std::vector<std::pair<std::filesystem::path, std::pair<std::string, std::string>>> refs;
    for (const auto& f : files) {
        auto doc = reg.load_file(f);   // same pipeline as packs: $schema tag, floats rejected, migrations
        if (!doc.ok) {
            if (doc.declared.type == "spell") out.errors.push_back(f.string() + ": " + doc.error);
            continue;
        }
        if (doc.current.type != "spell") continue;
        auto r = spell_from_json(doc.data);
        if (!r.def) { out.errors.push_back(f.string() + ": " + r.error); continue; }
        for (auto& ref : r.refs) refs.emplace_back(f, std::move(ref));
        std::string e;
        if (!add_spell(*book, std::move(*r.def), e)) out.errors.push_back(f.string() + ": " + e);
    }
    // References must resolve to a loaded spell (same rule as the validator).
    for (const auto& [f, ref] : refs) {
        const SpellDef* d = book->find(spell_id_of(ref.second));
        if (!d || d->content_id != ref.second)
            out.errors.push_back(f.string() + ": " + ref.first + " '" + ref.second + "' is not a spell@1 in this pack or its depends");
    }
    if (out.errors.empty()) out.book = std::move(book);   // any error (including a collision) is fatal
    return out;
}

} // namespace rules5e_content
