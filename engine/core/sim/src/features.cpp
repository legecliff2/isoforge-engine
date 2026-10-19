#include "sim/features.hpp"

#include <algorithm>
#include <cctype>

#include "rules5e/armor.hpp"

#include "rules5e_content/spell_loader.hpp"
#include "sim/content.hpp"

namespace eng::sim {

using assets::Json;

namespace {
bool has_property(const Json& action, const char* p) {
    auto it = action.find("properties");
    if (it == action.end() || !it->is_array()) return false;
    for (const auto& v : *it)
        if (v.is_string() && v.get_ref<const std::string&>() == p) return true;
    return false;
}

// "fighter_level" -> that class's level; "level" -> total level; integer -> itself.
std::optional<int> resolve_amount(const Json& doc, const Json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (!v.is_string()) return std::nullopt;
    const std::string& s = v.get_ref<const std::string&>();
    if (s == "level") return doc.value("level", 0);
    const std::string suffix = "_level";
    if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string cls = s.substr(0, s.size() - suffix.size());
        int lvl = 0;
        for (const auto& c : doc.value("classes", Json::array()))
            if (c.value("class", "") == cls) lvl += c.value("level", 0);
        return lvl;
    }
    return std::nullopt;
}

std::optional<std::string> apply_damage_bonus(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx& ctx) {
    const auto amount = f.effect.find("amount");
    if (amount == f.effect.end() || !amount->is_number_integer()) return f.id + ": damage_bonus needs integer amount";
    const std::string cond = f.effect.value("condition", "");
    const ActionPredicate* pred = cond.empty() ? nullptr : ctx.registry.condition(cond);
    if (!cond.empty() && !pred) return f.id + ": unknown condition '" + cond + "'";
    for (std::size_t i = 0; i < a.base.attacks.size(); ++i)
        if (!pred || (*pred)(ctx.doc, *ctx.attack_actions[i]))
            a.base.attacks[i].damage.bonus = int16_t(a.base.attacks[i].damage.bonus + amount->get<int>());
    return std::nullopt;
}

std::optional<std::string> apply_bonus_action_grant(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx&) {
    const auto acts = f.effect.find("actions");
    if (acts == f.effect.end() || !acts->is_array()) return f.id + ": bonus_action_grant needs actions[]";
    for (const auto& v : *acts) {
        const std::string s = v.is_string() ? v.get<std::string>() : "";
        rules5e::ActionKind k;
        if (s == "dash") k = rules5e::ActionKind::Dash;
        else if (s == "disengage") k = rules5e::ActionKind::Disengage;
        else if (s == "hide") k = rules5e::ActionKind::Hide;
        else if (s == "dodge") k = rules5e::ActionKind::Dodge;
        else return f.id + ": bonus_action_grant: unsupported action '" + s + "'";
        a.base.bonus_action_grants |= rules5e::bit(k);
    }
    return std::nullopt;
}

// Multiattack (static): declared options, each an ordered list of this creature's attack action ids made
// with one Attack action. `options: [{id, attacks: [...]}]`, or the shorthand `attacks: [...]` = one option
// named "multiattack". Option order is the order content declares them (the AI prefers earlier ones).
std::optional<std::string> apply_multiattack(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx& ctx) {
    std::vector<std::pair<std::string, const Json*>> opts;
    if (auto o = f.effect.find("options"); o != f.effect.end()) {
        if (!o->is_array() || o->empty()) return f.id + ": multiattack options must be a non-empty array";
        for (const Json& x : *o) {
            if (!x.is_object() || !x.contains("id") || !x["id"].is_string()) return f.id + ": multiattack option needs an id";
            opts.emplace_back(x["id"].get<std::string>(), &x["attacks"]);
        }
    } else if (auto at = f.effect.find("attacks"); at != f.effect.end()) {
        opts.emplace_back("multiattack", &*at);
    } else {
        return f.id + ": multiattack needs options[] or attacks[]";
    }
    for (const auto& [oid, list] : opts) {
        if (oid.empty()) return f.id + ": multiattack option id is empty";
        for (const auto& d : a.multiattack_decls)
            if (d.id == oid) return f.id + ": duplicate multiattack option '" + oid + "'";
        if (!list->is_array() || list->empty() || list->size() > 255)
            return f.id + ": multiattack option '" + oid + "' needs 1..255 attacks";
        Archetype::MultiattackDecl d;
        d.id = oid;
        for (const Json& n : *list) {
            const std::string aid = n.is_string() ? n.get<std::string>() : "";
            bool found = false;
            for (std::size_t i = 0; i < ctx.attack_actions.size() && !found; ++i)
                if (ctx.attack_actions[i]->value("id", "") == aid) { d.attacks.push_back(uint8_t(i)); found = true; }
            if (!found) return f.id + ": multiattack option '" + oid + "' names unknown attack action '" + aid + "'";
        }
        a.multiattack_decls.push_back(std::move(d));
    }
    return std::nullopt;
}

// Qualified resistances / immunities ("from nonmagical attacks [that aren't silvered / adamantine]").
std::optional<std::string> mitigation_masks(const FeatureDecl& f, const char* key, rules5e::QualifiedDamageMask& m) {
    auto it = f.effect.find(key);
    if (it == f.effect.end()) return std::nullopt;
    if (!it->is_object()) return f.id + ": damage_mitigation " + key + " must be an object";
    for (const auto& [qual, list] : it->items()) {
        uint32_t* mask = qual == "nonmagical"                   ? &m.nonmagical
                         : qual == "nonmagical_not_silvered"    ? &m.nonmagical_not_silvered
                         : qual == "nonmagical_not_adamantine"  ? &m.nonmagical_not_adamantine
                                                                 : nullptr;
        if (!mask) return f.id + ": damage_mitigation: unknown qualifier '" + qual + "'";
        if (!list.is_array()) return f.id + ": damage_mitigation " + qual + " must be an array of damage types";
        for (const auto& n : list) {
            auto t = damage_type_from_name(n.is_string() ? n.get<std::string>() : "");
            if (!t) return f.id + ": damage_mitigation: bad damage type in " + qual;
            *mask |= rules5e::bit(*t);
        }
    }
    return std::nullopt;
}

std::optional<std::string> apply_damage_mitigation(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx&) {
    if (!f.effect.contains("resistances") && !f.effect.contains("immunities"))
        return f.id + ": damage_mitigation needs resistances and/or immunities";
    if (auto e = mitigation_masks(f, "resistances", a.base.resist_qualified)) return e;
    return mitigation_masks(f, "immunities", a.base.immune_qualified);
}

// Magic Weapons: every attack counts as magical (bypasses "nonmagical" resistance / immunity).
std::optional<std::string> apply_magic_weapons(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    for (auto& at : a.base.attacks) at.qualifiers |= rules5e::kAttackMagical;
    return std::nullopt;
}

std::optional<std::string> apply_self_heal(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx& ctx) {
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    const std::string dice = f.effect.value("dice", "");
    auto d = parse_dice(dice);
    if (!d) return f.id + ": self_heal needs dice";
    if (auto p = f.effect.find("plus"); p != f.effect.end()) {
        auto plus = resolve_amount(ctx.doc, *p);
        if (!plus) return f.id + ": self_heal: cannot resolve plus";
        d->bonus = int16_t(d->bonus + *plus);
    }
    af.heal = *d;
    const std::string cost = f.effect.value("cost", "action");
    if (cost == "bonus_action") af.cost = FeatureCost::BonusAction;
    else if (cost == "reaction")
        return f.id + ": self_heal cost 'reaction' is reserved, not supported by this engine (use action or bonus_action)";
    else if (cost != "action") return f.id + ": unknown cost '" + cost + "'";
    af.uses = uint8_t(f.effect.value("uses", 0));
    // recharge: omitted = never; otherwise short_rest | long_rest (CONTENT_EFFECTS.md). Unknown values
    // are errors rather than silently "never".
    if (auto rc_it = f.effect.find("recharge"); rc_it != f.effect.end()) {
        const std::string rc = rc_it->is_string() ? rc_it->get<std::string>() : std::string("<non-string>");
        if (rc == "short_rest") af.recharge = Recharge::ShortRest;
        else if (rc == "long_rest") af.recharge = Recharge::LongRest;
        else return f.id + ": unknown recharge '" + rc + "' (expected short_rest or long_rest; omit for never)";
    }
    a.active.push_back(std::move(af));
    return std::nullopt;
}

// apply_modifier (row 12): Rage, Bardic Inspiration token, Leadership. `modifier` or `modifiers[]` compile to rows the
// feature_modifiers hook hands to rules when the feature is used (UseFeature). A damage `bonus_table` resolves against
// the owner's level in `class` (else the total level). usage kind "pool" is row 13's; other usage shapes are listed
// as unsupported and the feature is not offered.
std::optional<std::string> apply_apply_modifier(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx& ctx) {
    std::vector<const Json*> mods;
    if (auto m = f.effect.find("modifier"); m != f.effect.end()) mods.push_back(&*m);
    if (auto ms = f.effect.find("modifiers"); ms != f.effect.end() && ms->is_array())
        for (const Json& m : *ms) mods.push_back(&m);
    if (mods.empty() || mods.size() > 16) return f.id + ": apply_modifier needs modifier or 1..16 modifiers[]";
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    const std::string cost = f.effect.value("cost", "action");
    if (cost == "bonus_action") af.cost = FeatureCost::BonusAction;
    else if (cost != "action") return f.id + ": apply_modifier cost '" + cost + "' is not supported (use action or bonus_action)";
    if (f.effect.contains("usage")) {
        a.unsupported_features.push_back(f.id + " (apply_modifier: usage needs resource pools)");
        return std::nullopt;
    }
    std::vector<rules5e::ModifierSpec> rows;
    const uint32_t source = rules5e::spell_id_of(a.id + "#" + f.id);
    for (std::size_t i = 0; i < mods.size(); ++i) {
        rules5e::ModifierSpec m;
        const Json* table = nullptr;
        if (auto e = rules5e_content::parse_modifier(*mods[i], m, &table); !e.empty()) return f.id + ": " + e;
        if (table) {
            if (m.stat != rules5e::ModStat::Damage) return f.id + ": bonus_table only applies to damage modifiers";
            int level = 0;
            const std::string cls = table->is_object() ? table->value("class", "") : "";
            for (const Json& c : ctx.doc.value("classes", Json::array()))
                if (c.value("class", "") == cls) level += c.value("level", 0);
            if (level == 0 && cls.empty()) level = ctx.doc.value("level", 0);
            if (level < 1) return f.id + ": bonus_table has no level to resolve against";
            const auto bl = table->find("by_level");
            if (bl == table->end() || !bl->is_array()) return f.id + ": bonus_table needs by_level";
            int best_level = 0, value = 0;
            for (const Json& e : *bl)
                if (e.is_object() && e.value("level", 0) <= level && e.value("level", 0) >= best_level) {
                    best_level = e.value("level", 0);
                    value = e.value("value", 0);
                }
            if (best_level == 0) return f.id + ": bonus_table has no entry for level " + std::to_string(level);
            m.bonus = int16_t(value);
        }
        m.source = source;
        m.id = rules5e::spell_id_of(a.id + "#" + f.id + "#m" + std::to_string(i));
        rows.push_back(m);
    }
    a.feature_modifiers[fid] = std::move(rows);
    a.active.push_back(std::move(af));
    return std::nullopt;
}

// R-G5 static feature kinds (character@4 / monster@4).
// attack_bonus {attacks: "ranged" | "melee", amount}: weapon attack rolls (Archery +2 ranged); several sum.
std::optional<std::string> apply_attack_bonus(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx&) {
    const std::string which = f.effect.value("attacks", "");
    const auto amt = f.effect.find("amount");
    if (amt == f.effect.end() || !amt->is_number_integer()) return f.id + ": attack_bonus needs integer amount";
    int8_t& slot = which == "ranged" ? a.base.attack_bonus_ranged : a.base.attack_bonus_melee;
    if (which != "ranged" && which != "melee") return f.id + ": attack_bonus attacks must be 'ranged' or 'melee'";
    const int v = int(slot) + amt->get<int>();
    if (v < -20 || v > 20) return f.id + ": attack_bonus out of range";
    slot = int8_t(v);
    return std::nullopt;
}
// crit_range {min}: Improved Critical 19, Superior Critical 18; the lowest wins.
std::optional<std::string> apply_crit_range(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx&) {
    const int m = f.effect.value("min", 0);
    if (m < 2 || m > 20) return f.id + ": crit_range min must be 2..20";
    if (m < a.base.crit_min) a.base.crit_min = int8_t(m);
    return std::nullopt;
}
// ac_bonus {amount, requires_armor}: rules5e::compute_ac with AcInput::armored_bonus (requires_armor) or flat_bonus; the
// difference is added to the content AC. "Wearing armor" (sim has no equipment model): equipment[] names an SRD armor item, or
// armor_class.note names one (e.g. "chain mail, shield"); "natural armor" does not count.
bool wears_armor(const Json& doc) {
    static const char* kArmor[] = {"padded", "leather", "studded_leather", "hide", "chain_shirt", "scale_mail", "breastplate",
                                   "half_plate", "ring_mail", "chain_mail", "splint", "plate"};
    if (auto eq = doc.find("equipment"); eq != doc.end() && eq->is_array())
        for (const Json& e : *eq) {
            const std::string id = e.is_string() ? e.get<std::string>() : (e.is_object() ? e.value("id", "") : "");
            for (const char* k : kArmor) if (id == k || id == std::string(k) + "_armor") return true;
        }
    std::string note;
    if (auto ac = doc.find("armor_class"); ac != doc.end() && ac->is_object()) note = ac->value("note", "");
    for (char& ch : note) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (note.find("natural armor") != std::string::npos) return false;
    for (const char* k : kArmor) {
        std::string n = k;
        for (char& ch : n) if (ch == '_') ch = ' ';
        if (note.find(n) != std::string::npos) return true;
    }
    return false;
}
std::optional<std::string> apply_ac_bonus(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx& ctx) {
    const auto amt = f.effect.find("amount");
    if (amt == f.effect.end() || !amt->is_number_integer() || amt->get<int>() < -10 || amt->get<int>() > 10)
        return f.id + ": ac_bonus needs integer amount (-10..10)";
    rules5e::AcInput in;
    in.abilities = a.base.abilities;
    in.has_armor = wears_armor(ctx.doc);
    const int before = rules5e::compute_ac(in).ac;
    if (f.effect.value("requires_armor", false)) in.armored_bonus = int16_t(amt->get<int>());
    else in.flat_bonus = int16_t(amt->get<int>());
    const int ac = int(a.base.ac) + rules5e::compute_ac(in).ac - before;
    if (ac < 0 || ac > 127) return f.id + ": ac_bonus puts AC out of range";
    a.base.ac = int8_t(ac);
    return std::nullopt;
}

FeatureRegistry make_builtin() {
    FeatureRegistry r;
    r.add_hook("damage_bonus", {apply_damage_bonus});
    r.add_hook("bonus_action_grant", {apply_bonus_action_grant});
    r.add_hook("self_heal", {apply_self_heal});
    r.add_hook("multiattack", {apply_multiattack});
    r.add_hook("damage_mitigation", {apply_damage_mitigation});
    r.add_hook("magic_weapons", {apply_magic_weapons});
    r.add_hook("apply_modifier", {apply_apply_modifier}, 4);
    r.add_hook("attack_bonus", {apply_attack_bonus});
    r.add_hook("crit_range", {apply_crit_range});
    r.add_hook("ac_bonus", {apply_ac_bonus});
    // Kinds the @4 schemas introduce (frozen list of the 3 -> 4 migration; hooks arrive with their rows).
    for (const char* k : {"on_event", "grant_action", "heal_pool", "save_effect", "reaction"})
        r.reserve_kind(k, 4);
    // v0 wield model: a melee weapon without the two_handed property counts as wielded in one hand
    // with no other weapon (sim has no equipment/wield state yet).
    r.add_condition("one_handed_melee_no_other_weapon", [](const Json&, const Json& act) {
        return act.value("kind", "") == "melee_weapon_attack" && !has_property(act, "two_handed");
    });
    r.add_condition("melee_weapon", [](const Json&, const Json& act) { return act.value("kind", "") == "melee_weapon_attack"; });
    r.add_condition("ranged_weapon", [](const Json&, const Json& act) { return act.value("kind", "") == "ranged_weapon_attack"; });
    return r;
}
} // namespace

const FeatureHook* FeatureRegistry::hook(const std::string& kind) const {
    auto it = hooks_.find(kind);
    return it == hooks_.end() ? nullptr : &it->second;
}
const ActionPredicate* FeatureRegistry::condition(const std::string& id) const {
    auto it = conditions_.find(id);
    return it == conditions_.end() ? nullptr : &it->second;
}
std::vector<std::string> FeatureRegistry::kinds() const {
    std::vector<std::string> out;
    for (const auto& [k, h] : hooks_) out.push_back(k);
    return out;
}
const FeatureRegistry& FeatureRegistry::builtin() {
    static const FeatureRegistry r = make_builtin();
    return r;
}

std::vector<FeatureDecl> declared_features(const Json& doc) {
    std::vector<FeatureDecl> out;
    for (const char* key : {"features", "traits"}) {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_array()) continue;
        for (const auto& f : *it) {
            FeatureDecl d;
            d.id = f.value("id", "");
            if (auto e = f.find("effect"); e != f.end() && e->is_object()) {
                d.effect = *e;
                d.kind = e->value("kind", "");
            }
            out.push_back(std::move(d));
        }
    }
    return out;
}

} // namespace eng::sim
