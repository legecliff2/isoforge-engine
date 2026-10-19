#include <bit>
#include "sim/content.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

#include "rules5e/encounter.hpp"
#include "rules5e/weapon.hpp"
#include "assets/content_migrations.hpp"
#include "rules5e_content/spell_loader.hpp"

namespace eng::sim {

using assets::Json;

std::optional<rules5e::DiceExpr> parse_dice(std::string_view s) {
    auto num = [](std::string_view v, int& out) {
        auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
        return ec == std::errc{} && p == v.data() + v.size() && !v.empty();
    };
    const auto d = s.find('d');
    if (d == std::string_view::npos) return std::nullopt;
    const auto sign = s.find_first_of("+-", d);
    int count = 0, sides = 0, bonus = 0;
    if (!num(s.substr(0, d), count)) return std::nullopt;
    if (!num(s.substr(d + 1, sign == std::string_view::npos ? std::string_view::npos : sign - d - 1), sides)) return std::nullopt;
    if (sign != std::string_view::npos) {
        if (!num(s.substr(sign + 1), bonus)) return std::nullopt;
        if (s[sign] == '-') bonus = -bonus;
    }
    if (count < 1 || count > 255 || sides < 1 || sides > 255) return std::nullopt;
    return rules5e::DiceExpr{uint8_t(count), uint8_t(sides), int16_t(bonus)};
}

namespace {
std::optional<rules5e::DamageType> damage_type(const std::string& s) {
    static const char* names[] = {"acid", "bludgeoning", "cold", "fire", "force", "lightning", "necrotic",
                                  "piercing", "poison", "psychic", "radiant", "slashing", "thunder"};
    for (int i = 0; i < 13; ++i)
        if (s == names[i]) return rules5e::DamageType(i);
    return std::nullopt;
}
std::optional<rules5e::Condition> condition_of(const std::string& s) {
    static const char* names[] = {"blinded", "charmed", "deafened", "frightened", "grappled", "incapacitated",
                                  "invisible", "paralyzed", "petrified", "poisoned", "prone", "restrained",
                                  "stunned", "unconscious", "exhaustion"};
    for (int i = 0; i < 15; ++i)
        if (s == names[i]) return rules5e::Condition(i);
    return std::nullopt;
}
std::optional<rules5e::CreatureType> creature_type_of(const std::string& s) {
    static const char* names[] = {"aberration", "beast", "celestial", "construct", "dragon", "elemental", "fey",
                                  "fiend", "giant", "humanoid", "monstrosity", "ooze", "plant", "undead"};
    for (int i = 0; i < 14; ++i)
        if (s == names[i]) return rules5e::CreatureType(i);
    return std::nullopt;
}
int geti(const Json& j, const char* k, int def = 0) {
    auto it = j.find(k);
    return (it != j.end() && it->is_number_integer()) ? it->get<int>() : def;
}

std::optional<rules5e::Ability> ability_of(const std::string& s) {
    static const char* names[] = {"str", "dex", "con", "int", "wis", "cha"};
    for (int i = 0; i < 6; ++i) if (s == names[i]) return rules5e::Ability(i);
    return std::nullopt;
}

// Compiles a monster@4 / character@4 `save_effect` action into a synthetic SpellDef (VERSION_BUMP_BATCH 2.3).
// Returns an error message (load error), or nullopt. `unsupported` is set (and nothing compiled) for shapes
// the engine cannot run yet (pool usage: needs resource pools, row 13).
// ---- Triggers (row 9a sim side): on_event effects and on_hit[] riders -> rules5e::Trigger ----
// Owner level for `scale.per_levels`: the named class's level (character `classes[]`), else the character `level`, else 1
// (monsters have no level: no scaling). Extra dice = (level - 1) / every, so "every 2, add 1d6" on a level-1 base of 1d6 gives
// 2d6 at level 3 and 3d6 at level 5 (Sneak Attack). The added dice must have the base's sides.
int owner_level(const Json& doc, const Json& per_levels) {
    if (per_levels.contains("class") && per_levels["class"].is_string())
        if (auto cl = doc.find("classes"); cl != doc.end() && cl->is_array())
            for (const Json& c : *cl)
                if (c.is_object() && c.value("class", "") == per_levels["class"].get<std::string>()) return std::max(1, geti(c, "level", 1));
    return std::max(1, geti(doc, "level", 1));
}

std::optional<std::string> compile_save_action(const std::string& arch_id, const Json& act, rules5e::SpellDef& d,
                                               Archetype::SaveAction& sa, const Json& doc, bool& unsupported);

// What a rider needs beyond its own JSON: the owner's attacks (bonus_attack resolves attack_id) and the list the
// synthetic Save SpellDefs of area_save riders go to (registered in the SpellBook by ContentLibrary::add).
struct RiderCtx {
    const std::string& arch;
    const std::vector<const Json*>& attacks;
    std::vector<rules5e::SpellDef>& synth;
};

std::optional<std::string> compile_rider(const std::string& where, const Json& r, const Json& doc, const RiderCtx& rc,
                                         std::vector<rules5e::RiderEffect>& out) {
    if (!r.is_object() || !r.contains("kind") || !r["kind"].is_string()) return where + ": bad rider";
    const std::string k = r["kind"].get<std::string>();
    using RK = rules5e::RiderKind;
    rules5e::RiderEffect e;
    if (k == "extra_damage") {
        auto dm = r.find("damage");
        if (dm == r.end() || !dm->is_array() || dm->empty()) return where + ": extra_damage needs damage[]";
        int extra = 0;
        std::optional<rules5e::DiceExpr> add;
        if (auto sc = r.find("scale"); sc != r.end() && sc->is_object())
            if (auto pl = sc->find("per_levels"); pl != sc->end() && pl->is_object()) {
                add = parse_dice(pl->value("add_dice", ""));
                const int every = geti(*pl, "every");
                if (!add || every < 1) return where + ": bad scale.per_levels";
                extra = (owner_level(doc, *pl) - 1) / every;
            }
        std::optional<rules5e::DiceExpr> slot_add;
        uint8_t slot_base = 0;
        if (auto sc = r.find("scale"); sc != r.end() && sc->is_object())
            if (auto ps = sc->find("per_slot_level"); ps != sc->end() && ps->is_object()) {
                slot_add = parse_dice(ps->value("add_dice", ""));
                const int base = geti(*ps, "base_level");
                if (!slot_add || base < 1 || base > 9) return where + ": bad scale.per_slot_level";
                slot_base = uint8_t(base);
            }
        bool first = true;
        for (const Json& d : *dm) {
            auto dice = d.is_object() && d["dice"].is_string() ? parse_dice(d["dice"].get<std::string>()) : std::nullopt;
            const std::string ty = d.is_object() && d["type"].is_string() ? d["type"].get<std::string>() : "";
            if (!dice || ty.empty()) return where + ": bad rider damage";
            rules5e::RiderEffect x;
            x.kind = RK::ExtraDamage;
            x.dice = *dice;
            if (ty == "same_as_weapon") x.same_as_weapon = true;
            else if (auto t = damage_type(ty)) x.type = *t;
            else return where + ": bad rider damage type";
            if (first && slot_add) { x.slot_dice = *slot_add; x.slot_base = slot_base; }
            if (first && add && extra > 0) {   // scaling rides the first damage entry
                if (add->sides != x.dice.sides) return where + ": scale add_dice sides differ from the base dice";
                const int cnt = int(x.dice.count) + extra * int(add->count);
                if (cnt > 255) return where + ": scaled dice too large";
                x.dice.count = uint8_t(cnt);
                x.dice.bonus = int16_t(x.dice.bonus + extra * add->bonus);
            }
            first = false;
            out.push_back(x);
        }
        return std::nullopt;
    }
    if (k == "bonus_attack") {   // Rampage: value = index into the owner's attacks, resolved from attack_id
        const std::string aid = r.contains("attack_id") && r["attack_id"].is_string() ? r["attack_id"].get<std::string>() : "";
        for (std::size_t i = 0; i < rc.attacks.size(); ++i)
            if (rc.attacks[i]->value("id", "") == aid && !aid.empty()) {
                e.kind = RK::BonusAttack;
                e.value = int16_t(i);
                out.push_back(e);
                return std::nullopt;
            }
        return where + ": bonus_attack names unknown attack_id '" + aid + "'";
    }
    if (k == "area_save") {   // Death Burst: a synthetic Save SpellDef (fixed DC, is_spell false) the rules resolve over the hook's targets
        Json act = r;
        act["kind"] = "save_effect";
        act["id"] = "area_save." + std::to_string(rc.synth.size());
        act.erase("origin");
        rules5e::SpellDef d;
        Archetype::SaveAction sa;
        bool unsupported = false;
        if (auto er = compile_save_action(rc.arch, act, d, sa, doc, unsupported)) return er;
        if (unsupported) return where + ": area_save uses an unsupported usage or condition duration";
        if (!sa.area) return where + ": area_save needs an area";
        d.content_id = rc.arch + "#" + act["id"].get<std::string>();
        e.kind = RK::AreaSave;
        e.spell_id = d.id;
        rc.synth.push_back(std::move(d));
        out.push_back(e);
        return std::nullopt;
    }
    if (k == "spend") {   // pool + cost -> slot 0, pool id, value = cost; slot N -> N; "any" -> 10
        e.kind = RK::Spend;
        if (auto sl = r.find("slot"); sl != r.end()) {
            if (sl->is_string() && sl->get<std::string>() == "any") e.slot = 10;
            else if (sl->is_number_integer() && sl->get<int>() >= 1 && sl->get<int>() <= 9) e.slot = uint8_t(sl->get<int>());
            else return where + ": bad spend slot";
        } else {
            if (!r.contains("pool") || !r["pool"].is_string()) return where + ": spend needs pool or slot";
            const int cost = geti(r, "cost", 1);
            if (cost < 1 || cost > 255) return where + ": bad spend cost";
            e.pool = rules5e::spell_id_of(r["pool"].get<std::string>());
            e.value = int16_t(cost);
        }
        out.push_back(e);
        return std::nullopt;
    }
    if (k == "save_condition") {   // a synthetic Save SpellDef (fixed DC, damage, condition); same_as_weapon is a load error (bad damage type)
        Json act = r;
        act["kind"] = "save_effect";
        act["id"] = "save_condition." + std::to_string(rc.synth.size());
        rules5e::SpellDef d;
        Archetype::SaveAction sa;
        bool unsupported = false;
        if (auto er = compile_save_action(rc.arch, act, d, sa, doc, unsupported)) return er;
        if (unsupported) return where + ": save_condition uses an unsupported duration";
        e.kind = RK::SaveCondition;
        e.spell_id = d.id;
        rc.synth.push_back(std::move(d));
        out.push_back(e);
        return std::nullopt;
    }
    if (k == "apply_condition") {   // a synthetic ApplyCondition SpellDef: condition, expiry, duration_rounds
        const auto ap = r.find("apply");
        if (ap == r.end() || !ap->is_array() || ap->size() != 1 || !(*ap)[0].is_object()) return where + ": apply_condition needs exactly one apply[] entry";
        const Json& x = (*ap)[0];
        auto cond = condition_of(x.value("condition", ""));
        if (!cond) return where + ": bad condition";
        if (x.contains("repeat_save")) return where + ": apply_condition does not take repeat_save";
        rules5e::SpellDef d;
        d.content_id = rc.arch + "#apply_condition." + std::to_string(rc.synth.size());
        d.id = rules5e::spell_id_of(d.content_id);
        d.level = 0;
        d.casting_time = rules5e::CastingTime::Action;
        d.kind = rules5e::SpellEffectKind::ApplyCondition;
        d.is_spell = false;
        d.magical = false;
        d.applies_condition = true;
        d.condition = *cond;
        if (!x.contains("duration")) return where + ": apply_condition needs a duration";
        const Json& du = x["duration"];
        const std::string dk = du.is_object() && du.contains("kind") && du["kind"].is_string() ? du["kind"].get<std::string>() : "";
        if (dk == "rounds") {
            const int n = geti(du, "n");
            if (n < 1 || n > 65535) return where + ": bad duration rounds";
            d.expiry = uint8_t(rules5e::Expiry::Rounds);
            d.duration_rounds = uint16_t(n);
        } else if (dk == "until_end_of_target_next_turn") d.expiry = uint8_t(rules5e::Expiry::EndOfTargetNextTurn);
        else return where + ": unsupported apply_condition duration";
        e.kind = RK::ApplyCondition;
        e.spell_id = d.id;
        rc.synth.push_back(std::move(d));
        out.push_back(e);
        return std::nullopt;
    }
    // Every other rider kind is stored (reserved) and does not fire.
    static const std::pair<const char*, RK> kinds[] = {{"heal_self", RK::HealSelf}, {"temp_hp", RK::TempHp}, {"set_hp", RK::SetHp}};
    for (const auto& [name, kind] : kinds)
        if (k == name) {
            e.kind = kind;
            e.value = int16_t(geti(r, "value", geti(r, "amount")));
            if (auto d = r.find("dice"); d != r.end() && d->is_string())
                if (auto pd = parse_dice(d->get<std::string>())) e.dice = *pd;
            if (kind == RK::Spend && r.contains("pool") && r["pool"].is_string()) e.pool = rules5e::spell_id_of(r["pool"].get<std::string>());
            out.push_back(e);
            return std::nullopt;
        }
    return where + ": unknown rider kind '" + k + "'";
}

std::optional<std::string> compile_predicate(const std::string& where, const Json& p, rules5e::Predicate& out) {
    if (auto e = rules5e_content::parse_predicate(p, out); !e.empty()) return where + ": " + e;
    return std::nullopt;
}

// R-G6 bonus-action attack options (declared >= 4 / undeclared only), compiled from existing content fields (no schema change):
//   * two-weapon fighting: a character with >= 2 melee weapon actions whose `properties` list "light" gets one off-hand option per
//     light weapon (requires one of the light weapons used by the Attack action, a different one); the off-hand rolls the dice
//     without the ability modifier unless a feature with id "two_weapon_fighting" (the style) is present;
//   * features by local id: "martial_arts" (an unarmed strike: an action whose properties list "unarmed"), "flurry_of_blows"
//     (2 unarmed strikes, spends 1 from the "ki" pool; the document must declare a resource "ki"), "frenzy" (needs a "rage"
//     feature; any melee attack while the Rage modifier, `<archetype>#rage#m0`, is in force).
// Martial Arts / Flurry require the Attack action to have used an "unarmed" or "monk" action.
std::optional<std::string> compile_bonus_attacks(const std::string& aid, const std::vector<FeatureDecl>& feats,
                                                 const std::vector<const Json*>& acts, bool is_character, const Json& doc,
                                                 rules5e::Combatant& c) {
    auto has_feature = [&](const char* id) {
        for (const auto& f : feats) if (f.id == id) return true;
        return false;
    };
    auto has_prop = [](const Json& act, const char* p) {
        auto it = act.find("properties");
        if (it == act.end() || !it->is_array()) return false;
        for (const Json& x : *it) if (x.is_string() && x.get<std::string>() == p) return true;
        return false;
    };
    uint32_t light = 0, unarmed_set = 0;
    int first_unarmed = -1;
    for (std::size_t i = 0; i < acts.size() && i < 32; ++i) {
        const Json& act = *acts[i];
        if (act.value("kind", "") != "melee_weapon_attack") continue;
        if (has_prop(act, "light")) light |= 1u << i;
        if (has_prop(act, "unarmed") || has_prop(act, "monk")) unarmed_set |= 1u << i;
        if (has_prop(act, "unarmed") && first_unarmed < 0) first_unarmed = int(i);
    }
    auto add = [&](const std::string& name, rules5e::BonusAttack b) {
        b.id = rules5e::spell_id_of(aid + "#bonus#" + name);
        c.bonus_attacks.push_back(b);
    };
    if (is_character && std::popcount(light) >= 2) {
        const bool style = has_feature("two_weapon_fighting");
        for (std::size_t i = 0; i < acts.size() && i < 32; ++i) {
            if (!(light & (1u << i))) continue;
            rules5e::BonusAttack b;
            b.attack_index = uint8_t(i);
            b.requires_attacks = light;
            b.different_weapon = true;
            if (!style) {
                b.has_damage = true;
                b.damage = c.attacks[i].damage;
                b.damage.bonus = 0;
            }
            add("offhand#" + acts[i]->value("id", "?"), b);
        }
    }
    const bool martial = has_feature("martial_arts"), flurry = has_feature("flurry_of_blows");
    if ((martial || flurry) && first_unarmed < 0) return aid + ": martial_arts / flurry_of_blows need an action with the 'unarmed' property";
    if (martial) {
        rules5e::BonusAttack b;
        b.attack_index = uint8_t(first_unarmed);
        b.requires_attacks = unarmed_set;
        add("martial_arts", b);
    }
    if (flurry) {
        bool ki = false;
        if (auto rs = doc.find("resources"); rs != doc.end() && rs->is_array())
            for (const Json& r : *rs) ki = ki || (r.is_object() && r.value("id", "") == "ki");
        if (!ki) return aid + ": flurry_of_blows needs a declared 'ki' resource";
        rules5e::BonusAttack b;
        b.attack_index = uint8_t(first_unarmed);
        b.count = 2;
        b.requires_attacks = unarmed_set;
        b.pool = rules5e::spell_id_of("ki");
        b.pool_cost = 1;
        add("flurry_of_blows", b);
    }
    if (has_feature("frenzy")) {
        if (!has_feature("rage")) return aid + ": frenzy needs a 'rage' feature";
        rules5e::BonusAttack b;
        b.any_melee = true;
        b.requires_modifier = rules5e::spell_id_of(aid + "#rage#m0");
        add("frenzy", b);
    }
    if (c.bonus_attacks.size() > 255) return aid + ": too many bonus attack options";
    return std::nullopt;
}

// Adds a trigger to the archetype's base combatant. `ids` maps numeric ids to their strings (duplicate = load error).
std::optional<std::string> add_trigger(const std::string& arch, const std::string& owner, std::size_t index, rules5e::Trigger t,
                                       std::map<uint32_t, std::string>& ids, rules5e::Combatant& c) {
    const std::string name = arch + "#" + owner + "#" + std::to_string(index);
    t.id = rules5e::spell_id_of(name);
    if (auto [it, fresh] = ids.emplace(t.id, name); !fresh)
        return arch + ": trigger id collision between '" + it->second + "' and '" + name + "'";
    if (c.triggers.size() >= 32) return arch + ": too many triggers (max 32)";
    c.triggers.push_back(std::move(t));
    return std::nullopt;
}

// `effect` is an on_event effect object. Returns an error, or sets `unsupported` (listed, not compiled).
std::optional<std::string> compile_on_event(const std::string& arch, const std::string& owner, const Json& effect, const Json& doc, const RiderCtx& rc,
                                            std::map<uint32_t, std::string>& ids, rules5e::Combatant& c, bool& unsupported) {
    const std::string where = arch + ": " + owner + " on_event";
    static const std::pair<const char*, rules5e::TriggerOn> ons[] = {{"hit", rules5e::TriggerOn::Hit}, {"crit", rules5e::TriggerOn::Crit},
        {"damage_dealt", rules5e::TriggerOn::DamageDealt}, {"reduce_to_zero", rules5e::TriggerOn::ReduceToZero},
        {"drop_to_zero", rules5e::TriggerOn::DropToZero}, {"death", rules5e::TriggerOn::Death},
        {"turn_start", rules5e::TriggerOn::TurnStart}, {"turn_end", rules5e::TriggerOn::TurnEnd}};
    rules5e::Trigger t;
    const std::string on = effect.contains("on") && effect["on"].is_string() ? effect["on"].get<std::string>() : "";
    bool found = false;
    for (const auto& [n, v] : ons) if (on == n) { t.on = v; found = true; }
    if (!found) return where + ": bad 'on'";
    const std::string lim = effect.contains("limit") && effect["limit"].is_string() ? effect["limit"].get<std::string>() : "none";
    if (lim == "none") t.limit = rules5e::TriggerLimit::None;
    else if (lim == "once_per_turn") t.limit = rules5e::TriggerLimit::OncePerTurn;
    else if (lim == "once_per_round") t.limit = rules5e::TriggerLimit::OncePerRound;
    else if (lim == "once_per_attack") t.limit = rules5e::TriggerLimit::OncePerAttack;
    else return where + ": bad limit";
    const std::string ch = effect.contains("choice") && effect["choice"].is_string() ? effect["choice"].get<std::string>() : "auto";
    t.choice = ch == "auto" ? 0 : ch == "ask" ? 1 : ch == "never" ? 2 : 255;
    if (t.choice == 255) return where + ": bad choice";
    if (effect.contains("usage")) { unsupported = true; return std::nullopt; }   // a use cost is a later sub-PR
    if (auto w = effect.find("when"); w != effect.end()) {
        if (!w->is_array()) return where + ": bad when";
        for (const Json& p : *w) {
            rules5e::Predicate pr;
            if (auto e = compile_predicate(where, p, pr)) return e;
            t.when.push_back(pr);
        }
    }
    auto ef = effect.find("effects");
    if (ef == effect.end() || !ef->is_array() || ef->empty()) return where + ": needs effects[]";
    for (const Json& r : *ef) if (auto e = compile_rider(where, r, doc, rc, t.effects)) return e;
    return add_trigger(arch, owner, 0, std::move(t), ids, c);
}

std::optional<std::string> compile_save_action(const std::string& arch_id, const Json& act, rules5e::SpellDef& d,
                                               Archetype::SaveAction& sa, const Json& doc, bool& unsupported) {
    const std::string aid = act.value("id", "");
    const std::string where = arch_id + ": save_effect '" + aid + "'";
    if (aid.empty() || aid.find('#') != std::string::npos || arch_id.find('#') != std::string::npos)
        return where + ": id must be non-empty and contain no '#'";
    d = rules5e::SpellDef{};
    d.content_id = arch_id + "#" + aid;
    d.id = rules5e::spell_id_of(d.content_id);
    d.level = 0;
    d.casting_time = rules5e::CastingTime::Action;
    d.kind = rules5e::SpellEffectKind::Save;
    d.is_spell = false;   // not a spell: no SpellCast window, no components, not Counterspellable
    d.magical = act.value("magical", false);
    d.targets = 1;
    sa = {};
    sa.id = aid;
    sa.spell_id = d.id;
    const auto sv = act.find("save");
    if (sv == act.end() || !sv->is_object()) return where + ": missing save";
    auto ab = ability_of(sv->value("ability", ""));
    const int dc = geti(*sv, "dc");
    if (!ab || dc < 1 || dc > 30) return where + ": bad save";
    d.save_ability = *ab;
    d.fixed_dc = int8_t(dc);
    // Area template (validated and kept for the area row; UI hint until then) and origin / range.
    const bool has_area = act.contains("area") && act["area"].is_object();
    const std::string origin = act.value("origin", "self");
    if (origin != "self" && origin != "point") return where + ": bad origin";
    if (has_area) {
        const Json& ar = act["area"];
        const std::string shape = ar.value("shape", "");
        d.area = shape == "cone" ? rules5e::AreaShape::Cone : shape == "cube" ? rules5e::AreaShape::Cube
               : shape == "cylinder" ? rules5e::AreaShape::Cylinder : shape == "line" ? rules5e::AreaShape::Line
               : shape == "sphere" ? rules5e::AreaShape::Sphere : rules5e::AreaShape::None;
        if (d.area == rules5e::AreaShape::None) return where + ": bad area shape";
        const int size = geti(ar, "size_ft"), width = geti(ar, "width_ft"), height = geti(ar, "height_ft");
        if (size < 5 || size > 65535 || width < 0 || width > 65535 || height < 0 || height > 65535) return where + ": bad area size";
        d.area_size_ft = uint16_t(size);
        d.area_width_ft = uint16_t(width);
        d.area_height_ft = uint16_t(height);
        sa.area = true;
    }
    const int range = geti(act, "range_ft", origin == "self" && has_area ? 0 : 5);
    if (range < 0 || range > 65535 || (origin == "point" && range < 5)) return where + ": bad range_ft";
    d.range_ft = uint16_t(origin == "self" && has_area ? 0 : range);
    sa.range_ft = d.range_ft;
    // Damage (one component; the save is rolled once per target) and applied conditions.
    if (auto dm = act.find("damage"); dm != act.end() && dm->is_array() && !dm->empty()) {
        const auto on_save = act.value("on_success", "") == "half" ? rules5e::OnSave::Half : rules5e::OnSave::None;
        for (std::size_t i = 0; i < dm->size(); ++i) {
            auto dice = parse_dice((*dm)[i].value("dice", ""));
            auto type = damage_type((*dm)[i].value("type", ""));
            if (!dice || !type) return where + ": bad damage";
            if (i == 0) {
                d.dice = *dice;
                d.damage_type = *type;
                d.on_save = on_save;
            } else {   // further damage components: further Save effects over the same targets, on_save copied
                rules5e::SpellEffect e;
                e.kind = rules5e::SpellEffectKind::Save;
                e.dice = *dice;
                e.damage_type = *type;
                e.save_ability = d.save_ability;
                e.on_save = on_save;
                d.extra_effects.push_back(e);
            }
        }
    }
    if (auto ap = act.find("apply"); ap != act.end() && ap->is_array()) {
        auto repeat_of = [](const std::string& r) {
            return r == "start_of_turn" ? rules5e::RepeatSave::StartOfTurn : r == "end_of_turn" ? rules5e::RepeatSave::EndOfTurn
                 : r == "on_damage" ? rules5e::RepeatSave::OnDamage : rules5e::RepeatSave::None;
        };
        bool first = true;
        for (const Json& x : *ap) {
            auto cond = condition_of(x.is_object() ? x.value("condition", "") : "");
            if (!cond) return where + ": bad condition in apply[]";
            // Durations (rules5e timed effects, #76): rounds n -> expiry Rounds + duration_rounds; until_end_of_target_next_turn
            // -> expiry EndOfTargetNextTurn; until_save -> a repeat save (end_of_turn unless one is named), no expiry. A
            // repeat_save may accompany a timed duration. No duration and no repeat save lasts until the combat ends,
            // which the engine does not model for conditions yet: listed, not compiled.
            const std::string rs = x.value("repeat_save", "");
            const bool has_dur = x.contains("duration");
            if (has_dur && !x["duration"].is_object()) return where + ": bad duration in apply[]";
            const Json dur = has_dur ? x["duration"] : Json::object();
            const std::string dk = dur.is_object() && dur.contains("kind") && dur["kind"].is_string() ? dur["kind"].get<std::string>() : "";
            uint8_t expiry = 0;
            uint16_t rounds = 0;
            if (dk == "rounds") {
                const int n = geti(dur, "n");
                if (n < 1 || n > 65535) return where + ": bad duration rounds";
                expiry = uint8_t(rules5e::Expiry::Rounds);
                rounds = uint16_t(n);
            } else if (dk == "until_end_of_target_next_turn") {
                expiry = uint8_t(rules5e::Expiry::EndOfTargetNextTurn);
            } else if (dk != "until_save" && (has_dur || rs.empty())) {
                unsupported = true;   // an unknown duration kind (or none at all, without a repeat save)
                return std::nullopt;
            }
            const rules5e::RepeatSave rep = !rs.empty() ? repeat_of(rs)
                                          : dk == "until_save" ? rules5e::RepeatSave::EndOfTurn : rules5e::RepeatSave::None;
            if (first) {   // the first condition rides the primary Save effect
                d.applies_condition = true;
                d.condition = *cond;
                d.repeat_save = rep;
                d.expiry = expiry;
                d.duration_rounds = rounds;
                first = false;
            } else {       // further conditions: ApplyCondition on the creatures that failed the save
                rules5e::SpellEffect e;
                e.kind = rules5e::SpellEffectKind::ApplyCondition;
                e.applies_condition = true;
                e.condition = *cond;
                e.repeat_save = rep;
                e.expiry = expiry;
                e.duration_rounds = rounds;
                e.affected_only = true;
                d.extra_effects.push_back(e);
            }
        }
    }
    if (d.dice.count == 0 && d.dice.bonus == 0 && !d.applies_condition) return where + ": needs damage[] or apply[]";
    // Usage. Pools need resource rows (a later row): such actions are listed, not compiled.
    if (auto u = act.find("usage"); u != act.end() && u->is_object()) {
        const std::string k = u->value("kind", "");
        if (k == "at_will") d.usage_kind = 0;
        else if (k == "recharge") {
            const int mn = geti(*u, "min");
            if (mn < 2 || mn > 6) return where + ": bad recharge min";
            d.usage_kind = 1;
            d.recharge_min = uint8_t(mn);
        } else if (k == "per_day" || k == "per_rest") {
            const int uses = geti(*u, "uses");
            if (uses < 1 || uses > 255) return where + ": bad uses";
            d.usage_kind = 2;
            sa.uses = uint8_t(uses);
            sa.recharge = k == "per_day" || u->value("rest", "long") == "long" ? Recharge::LongRest : Recharge::ShortRest;
        } else if (k == "pool") {
            // The pool comes from the document's resources[]; integer max only until class pools (row 13).
            const std::string pid = u->value("pool", "");
            const Json* res = nullptr;
            if (auto rs = doc.find("resources"); rs != doc.end() && rs->is_array())
                for (const Json& r : *rs) if (r.is_object() && r.value("id", "") == pid) res = &r;
            if (!res || !(*res)["max"].is_number_integer()) { unsupported = true; return std::nullopt; }
            const int mx = (*res)["max"].get<int>(), cost = geti(*u, "cost", 1);
            const int start = geti(*res, "start", mx);
            if (mx < 1 || mx > 65535 || cost < 1 || cost > 255 || start < 0 || start > mx) return where + ": bad pool";
            const std::string rc = res->value("recharge", "never");
            d.usage_kind = 3;
            d.pool_id = rules5e::spell_id_of(pid);
            d.pool_cost = uint8_t(cost);
            sa.pool_id = d.pool_id;
            sa.pool_cost = d.pool_cost;
            sa.pool_max = uint16_t(mx);
            sa.pool_start = uint16_t(start);
            sa.pool_recharge = rc == "short_rest" ? Recharge::ShortRest : rc == "long_rest" ? Recharge::LongRest : Recharge::None;
        } else return where + ": bad usage kind";
    }
    sa.usage_kind = d.usage_kind;
    return std::nullopt;
}

// ---- Reactions (row 11 sim side): reactions[] entries -> rules5e::Reaction rows on the base combatant ----
// Compiled: window damaged / ally_attacked with reduce_damage (fraction half | dice), impose_disadvantage, retaliate
// (synthetic Save or AutoHit SpellDef) and attack (attack_id -> attack index); window roll_attack (R-G7, AttackRolled) with
// subtract_roll {dice} and an optional `usage: {kind: pool, pool, cost}` (Cutting Words). Listed in unsupported_features and
// not compiled: every other window (being_hit / spell_cast / leaving_reach: ac_bonus and the opportunity-attack path stay with
// the existing hooks and features; a leaving_reach `attack` is never a second OA), effect kinds cast / ac_bonus, any other
// `usage`, `source: ally_within_5ft` and `visible: false`.
std::optional<std::string> compile_reactions(Archetype& a, const Json& doc, const RiderCtx& rc, rules5e::Combatant& c) {
    auto rs = doc.find("reactions");
    if (rs == doc.end() || !rs->is_array()) return std::nullopt;
    using RE = rules5e::ReactionEffectKind;
    std::map<uint32_t, std::string> ids;
    for (const Json& r : *rs) {
        if (!r.is_object() || r.value("inert", false)) continue;
        const std::string rid = r.value("id", "");
        const std::string where = a.id + ": reaction '" + rid + "'";
        if (rid.empty() || rid.find('#') != std::string::npos) return where + ": id must be non-empty and contain no '#'";
        const Json* w = r.contains("window") && r["window"].is_object() ? &r["window"] : nullptr;
        const Json* ef = r.contains("effect") && r["effect"].is_object() ? &r["effect"] : nullptr;
        if (!w || !ef) return where + ": needs window and effect";
        const std::string wk = w->value("kind", ""), ek = ef->value("kind", "");
        rules5e::Reaction x;
        if (wk == "damaged") x.window = rules5e::ReactionTrigger::Damaged;
        else if (wk == "ally_attacked") x.window = rules5e::ReactionTrigger::AllyAttacked;
        else if (wk == "roll_attack") x.window = rules5e::ReactionTrigger::AttackRolled;   // R-G7 (Cutting Words)
        else { a.unsupported_features.push_back(rid + " (reaction window " + wk + ")"); continue; }
        const bool attack_rolled = x.window == rules5e::ReactionTrigger::AttackRolled;
        // R-G7: a subtract_roll reaction may spend from a resources[] pool (Cutting Words: bardic inspiration). Combat::create
        // registers resources[] pools with rules5e::register_pool; an unlimited pool gates nothing.
        if (attack_rolled && ek == "subtract_roll" && r.contains("usage")) {
            const Json& u = r["usage"];
            if (!u.is_object() || u.value("kind", "") != "pool") {
                a.unsupported_features.push_back(rid + " (reaction: unsupported usage / source / visible)");
                continue;
            }
            const std::string pid = u.value("pool", "");
            const int cost = u.contains("cost") ? (u["cost"].is_number_integer() ? u["cost"].get<int>() : 0) : 1;
            if (cost < 1 || cost > 65535) return where + ": bad usage cost";
            bool found = false, unlimited = false;
            for (const auto& res : a.resources) found = found || res.id == pid;
            for (const auto& res : a.unlimited_resources) unlimited = unlimited || res == pid;
            if (!found && !unlimited) return where + ": usage names unknown pool '" + pid + "'";
            if (found) { x.pool = rules5e::spell_id_of(pid); x.pool_cost = uint16_t(cost); }
        } else if (r.contains("usage")) {
            a.unsupported_features.push_back(rid + " (reaction: unsupported usage / source / visible)");
            continue;
        }
        if (w->value("source", "self") != "self" || !w->value("visible", true)) {
            a.unsupported_features.push_back(rid + " (reaction: unsupported usage / source / visible)");
            continue;
        }
        x.melee_only = w->value("melee_only", false);
        x.ranged_only = w->value("ranged_only", false);
        if (x.melee_only && x.ranged_only) return where + ": melee_only and ranged_only together";
        const bool damaged = x.window == rules5e::ReactionTrigger::Damaged;
        if (ek == "reduce_damage") {
            if (!damaged) return where + ": reduce_damage needs the damaged window";
            x.kind = RE::ReduceDamage;
            if (ef->contains("fraction")) x.half = true;
            else if (auto d = parse_dice(ef->contains("dice") && (*ef)["dice"].is_string() ? (*ef)["dice"].get<std::string>() : "")) x.dice = *d;
            else return where + ": reduce_damage needs fraction or dice";
        } else if (ek == "subtract_roll") {   // R-G7 Cutting Words: subtract `dice` (the bardic die) from an enemy's attack roll
            if (!attack_rolled) return where + ": subtract_roll needs the roll_attack window";
            x.kind = RE::SubtractRoll;
            auto d = parse_dice(ef->contains("dice") && (*ef)["dice"].is_string() ? (*ef)["dice"].get<std::string>() : "");
            if (!d || d->count == 0) return where + ": subtract_roll needs dice";
            x.dice = *d;
        } else if (attack_rolled) {
            return where + ": the roll_attack window only takes subtract_roll";
        } else if (ek == "impose_disadvantage") {
            if (damaged) return where + ": impose_disadvantage needs the ally_attacked window";
            x.kind = RE::ImposeDisadvantage;
        } else if (ek == "retaliate") {
            if (!damaged) return where + ": retaliate needs the damaged window";
            x.kind = RE::Retaliate;
            const auto dm = ef->find("damage");
            if (dm == ef->end() || !dm->is_array() || dm->empty()) return where + ": retaliate needs damage[]";
            rules5e::SpellDef d;
            if (ef->contains("save")) {   // Save kind: fixed DC, dice, damage type, on_save
                Json act = *ef;
                act["kind"] = "save_effect";
                act["id"] = "reaction." + rid;
                Archetype::SaveAction sa;
                bool unsupported = false;
                if (auto er = compile_save_action(rc.arch, act, d, sa, doc, unsupported)) return er;
            } else {                      // no save: an auto-hit def (one damage entry)
                if (dm->size() != 1) return where + ": retaliate without a save takes one damage entry";
                auto dice = parse_dice((*dm)[0].value("dice", ""));
                auto type = damage_type((*dm)[0].value("type", ""));
                if (!dice || !type) return where + ": bad damage";
                d.content_id = rc.arch + "#reaction." + rid;
                d.id = rules5e::spell_id_of(d.content_id);
                d.level = 0;
                d.casting_time = rules5e::CastingTime::Reaction;
                d.kind = rules5e::SpellEffectKind::AutoHit;
                d.is_spell = false;
                d.magical = false;
                d.targets = 1;
                d.darts = 1;
                d.dice = *dice;
                d.damage_type = *type;
            }
            x.spell_id = d.id;
            rc.synth.push_back(std::move(d));
        } else if (ek == "attack") {
            if (!damaged) return where + ": attack needs the damaged window";
            x.kind = RE::Attack;
            const std::string at = ef->value("attack_id", "");
            bool found = false;
            for (std::size_t i = 0; i < rc.attacks.size() && !found; ++i)
                if (!at.empty() && rc.attacks[i]->value("id", "") == at) { x.attack_index = uint8_t(i); found = true; }
            if (!found) return where + ": attack names unknown attack_id '" + at + "'";
        } else { a.unsupported_features.push_back(rid + " (reaction effect " + ek + ")"); continue; }
        x.id = rules5e::spell_id_of(a.id + "#" + rid);
        if (auto [it, fresh] = ids.emplace(x.id, rid); !fresh) return where + ": id collides with reaction '" + it->second + "'";
        c.reactions.push_back(x);
    }
    return std::nullopt;
}

// ---- resources[] (row 13): integer | per_level / plus_ability | table -> pool rows ----
// `max` resolves against the owner's level in `class` (else the total level) and ability modifier at load time; a
// table is a step function (last entry with level <= L, 0 below the first); an `unlimited` entry registers no pool.
int level_in(const Json& doc, const std::string& cls) {
    int level = 0;
    if (!cls.empty()) {
        for (const Json& c : doc.value("classes", Json::array()))
            if (c.is_object() && c.value("class", "") == cls) level += geti(c, "level");
        return level;
    }
    return std::max(1, geti(doc, "level", 1));
}

std::optional<std::string> compile_resources(Archetype& a, const Json& doc, bool is_character, rules5e::Combatant& c) {
    auto rs = doc.find("resources");
    if (rs == doc.end() || !rs->is_array()) return std::nullopt;
    for (const Json& r : *rs) {
        if (!r.is_object()) return a.id + ": bad resource";
        const std::string rid = r.value("id", "");
        const std::string where = a.id + ": resource '" + rid + "'";
        if (rid.empty()) return where + ": needs an id";
        for (const auto& o : a.resources) if (o.id == rid) return where + ": duplicate id";
        for (const auto& o : a.unlimited_resources) if (o == rid) return where + ": duplicate id";
        const auto mx = r.find("max");
        if (mx == r.end()) return where + ": needs max";
        long max = 0;
        bool unlimited = false;
        if (mx->is_number_integer()) max = mx->get<long>();
        else if (mx->is_object() && mx->contains("table")) {
            if (!is_character) return where + ": a monster pool must use an integer max";
            const Json& t = (*mx)["table"];
            const auto bl = t.is_object() ? t.find("by_level") : t.end();
            if (!t.is_object() || bl == t.end() || !bl->is_array() || bl->empty()) return where + ": bad table";
            const int lvl = level_in(doc, t.value("class", ""));
            int best = 0;
            for (const Json& e : *bl) {
                if (!e.is_object()) return where + ": bad table entry";
                const int el = geti(e, "level");
                if (el < 1 || el > 20) return where + ": bad table level";
                if (el <= lvl && el >= best) { best = el; unlimited = e.value("unlimited", false); max = geti(e, "value"); }
            }
            if (best == 0) { max = 0; unlimited = false; }
        } else if (mx->is_object()) {
            if (!is_character) return where + ": a monster pool must use an integer max";
            if (!mx->contains("per_level") && !mx->contains("plus_ability")) return where + ": max needs per_level or plus_ability";
            const int lvl = level_in(doc, mx->value("class", ""));
            max = long(geti(*mx, "per_level")) * lvl;
            if (mx->contains("plus_ability")) {
                auto ab = ability_of((*mx)["plus_ability"].is_string() ? (*mx)["plus_ability"].get<std::string>() : "");
                if (!ab) return where + ": bad plus_ability";
                const int score = c.abilities.score[std::size_t(int(*ab))];
                max += (score - 10) >= 0 ? (score - 10) / 2 : -((11 - score) / 2);
            }
            max = std::max<long>(max, geti(*mx, "min", 1));
        } else return where + ": bad max";
        if (max < 0 || max > 65535) return where + ": max out of range";
        if (unlimited) { a.unlimited_resources.push_back(rid); continue; }
        const std::string rc = r.value("recharge", "never");
        if (rc != "short_rest" && rc != "long_rest" && rc != "dawn" && rc != "never") return where + ": bad recharge";
        Archetype::Resource res;
        res.id = rid;
        res.pool_id = rules5e::spell_id_of(rid);
        res.max = uint16_t(max);
        res.start = uint16_t(max);
        if (r.contains("start")) {
            const int st = geti(r, "start", -1);
            if (st < 0 || st > max) return where + ": bad start";
            res.start = uint16_t(st);
        }
        res.recharge = uint8_t(rc == "short_rest" ? rules5e::FeatureRecharge::ShortRest
                               : rc == "long_rest" || rc == "dawn" ? rules5e::FeatureRecharge::LongRest : rules5e::FeatureRecharge::None);
        if (rid == "legendary_resistance") c.legendary_pool = res.pool_id;   // Legendary Resistance: rules auto-spend on a failed save
        a.resources.push_back(res);
    }
    return std::nullopt;
}
// R-G7 wield model: equipment[] names a shield, or armor_class.note mentions one. An entry is a string, {"id": ...} or the
// character@4 form {"item": "srd51.item.shield"}; it is a shield when the id is "shield" or a namespaced id whose last
// '.'-segment is "shield" (sim does not load item@1 defs, so the item's category is not reachable here).
bool carries_shield(const Json& doc) {
    auto is_shield_id = [](const std::string& id) {
        const std::size_t dot = id.rfind('.');
        return (dot == std::string::npos ? id : id.substr(dot + 1)) == "shield";
    };
    if (auto eq = doc.find("equipment"); eq != doc.end() && eq->is_array())
        for (const Json& e : *eq) {
            if (is_shield_id(equipment_entry_id(e))) return true;
        }
    if (auto ac = doc.find("armor_class"); ac != doc.end() && ac->is_object()) {
        std::string note = ac->value("note", "");
        for (char& ch : note) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (note.find("shield") != std::string::npos) return true;
    }
    return false;
}

} // namespace

std::optional<rules5e::DamageType> damage_type_from_name(const std::string& s) { return damage_type(s); }

std::optional<std::string> ContentLibrary::add(const Json& doc, const std::string& schema_type, const std::string& pack_id,
                                                 uint32_t declared_version) {
    Archetype a;
    a.id = doc.value("id", "");
    if (a.id.empty()) return "stat block without id";
    a.schema_type = schema_type;
    a.pack_id = pack_id;
    rules5e::Combatant& c = a.base;
    static const char* ab[] = {"str", "dex", "con", "int", "wis", "cha"};
    const Json& abil = doc.at("abilities");
    for (int i = 0; i < 6; ++i) c.abilities.score[std::size_t(i)] = int8_t(geti(abil, ab[i], 10));
    c.ac = int8_t(geti(doc.at("armor_class"), "value"));
    c.hp = c.hp_max = int16_t(geti(doc.at("hit_points"), "average"));
    c.speed_ft = uint8_t(geti(doc.at("speed"), "walk"));
    c.proficiency_bonus = int8_t(geti(doc, "proficiency_bonus", 2));
    if (auto st = doc.find("saving_throws"); st != doc.end())
        for (int i = 0; i < 6; ++i)
            if (st->contains(ab[i])) c.save_proficiencies |= rules5e::bit(rules5e::Ability(i));
    // monster@3 / character@3 defenses. Pre-@3 files' copies are inert (migration marker).
    std::set<std::string> inert;
    if (auto m = doc.find("inert_mechanics"); m != doc.end() && m->is_array())
        for (const Json& x : *m) if (x.is_string()) inert.insert(x.get<std::string>());
    a.inert_mechanics.assign(inert.begin(), inert.end());
    // @4 forward_compat flags (descriptive, ignored by sim; listed with the inert mechanics).
    if (auto fc = doc.find("forward_compat"); fc != doc.end() && fc->is_array())
        for (const Json& x : *fc) if (x.is_string()) a.forward_compat.push_back(x.get<std::string>());
    auto damage_mask = [&](const char* key, uint32_t& mask) -> std::optional<std::string> {
        auto it = doc.find(key);
        if (it == doc.end() || inert.count(key)) return std::nullopt;
        for (const Json& n : *it) {
            auto t = damage_type(n.is_string() ? n.get<std::string>() : "");
            if (!t) return a.id + ": bad damage type in " + key;
            mask |= rules5e::bit(*t);
        }
        return std::nullopt;
    };
    if (auto e = damage_mask("damage_resistances", c.resistances)) return e;
    if (auto e = damage_mask("damage_immunities", c.immunities)) return e;
    if (auto e = damage_mask("damage_vulnerabilities", c.vulnerabilities)) return e;
    if (auto it = doc.find("condition_immunities"); it != doc.end() && !inert.count("condition_immunities"))
        for (const Json& n : *it) {
            auto k = condition_of(n.is_string() ? n.get<std::string>() : "");
            if (!k) return a.id + ": bad condition in condition_immunities";
            c.condition_immunities |= rules5e::bit(*k);
        }
    const bool multi_damage = !inert.count("damage[1..]");
    // Size: stored for every version; the footprint (Large+ = 2x2..4x4) is monster@3 / character@3 only. Creature type and spellcasting: monster@3 / character@3.
    {
        static const char* sizes[] = {"tiny", "small", "medium", "large", "huge", "gargantuan"};
        const std::string sz = doc.value("size", "medium");
        for (int i = 0; i < 6; ++i) if (sz == sizes[i]) a.size = CreatureSize(i);
        a.footprint = inert.count("size") ? uint8_t(1) : footprint_of(a.size);
    }
    if (!inert.count("creature_type") && doc.contains("type")) {
        auto ct = creature_type_of(doc.value("type", ""));
        if (!ct) return a.id + ": bad creature type";
        c.creature_type = *ct;
    }
    // 0 = parse the document's own $schema tag (never "latest"); a missing, unparsable or other-type tag means no refusal.
    uint32_t declared = declared_version;
    if (declared == 0)
        if (auto s = doc.find("$schema"); s != doc.end() && s->is_string())
            if (auto ref = assets::parse_schema_ref(s->get<std::string>()); ref && ref->type == schema_type) declared = ref->version;   // another type's tag is not this document's version
    if (auto sc = doc.find("spellcasting"); sc != doc.end() && !inert.count("spellcasting")) {
        static const char* sc_abil[] = {"str", "dex", "con", "int", "wis", "cha"};
        const std::string sc_ab = sc->value("ability", "");
        bool ok = false;
        for (int i = 0; i < 6; ++i) if (sc_ab == sc_abil[i]) { c.spellcasting_ability = rules5e::Ability(i); ok = true; }
        if (!ok) return a.id + ": bad spellcasting ability";
        const int lvl = geti(*sc, "caster_level");
        if (lvl < 1 || lvl > 20) return a.id + ": bad caster_level";
        c.caster_level = uint8_t(lvl);
        if (auto sl = sc->find("slots"); sl != sc->end())
            for (auto it = sl->begin(); it != sl->end(); ++it) {
                const int level = std::atoi(it.key().c_str());
                if (level < 1 || level > 9 || !it.value().is_number_integer() || it.value().get<int>() < 1 || it.value().get<int>() > 9)
                    return a.id + ": bad spellcasting slots";
                c.spell_slots[std::size_t(level)] = c.spell_slots_max[std::size_t(level)] = uint8_t(it.value().get<int>());
            }
        if (auto pc = sc->find("pact"); pc != sc->end()) {
            c.pact_slot_level = uint8_t(geti(*pc, "level"));
            c.pact_slots = c.pact_slots_max = uint8_t(geti(*pc, "slots"));
            if (c.pact_slot_level < 1 || c.pact_slot_level > 5 || c.pact_slots < 1) return a.id + ": bad pact slots";
        }
        // monster@4 / character@4 `spellcasting` (VERSION_BUMP_BATCH row 8): `spells` is optional (an innate-only caster),
        // `dc` / `attack_bonus` override the derived values, `innate` lists spells cast without slots. A file that
        // declared < 4 cannot carry them (its schema refuses them); they are ignored, never half-applied.
        const bool v4 = declared == 0 || declared >= 4;
        if (auto sp = sc->find("spells"); sp != sc->end()) {
            if (!sp->is_array()) return a.id + ": bad spell id";
            for (const Json& e : *sp) {
                if (!e.is_string()) return a.id + ": bad spell id";
                a.spell_ids.push_back(e.get<std::string>());
                c.spells_known.push_back(rules5e::spell_id_of(a.spell_ids.back()));
            }
        } else if (!v4) {
            return a.id + ": bad spell id";   // @3 requires `spells`
        }
        if (v4) {
            for (const char* k : {"dc", "attack_bonus"})
                if (auto it = sc->find(k); it != sc->end() && (!it->is_number_integer() || it->get<int>() < 0 || it->get<int>() > 100))
                    return a.id + ": bad spellcasting " + k;
            c.spell_dc_override = int16_t(geti(*sc, "dc"));
            c.spell_attack_override = int16_t(geti(*sc, "attack_bonus"));
            if (auto in = sc->find("innate"); in != sc->end()) {
                if (!in->is_object()) return a.id + ": bad innate block";
                const bool as_listed = in->value("components", std::string("none")) == "as_listed";
                bool has_ab = false;
                rules5e::Ability innate_ab = rules5e::Ability::Cha;
                if (auto it = in->find("ability"); it != in->end()) {
                    const auto o = it->is_string() ? ability_of(it->get<std::string>()) : std::nullopt;
                    if (!o) return a.id + ": bad innate ability";
                    has_ab = true;
                    innate_ab = *o;
                }
                auto add_spell = [&](const Json& id, uint8_t uses_max, uint8_t uses_left, uint8_t recharge) -> std::optional<std::string> {
                    if (!id.is_string()) return a.id + ": bad innate spell id";
                    rules5e::InnateSpell is;
                    is.spell_id = rules5e::spell_id_of(id.get<std::string>());
                    is.uses_max = uses_max;
                    is.uses_left = uses_left;
                    is.recharge_min = recharge;
                    is.has_ability = has_ab;
                    is.ability = innate_ab;
                    is.as_listed = as_listed;
                    for (const auto& o : c.innate)
                        if (o.spell_id == is.spell_id) return a.id + ": innate spell '" + id.get<std::string>() + "' listed twice";
                    c.innate.push_back(is);
                    a.innate_spell_ids.push_back(id.get<std::string>());
                    return std::nullopt;
                };
                if (auto it = in->find("at_will"); it != in->end()) {
                    if (!it->is_array()) return a.id + ": bad innate at_will";
                    for (const Json& e : *it) if (auto er = add_spell(e, 0, 0, 0)) return *er;
                }
                if (auto it = in->find("per_day"); it != in->end()) {
                    if (!it->is_object()) return a.id + ": bad innate per_day";
                    for (auto pd = it->begin(); pd != it->end(); ++pd) {
                        const int n = std::atoi(pd.key().c_str());
                        if (n < 1 || n > 9 || !pd.value().is_array()) return a.id + ": bad innate per_day";
                        for (const Json& e : pd.value()) if (auto er = add_spell(e, uint8_t(n), uint8_t(n), 0)) return *er;
                    }
                }
                if (auto it = in->find("recharge"); it != in->end()) {
                    if (!it->is_array()) return a.id + ": bad innate recharge";
                    for (const Json& e : *it) {
                        const int min = e.is_object() ? geti(e, "min") : 0;
                        if (min < 2 || min > 6 || !e.contains("spell")) return a.id + ": bad innate recharge";
                        if (auto er = add_spell(e["spell"], 1, 1, uint8_t(min))) return *er;
                    }
                }
            }
        }
    }
    c.is_pc = c.uses_death_saves = (schema_type == "character");
    std::vector<const Json*> attack_actions;   // content action per c.attacks[i]
    for (const Json& act : doc.at("actions")) {
        const std::string kind = act.value("kind", "");
        const bool melee = kind == "melee_weapon_attack", ranged = kind == "ranged_weapon_attack";
        if (!melee && !ranged) continue;
        const Json& dmg = act.at("damage").at(0);
        auto dice = parse_dice(dmg.value("dice", ""));
        auto type = damage_type(dmg.value("type", ""));
        if (!dice || !type) return a.id + ": bad damage in action " + act.value("id", "?");
        strings_.push_back(act.value("id", ""));
        rules5e::Attack at{};
        at.id = strings_.back();
        at.to_hit = int8_t(geti(act, "to_hit"));
        at.damage = *dice;
        at.type = *type;
        at.ranged = ranged;
        at.finesse = false;
        // R-G7: only the grip bits Great Weapon Fighting reads (two_handed, versatile) are carried; other properties (loading,
        // heavy, finesse, ...) stay off so existing behaviour and goldens do not move.
        if (auto ps = act.find("properties"); ps != act.end() && ps->is_array())
            for (const Json& pn : *ps)
                if (auto bitv = rules5e::weapon_property_from_string(pn.is_string() ? pn.get<std::string>() : std::string()))
                    at.properties = uint16_t(at.properties | (*bitv & (rules5e::kWeaponTwoHanded | rules5e::kWeaponVersatile)));
        // Weapon data qualifiers (magical / silvered / adamantine): monster@3 / character@3 only.
        if (!inert.count("attack_qualifiers"))
            if (auto qs = act.find("qualifiers"); qs != act.end() && qs->is_array())
                for (const Json& n : *qs) {
                    const std::string qn = n.is_string() ? n.get<std::string>() : "";
                    if (qn == "magical") at.qualifiers |= rules5e::kAttackMagical;
                    else if (qn == "silvered") at.qualifiers |= rules5e::kAttackSilvered;
                    else if (qn == "adamantine") at.qualifiers |= rules5e::kAttackAdamantine;
                    else return a.id + ": bad qualifier '" + qn + "' in action " + act.value("id", "?");
                }
        if (multi_damage)   // components 2..n (monster@3 / character@3)
            for (std::size_t i = 1; i < act.at("damage").size(); ++i) {
                const Json& extra = act.at("damage").at(i);
                auto xd = parse_dice(extra.value("dice", ""));
                auto xt = damage_type(extra.value("type", ""));
                if (!xd || !xt) return a.id + ": bad damage in action " + act.value("id", "?");
                at.extra_damage.push_back({*xd, *xt});
            }
        int normal = 0, lng = 0;
        if (ranged) {
            const Json& rg = act.at("range");
            normal = rg.at(0).get<int>();
            lng = rg.size() > 1 ? rg.at(1).get<int>() : normal;
            if (normal <= 0 || lng < normal || lng > 65535) return a.id + ": bad range in action " + act.value("id", "?");
        }
        at.reach_or_range_ft = uint8_t(melee ? geti(act, "reach", 5) : std::min(normal, 255));
        if (ranged) { at.range_ft = uint16_t(normal); at.long_range_ft = uint16_t(lng); }
        c.attacks.push_back(at);
        a.normal_range_ft.push_back(uint16_t(normal));
        a.long_range_ft.push_back(uint16_t(lng));
        attack_actions.push_back(&act);
    }
    if (declared == 0 || declared >= 4)
        if (auto e = compile_resources(a, doc, schema_type == "character", c)) return *e;
    // Feature hooks (data-driven by effect.kind; see features.hpp).
    a.features = declared_features(doc);
    std::vector<rules5e::SpellDef> synth;   // synthetic SpellDefs: area_save riders first, then save_effect actions
    const RiderCtx rider_ctx{a.id, attack_actions, synth};
    std::map<uint32_t, std::string> trigger_ids;   // numeric trigger id -> its string (duplicates are load errors)
    const FeatureBuildCtx fctx{*features_, doc, declared, attack_actions};
    for (std::size_t i = 0; i < a.features.size(); ++i) {
        const FeatureDecl& f = a.features[i];
        // Schema-version gate (docs/CONTENT_COMPAT.md): effects from pre-character@3 files are
        // marked inert by the 2 -> 3 migration; their numbers are already baked in.
        if (!f.kind.empty() && f.effect.value("inert", false)) {
            a.inert_features.push_back(f.id + " (" + f.kind + ")");
            continue;
        }
        const FeatureHook* h = f.kind.empty() ? nullptr : features_->hook(f.kind);
        // Defence in depth for the migration marker: a file that declared a version older than the one that
        // made this kind mechanical is listed inert (hook or not), never an error.
        if (!f.kind.empty() && declared != 0 && declared < features_->min_schema_version(f.kind)) {
            a.inert_features.push_back(f.id + " (" + f.kind + ")");
            continue;
        }
        if (f.kind == "on_event") {   // event triggers (rules5e::Trigger): compiled here, the registry reserves the kind only
            bool unsupported = false;
            if (auto e = compile_on_event(a.id, f.id, f.effect, doc, rider_ctx, trigger_ids, c, unsupported)) return *e;
            if (unsupported) a.unsupported_features.push_back(f.id + " (on_event: unsupported usage)");
            continue;
        }
        if (!h) {
            a.unsupported_features.push_back(f.id + " (" + (f.kind.empty() ? "no effect" : f.kind) + ")");
            continue;
        }
        if (auto e = h->apply(a, f, uint32_t(i + 1), fctx)) return a.id + ": " + *e;
    }
    // on_hit[] riders on weapon attacks (monster@4 / character@4): one trigger per rider, bound to its attack by an AttackIs
    // predicate so it fires only on that attack's hits. Owner = the action id, index = position in on_hit[].
    if (declared == 0 || declared >= 4)
        for (std::size_t ai = 0; ai < attack_actions.size(); ++ai) {
            const Json& act = *attack_actions[ai];
            auto oh = act.find("on_hit");
            if (oh == act.end()) continue;
            if (!oh->is_array()) return a.id + ": bad on_hit";
            for (std::size_t ri = 0; ri < oh->size(); ++ri) {
                if ((*oh)[ri].is_object() && (*oh)[ri].value("kind", "") == "grapple") {   // R-G8: sim applies it on the hit
                    const Json& g = (*oh)[ri];
                    const int dc = g.contains("escape_dc") ? (g["escape_dc"].is_number_integer() ? g["escape_dc"].get<int>() : -1) : 0;
                    if (dc < 0 || dc > 30) return a.id + ": " + act.value("id", "?") + " on_hit: bad grapple escape_dc";
                    a.grapple_on_hit[uint8_t(ai)] = int16_t(dc);
                    continue;
                }
                rules5e::Trigger t;
                t.on = rules5e::TriggerOn::Hit;
                rules5e::Predicate pr;
                pr.kind = rules5e::PredicateKind::AttackIs;
                pr.a = int16_t(ai);
                t.when.push_back(pr);
                if (auto e = compile_rider(a.id + ": " + act.value("id", "?") + " on_hit", (*oh)[ri], doc, rider_ctx, t.effects)) return *e;
                if (auto e = add_trigger(a.id, act.value("id", "?"), ri, std::move(t), trigger_ids, c)) return *e;
            }
        }
    // reactions[] (monster@4 / character@4; older files carry them stamped inert)
    if (declared == 0 || declared >= 4)
        if (auto e = compile_reactions(a, doc, rider_ctx, c)) return *e;
    if (declared == 0 || declared >= 4)
        if (auto e = compile_bonus_attacks(a.id, a.features, attack_actions, schema_type == "character", doc, c)) return *e;
    // multiattack options -> rules5e::AttackSequence (ids owned by the library, stable addresses).
    for (const auto& m : a.multiattack_decls) {
        strings_.push_back(m.id);
        c.multiattacks.push_back({strings_.back(), m.attacks});
    }
    a.ai = assets::behavior_ai_script(doc);   // monster@2 / character@2
    const std::size_t n_trigger_spells = synth.size();   // the area_save riders' defs come first
    for (std::size_t i = 0; i < n_trigger_spells; ++i) a.trigger_spell_ids.push_back(synth[i].id);
    // monster@4 / character@4 save_effect actions -> synthetic SpellDefs (never for a file that declared < 4).
    if (declared == 0 || declared >= 4)
        for (const Json& act : doc.at("actions")) {
            if (act.value("kind", "") != "save_effect") continue;
            rules5e::SpellDef d;
            Archetype::SaveAction sa;
            bool unsupported = false;
            if (auto e = compile_save_action(a.id, act, d, sa, doc, unsupported)) return e;
            if (unsupported) { a.unsupported_features.push_back(act.value("id", "?") + " (save_effect: unsupported usage or condition duration)"); continue; }
            for (const auto& prev : synth)
                if (prev.id == d.id) return a.id + ": save_effect ids '" + prev.content_id + "' and '" + d.content_id + "' collide";
            c.spells_known.push_back(d.id);
            a.save_actions.push_back(sa);
            synth.push_back(std::move(d));
        }
    // Register in the shared SpellBook, replacing an overridden archetype's entries; all-or-nothing.
    if (!synth.empty() || by_id_.count(a.id)) {
        std::vector<rules5e::SpellDef> old;
        if (auto prev = by_id_.find(a.id); prev != by_id_.end()) {
            for (const auto& sa : prev->second.save_actions)
                if (const auto* d = spells_->find(sa.spell_id)) old.push_back(*d);
            for (uint32_t id : prev->second.trigger_spell_ids)
                if (const auto* d = spells_->find(id)) old.push_back(*d);
        }
        for (const auto& d : old) spells_->erase(d.id);
        std::vector<uint32_t> added;
        for (const auto& d : synth) {
            if (const auto* clash = spells_->find(d.id)) {
                const std::string other = clash->content_id;
                for (uint32_t id : added) spells_->erase(id);
                for (const auto& o : old) spells_->add(o);
                return a.id + ": synthetic spell id of '" + d.content_id + "' collides with '" + other + "'";
            }
            spells_->add(d);
            added.push_back(d.id);
        }
    }
    // R-G7 v0 wield model (sim has no equipment state): a versatile melee weapon of a stat block that carries no shield is
    // held in two hands (the AI also requires no two-weapon bonus option, see driver.cpp).
    if (!carries_shield(doc))
        for (std::size_t i = 0; i < c.attacks.size() && i < 32; ++i)
            if (!c.attacks[i].ranged && rules5e::has_property(c.attacks[i], rules5e::kWeaponVersatile)) a.two_handed_grip |= 1u << i;
    by_id_[a.id] = std::move(a);
    return std::nullopt;
}

std::optional<std::string> ContentLibrary::add_spell(const Json& doc) {
    auto r = rules5e_content::spell_from_json(doc);
    if (!r.def) return r.error;
    std::string e;
    if (!rules5e_content::add_spell(*spells_, std::move(*r.def), e)) return e;
    return std::nullopt;
}

std::vector<std::string> ContentLibrary::load(const assets::PackSet& packs) {
    if (!packs.ok()) return packs.error_strings();
    std::vector<std::string> errors = packs.document_errors();
    for (const assets::PackDocument& d : packs.documents()) {
        if (!d.doc.ok || d.doc.current.type != "spell") continue;
        const std::string& pack_id = packs.packs()[d.pack].id;
        try {
            if (auto e = add_spell(d.doc.data)) errors.push_back(pack_id + ":" + d.path + ": " + *e);
        } catch (const std::exception& ex) {
            errors.push_back(pack_id + ":" + d.path + ": " + ex.what());
        }
    }
    for (const assets::PackDocument& d : packs.documents()) {
        if (!d.doc.ok || (d.doc.current.type != "monster" && d.doc.current.type != "character")) continue;
        const std::string& pack_id = packs.packs()[d.pack].id;
        try {
            if (auto e = add(d.doc.data, d.doc.current.type, pack_id, d.doc.declared.version)) errors.push_back(pack_id + ":" + d.path + ": " + *e);
        } catch (const std::exception& ex) {
            errors.push_back(pack_id + ":" + d.path + ": " + ex.what());
        }
    }
    // Spellcasting references must resolve to a loaded spell@1 (same rule as the validator).
    for (const auto& [id, a] : by_id_)
        {
            for (const std::string& sid : a.spell_ids) {
                const rules5e::SpellDef* d = spells_->find(rules5e::spell_id_of(sid));
                if (!d || d->content_id != sid) errors.push_back(a.pack_id + ":" + id + ": spellcasting.spells '" + sid + "' is not a loaded spell@1");
            }
            for (const std::string& sid : a.innate_spell_ids) {
                const rules5e::SpellDef* d = spells_->find(rules5e::spell_id_of(sid));
                if (!d || d->content_id != sid) errors.push_back(a.pack_id + ":" + id + ": spellcasting.innate '" + sid + "' is not a loaded spell@1");
            }
        }
    return errors;
}

std::vector<std::string> ContentLibrary::load_pack(const std::string& pack_dir) {
    return load(assets::PackSet::from_pack_dir(pack_dir));
}

const Archetype* ContentLibrary::find(const std::string& id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &it->second;
}

} // namespace eng::sim
