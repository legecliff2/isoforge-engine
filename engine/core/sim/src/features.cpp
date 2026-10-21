#include "sim/features.hpp"

#include <algorithm>
#include <cctype>

#include "rules5e/armor.hpp"
#include "rules5e/font_of_magic.hpp"

#include "rules5e_content/spell_loader.hpp"
#include "sim/content.hpp"

namespace eng::sim {

std::string equipment_entry_id(const assets::Json& entry) {
    if (entry.is_string()) return entry.get<std::string>();
    if (!entry.is_object()) return {};
    if (auto it = entry.find("item"); it != entry.end() && it->is_string()) return it->get<std::string>();
    if (auto jt = entry.find("id"); jt != entry.end() && jt->is_string()) return jt->get<std::string>();
    return {};
}

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
        else if (s == "use_object") k = rules5e::ActionKind::UseObject;   // 7.2y Fast Hands (UseObject 1..3)
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

// Row 13: `cost` and `usage` of a grant_action / heal_pool effect -> the ActiveFeature and its rules5e::FeatureSpec.
// usage: at_will (none) | per_day / per_rest (uses, recharge) | pool (declared in resources[]; an `unlimited` pool
// leaves FeatureSpec::pool = 0 so nothing is spent). cost: action (default) | bonus_action | free; reaction is not offered.
std::optional<std::string> fill_spec(Archetype& a, const FeatureDecl& f, ActiveFeature& af, rules5e::FeatureSpec& spec) {
    const std::string cost = f.effect.value("cost", "action");
    if (cost == "action") { af.cost = FeatureCost::Action; spec.cost = 1; }
    else if (cost == "bonus_action") { af.cost = FeatureCost::BonusAction; spec.cost = 2; }
    else if (cost == "free") { af.cost = FeatureCost::Free; spec.cost = 4; }
    else return f.id + ": " + f.kind + " cost '" + cost + "' is not supported (use action, bonus_action or free)";
    const auto u = f.effect.find("usage");
    if (u == f.effect.end()) return std::nullopt;
    if (!u->is_object()) return f.id + ": bad usage";
    const std::string k = u->value("kind", "");
    if (k == "at_will") return std::nullopt;
    if (k == "per_day" || k == "per_rest") {
        const int uses = u->value("uses", 0);
        if (uses < 1 || uses > 255) return f.id + ": bad uses";
        af.uses = uint8_t(uses);
        af.recharge = k == "per_day" || u->value("rest", "long") == "long" ? Recharge::LongRest : Recharge::ShortRest;
        return std::nullopt;
    }
    if (k == "pool") {
        const std::string pid = u->value("pool", "");
        const int pc = u->value("cost", 1);
        if (pc < 1 || pc > 65535) return f.id + ": bad pool cost";
        bool unlimited = false, found = false;
        for (const auto& r : a.resources) found = found || r.id == pid;
        for (const auto& r : a.unlimited_resources) unlimited = unlimited || r == pid;
        if (!found && !unlimited) return f.id + ": usage names unknown pool '" + pid + "'";
        if (!unlimited) { spec.pool = rules5e::spell_id_of(pid); spec.pool_cost = uint16_t(pc); }
        return std::nullopt;
    }
    return f.id + ": unsupported usage kind '" + k + "'";
}

// grant_action (Action Surge): UseFeature adds `count` extra actions to the turn's budget.
std::optional<std::string> apply_grant_action(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx&) {
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    rules5e::FeatureSpec spec;
    if (auto e = fill_spec(a, f, af, spec)) return e;
    const int count = f.effect.value("count", 1);
    if (count < 1 || count > 2) return f.id + ": grant_action count must be 1 or 2";
    spec.grant_actions = uint8_t(count);
    a.feature_specs[fid] = spec;
    a.active.push_back(std::move(af));
    return std::nullopt;
}

// heal_pool (Lay on Hands): the caller / AI sets ActionRequest::amount and target; the pool is charged the amount.
std::optional<std::string> apply_heal_pool(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx&) {
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    rules5e::FeatureSpec spec;
    if (auto e = fill_spec(a, f, af, spec)) return e;
    const std::string pid = f.effect.value("pool", "");
    bool found = false;
    for (const auto& r : a.resources) found = found || r.id == pid;
    if (!found) return f.id + ": heal_pool names unknown (or unlimited) pool '" + pid + "'";
    spec.pool = rules5e::spell_id_of(pid);
    spec.pool_cost = 0;
    spec.heal_pool = true;
    a.feature_specs[fid] = spec;
    a.active.push_back(std::move(af));
    return std::nullopt;
}

// apply_modifier (row 12): Rage, Bardic Inspiration token, Leadership. `modifier` or `modifiers[]` compile to rows the
// feature_modifiers hook hands to rules when the feature is used (UseFeature). A damage `bonus_table` resolves against
// the owner's level in `class` (else the total level). usage kind "pool" is row 13's; other usage shapes are listed
// as unsupported and the feature is not offered.
// 7.2z helpers: closed string-enum arrays (unique items) -> bitmask; false on a bad / repeated entry.
bool enum_bits(const Json& arr, std::initializer_list<const char*> names, uint32_t& out) {
    out = 0;
    if (!arr.is_array() || arr.empty()) return false;
    for (const Json& v : arr) {
        const std::string s = v.is_string() ? v.get<std::string>() : "";
        uint32_t i = 0, hit = 0;
        for (const char* n : names) { if (s == n) hit = 1u << i; ++i; }
        if (hit == 0 || (out & hit)) return false;
        out |= hit;
    }
    return true;
}
constexpr std::initializer_list<const char*> kAbilities7z = {"str", "dex", "con", "int", "wis", "cha"};
constexpr std::initializer_list<const char*> kConditions7z = {"blinded", "charmed", "deafened", "frightened", "grappled",
    "incapacitated", "invisible", "paralyzed", "petrified", "poisoned", "prone", "restrained", "stunned", "unconscious", "exhaustion"};

// 7.2z Danger Sense: apply_modifier {passive: true, advantage_on: {saves: [..]}, when: {save, source_seen?, not_conditions?}}:
// a static Combatant::save_advantages entry per listed save; nothing to activate.
std::optional<std::string> apply_passive_advantage(Archetype& a, const FeatureDecl& f) {
    for (const char* k : {"modifier", "modifiers", "cost", "usage", "duration", "scope", "ends_early", "blocks", "on_end"})
        if (f.effect.contains(k)) return f.id + ": passive: '" + std::string(k) + "' is not allowed";
    const auto ao = f.effect.find("advantage_on");
    const auto w = f.effect.find("when");
    if (ao == f.effect.end() || !ao->is_object() || !ao->contains("saves") || ao->contains("checks") || ao->size() != 1)
        return f.id + ": passive needs advantage_on {saves} only";
    uint32_t saves = 0, when_save = 0, notc = 0;
    if (!enum_bits((*ao)["saves"], kAbilities7z, saves)) return f.id + ": bad advantage_on.saves";
    if (w == f.effect.end() || !w->is_object()) return f.id + ": passive needs when";
    for (auto it = w->begin(); it != w->end(); ++it)
        if (it.key() != "save" && it.key() != "source_seen" && it.key() != "not_conditions") return f.id + ": when: unknown key '" + it.key() + "'";
    if (!enum_bits(Json::array({w->value("save", "")}), kAbilities7z, when_save) || !(saves & when_save))
        return f.id + ": when.save must be one of advantage_on.saves";
    if (auto nc = w->find("not_conditions"); nc != w->end() && !enum_bits(*nc, kConditions7z, notc)) return f.id + ": bad when.not_conditions";
    const auto ss = w->find("source_seen");
    if (ss != w->end() && !ss->is_boolean()) return f.id + ": bad when.source_seen";
    rules5e::SaveAdvantage sa;
    for (int i = 0; i < 6; ++i) if (when_save & (1u << i)) sa.ability = rules5e::Ability(i);
    sa.source_seen = ss == w->end() ? false : ss->get<bool>();
    sa.not_conditions = notc;
    a.base.save_advantages.push_back(sa);
    return std::nullopt;
}

std::optional<std::string> apply_apply_modifier(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx& ctx) {
    if (auto p = f.effect.find("passive"); p != f.effect.end()) {
        if (!p->is_boolean() || !p->get<bool>()) return f.id + ": passive must be true";
        return apply_passive_advantage(a, f);
    }
    if (f.effect.contains("when")) return f.id + ": when needs passive";
    std::vector<const Json*> mods;
    if (auto m = f.effect.find("modifier"); m != f.effect.end()) mods.push_back(&*m);
    if (auto ms = f.effect.find("modifiers"); ms != f.effect.end() && ms->is_array())
        for (const Json& m : *ms) mods.push_back(&m);
    const auto adv_on = f.effect.find("advantage_on");   // 7.2z Rage: advantage on Str checks and saves
    if ((mods.empty() && adv_on == f.effect.end()) || mods.size() > 16)
        return f.id + ": apply_modifier needs modifier, 1..16 modifiers[] or advantage_on";
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    const std::string cost = f.effect.value("cost", "action");
    if (cost == "bonus_action") af.cost = FeatureCost::BonusAction;
    else if (cost != "action") return f.id + ": apply_modifier cost '" + cost + "' is not supported (use action or bonus_action)";
    rules5e::FeatureSpec pool_spec;   // 7.2y: usage {kind: pool, pool, cost?} spends from resources[] (refused when empty)
    if (auto u = f.effect.find("usage"); u != f.effect.end()) {
        if (!u->is_object() || u->value("kind", "") != "pool") {
            a.unsupported_features.push_back(f.id + " (apply_modifier: usage other than pool)");
            return std::nullopt;
        }
        ActiveFeature tmp; tmp.feature_id = fid;
        if (auto e = fill_spec(a, f, tmp, pool_spec)) return e;
    }
    // scope (7.2y / 7.2z, closed, at least one key): weapon "wielded" (Sacred Weapon), attack melee_weapon | ranged_weapon,
    // ability str | dex (Rage: melee_weapon + str). Read by AttackRoll / Damage rows.
    uint8_t weapon_scope = 0, attack_scope = 0, ability_scope = 0;
    if (auto sc = f.effect.find("scope"); sc != f.effect.end()) {
        if (!sc->is_object() || sc->empty()) return f.id + ": scope needs at least one of weapon, attack, ability";
        for (auto it = sc->begin(); it != sc->end(); ++it) {
            const std::string v = it.value().is_string() ? it.value().get<std::string>() : "";
            if (it.key() == "weapon" && v == "wielded") weapon_scope = 1;
            else if (it.key() == "attack" && (v == "melee_weapon" || v == "ranged_weapon")) attack_scope = v == "melee_weapon" ? 1 : 2;
            else if (it.key() == "ability" && (v == "str" || v == "dex")) ability_scope = v == "str" ? 1 : 2;
            else return f.id + ": scope: bad key or value '" + it.key() + "'";
        }
    }
    uint32_t ends = 0, blocks = 0;
    if (auto e = f.effect.find("ends_early"); e != f.effect.end() && !enum_bits(*e, {"no_attack_or_damage_since_last_turn", "unconscious"}, ends))
        return f.id + ": bad ends_early";
    if (auto e = f.effect.find("blocks"); e != f.effect.end() && !enum_bits(*e, {"casting", "concentration"}, blocks))
        return f.id + ": bad blocks";
    int exhaustion = 0;
    if (auto e = f.effect.find("on_end"); e != f.effect.end()) {
        if (!e->is_object() || e->size() != 1 || !e->contains("exhaustion") || !(*e)["exhaustion"].is_number_integer())
            return f.id + ": on_end must be {exhaustion: 1..6}";
        exhaustion = (*e)["exhaustion"].get<int>();
        if (exhaustion < 1 || exhaustion > 6) return f.id + ": on_end must be {exhaustion: 1..6}";
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
        m.weapon_scope = weapon_scope;
        rows.push_back(m);
    }
    if (adv_on != f.effect.end()) {   // 7.2z: Save / AbilityCheck advantage rows, timed like the first modifier (else untimed)
        if (!adv_on->is_object() || adv_on->empty()) return f.id + ": bad advantage_on";
        for (auto it = adv_on->begin(); it != adv_on->end(); ++it) {
            uint32_t bits = 0;
            if ((it.key() != "checks" && it.key() != "saves") || !enum_bits(it.value(), kAbilities7z, bits)) return f.id + ": bad advantage_on";
            for (int ab = 0; ab < 6; ++ab) {
                if (!(bits & (1u << ab))) continue;
                rules5e::ModifierSpec m = rows.empty() ? rules5e::ModifierSpec{} : rows.front();
                m.stat = it.key() == "saves" ? rules5e::ModStat::Save : rules5e::ModStat::AbilityCheck;
                m.bonus = 0; m.die = {0, 0, 0}; m.mode = rules5e::RollMode::Advantage;
                m.has_ability = true; m.ability = rules5e::Ability(ab); m.has_damage_type = false; m.when.clear();
                if (rows.empty()) m.applies_to = rules5e::ModAppliesTo::Self;
                m.source = source;
                m.id = rules5e::spell_id_of(a.id + "#" + f.id + "#m" + std::to_string(rows.size()));
                rows.push_back(m);
            }
        }
    }
    for (auto& m : rows) {   // 7.2z: the effect-level keys on every row (rules act on the source)
        m.weapon_scope = weapon_scope;
        if (m.stat == rules5e::ModStat::Damage || m.stat == rules5e::ModStat::AttackRoll) {
            if (attack_scope) m.scope = attack_scope;
            m.scope_ability = ability_scope;
        }
        m.ends_early = uint8_t(ends); m.blocks = uint8_t(blocks); m.on_end_exhaustion = uint8_t(exhaustion);
    }
    a.feature_modifiers[fid] = std::move(rows);
    if (f.effect.contains("usage")) a.feature_specs[fid] = pool_spec;   // 7.2y: the pool gates UseFeature ("not enough resource")
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
// armor_class.note names one (e.g. "chain mail, shield"); "natural armor" does not count. Entries take any
// equipment_entry_id form, so character@4 {"item": "srd51.item.chain_mail"} counts.
bool wears_armor(const Json& doc) {
    static const char* kArmor[] = {"padded", "leather", "studded_leather", "hide", "chain_shirt", "scale_mail", "breastplate",
                                   "half_plate", "ring_mail", "chain_mail", "splint", "plate"};
    if (auto eq = doc.find("equipment"); eq != doc.end() && eq->is_array())
        for (const Json& e : *eq) {
            // A namespaced id ("srd51.item.chain_mail") matches on its last '.'-segment, as carries_shield does.
            const std::string id = equipment_entry_id(e);
            const std::size_t dot = id.rfind('.');
            const std::string name = dot == std::string::npos ? id : id.substr(dot + 1);
            for (const char* k : kArmor) if (name == k || name == std::string(k) + "_armor") return true;
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

// R-G7 static feature kinds (open kinds, live at any declared version like R-G5's).
// reroll_ones {rolls: ["attack", "check", "save"]} (Halfling Lucky): rules5e Combatant::reroll_ones bits 1 attack roll,
// 2 ability check, 4 saving throw; `rolls` omitted = all three (7). Several declarations OR together.
std::optional<std::string> apply_reroll_ones(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx&) {
    uint8_t mask = 7;
    if (auto rl = f.effect.find("rolls"); rl != f.effect.end()) {
        if (!rl->is_array() || rl->empty()) return f.id + ": reroll_ones rolls must be a non-empty array";
        mask = 0;
        for (const Json& r : *rl) {
            const std::string n = r.is_string() ? r.get<std::string>() : "";
            if (n == "attack") mask |= 1;
            else if (n == "check") mask |= 2;
            else if (n == "save") mask |= 4;
            else return f.id + ": reroll_ones rolls entries must be 'attack', 'check' or 'save'";
        }
    }
    a.base.reroll_ones = uint8_t(a.base.reroll_ones | mask);
    return std::nullopt;
}
// great_weapon_fighting {} (Fighting Style): weapon dice of two-handed / versatile-in-two-hands melee attacks showing 1-2 are
// rerolled once (rules5e). The grip of a versatile weapon is ActionRequest::two_handed_grip (sim AI, Archetype::two_handed_grip).
std::optional<std::string> apply_great_weapon_fighting(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    a.base.great_weapon_fighting = true;
    return std::nullopt;
}
// evasion {} (Rogue / Monk): a Dex save against a half-on-success effect deals 0 on a success, half on a failure (rules5e).
std::optional<std::string> apply_evasion(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    a.base.evasion = true;
    return std::nullopt;
}

// Small leftovers (rules #111, VERSION_BUMP_BATCH 7.2j hand-off a): open static kinds, no fields (the object stays open).
// aggressive {} (orc Aggressive): rules5e Combatant::aggressive, a bonus-action Dash (ActionRequest::aggressive); sim keeps the
// movement past the creature's own toward a hostile it can see (Combat::aggressive_step_ok).
std::optional<std::string> apply_aggressive(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    a.base.aggressive = true;
    return std::nullopt;
}
// reckless_attack {} (Barbarian Reckless Attack; minotaur / berserker Reckless): rules5e Combatant::reckless_attack. Declared by
// the caller with ActionRequest::reckless on the turn's first Attack; the built-in AI declares it on its first melee attack.
std::optional<std::string> apply_reckless_attack(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    a.base.reckless_attack = true;
    return std::nullopt;
}

// R-G14 sim wiring (rules #109, VERSION_BUMP_BATCH 7.2i hand-off a), open static kinds:
// metamagic {options: [careful | distant | empowered | extended | heightened | quickened | subtle | twinned], pool}: Sorcerer
// Metamagic. rules5e Combatant::metamagic_known (kMeta* bits) and sorcery_pool = the resources[] pool `pool` (sorcery points);
// the caller sets ActionRequest::metamagic (log_hash 0xB1). Several declarations add options; the pool must agree.
std::optional<std::string> apply_metamagic(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx&) {
    static const char* kNames[] = {"careful", "distant", "empowered", "extended", "heightened", "quickened", "subtle", "twinned"};
    const auto opts = f.effect.find("options");
    if (opts == f.effect.end() || !opts->is_array() || opts->empty()) return f.id + ": metamagic needs options";
    uint8_t known = 0;
    for (const Json& o : *opts) {
        const std::string n = o.is_string() ? o.get<std::string>() : "";
        int bitpos = -1;
        for (int i = 0; i < 8; ++i) if (n == kNames[i]) bitpos = i;
        if (bitpos < 0) return f.id + ": unknown metamagic option '" + n + "'";
        known = uint8_t(known | (1u << bitpos));   // kMetaCareful 1 ... kMetaTwinned 128, in kNames order
    }
    const std::string pid = f.effect.value("pool", "");
    bool found = false;
    for (const auto& r : a.resources) found = found || r.id == pid;
    if (!found) return f.id + ": metamagic pool '" + pid + "' is not a limited resources[] pool";
    const uint32_t pool = rules5e::spell_id_of(pid);
    if (a.base.sorcery_pool != 0 && a.base.sorcery_pool != pool) return f.id + ": metamagic declarations name different pools";
    a.base.metamagic_known = uint8_t(a.base.metamagic_known | known);
    a.base.sorcery_pool = pool;
    return std::nullopt;
}
// font_of_magic {pool} (Sorcerer 2+, VERSION_BUMP_BATCH 7.2o hand-off): the sorcery pool is the resources[] pool `pool`
// (registered here when resources[] lacks it), resolved to max = start = sorcery_points_max(sorcerer level from classes[])
// with recharge LongRest, and Combatant::sorcery_pool points at it even without a metamagic feature. A metamagic declaration
// naming a different pool (either order) is a load error, as for two metamagic declarations.
std::optional<std::string> apply_font_of_magic(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx& ctx) {
    const std::string pid = f.effect.value("pool", "");
    if (pid.empty()) return f.id + ": font_of_magic needs pool";
    for (const auto& u : a.unlimited_resources)
        if (u == pid) return f.id + ": font_of_magic pool '" + pid + "' is unlimited";
    int level = 0;
    for (const Json& c : ctx.doc.value("classes", Json::array()))
        if (c.is_object() && c.value("class", "") == "sorcerer") level += c.value("level", 0);
    const int max = rules5e::sorcery_points_max(level);
    if (max <= 0) return f.id + ": font_of_magic needs sorcerer level 2+ (sorcerer level " + std::to_string(level) + ")";
    const uint32_t pool = rules5e::spell_id_of(pid);
    if (a.base.sorcery_pool != 0 && a.base.sorcery_pool != pool) return f.id + ": font_of_magic and metamagic name different pools";
    Archetype::Resource* res = nullptr;
    for (auto& r : a.resources)
        if (r.id == pid) res = &r;
    if (!res) {
        a.resources.push_back(Archetype::Resource{pid, pool, 0, 0, 0});
        res = &a.resources.back();
    }
    res->max = uint16_t(max);
    res->start = uint16_t(max);
    res->recharge = uint8_t(rules5e::FeatureRecharge::LongRest);
    a.base.sorcery_pool = pool;
    return std::nullopt;
}
// sculpt_spells {} (Evocation Wizard): rules5e Combatant::sculpt_spells; the caller lists ActionRequest::spared.
std::optional<std::string> apply_sculpt_spells(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    a.base.sculpt_spells = true;
    return std::nullopt;
}

// slot_recovery {class: wizard | druid, max_slot_level 1..9 = 5, uses 1..255 = 1} (Arcane Recovery / Natural Recovery,
// VERSION_BUMP_BATCH 7.2n hand-off 1): a rules5e SlotRecoverySpec with the class level from classes[]. Load errors: unknown
// class, class missing from classes[], non-integer or out-of-range max_slot_level / uses, two declarations for one class.
std::optional<std::string> apply_slot_recovery(Archetype& a, const FeatureDecl& f, uint32_t feature_id, const FeatureBuildCtx& ctx) {
    const std::string cls = f.effect.value("class", "");
    if (cls != "wizard" && cls != "druid") return f.id + ": slot_recovery class must be wizard or druid";
    int level = 0;
    bool listed = false;
    for (const Json& c : ctx.doc.value("classes", Json::array()))
        if (c.is_object() && c.value("class", "") == cls) {
            listed = true;
            level += c.value("level", 0);
        }
    if (!listed) return f.id + ": slot_recovery class '" + cls + "' is not in classes[]";
    if (level < 1 || level > 20) return f.id + ": slot_recovery class level out of range 1..20";
    auto int_key = [&](const char* key, int def, int lo, int hi, int& out) -> std::optional<std::string> {
        const auto it = f.effect.find(key);
        if (it == f.effect.end()) { out = def; return std::nullopt; }
        if (!it->is_number_integer()) return f.id + ": slot_recovery " + key + " must be an integer";
        const int64_t v = it->get<int64_t>();
        if (v < lo || v > hi) return f.id + ": slot_recovery " + key + " out of range " + std::to_string(lo) + ".." + std::to_string(hi);
        out = int(v);
        return std::nullopt;
    };
    int max_level = 5, uses = 1;
    if (auto e = int_key("max_slot_level", 5, 1, 9, max_level)) return e;
    if (auto e = int_key("uses", 1, 1, 255, uses)) return e;
    for (const auto& prev : a.slot_recovery)
        if (a.features[prev.feature_id - 1].effect.value("class", "") == cls)
            return f.id + ": slot_recovery declared twice for class '" + cls + "'";
    a.slot_recovery.push_back(rules5e::SlotRecoverySpec{feature_id, uint8_t(level), uint8_t(max_level), uint8_t(uses)});
    return std::nullopt;
}

// pack_tactics {} (SRD Pack Tactics: jackal, kobold, wolf, dire wolf; T&C-G7): advantage on the creature's attack rolls
// against a creature when at least one of its allies that is not incapacitated is within 5 ft of it. Fills the rules5e
// roll trait {Attack, Advantage, kWhenAllyNearTarget}; sim supplies the fact (ActionRequest::ally_near_target, Combat::ally_near).
// No fields; a second declaration adds nothing. Static data, not state: no hash tag.
std::optional<std::string> apply_pack_tactics(Archetype& a, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) {
    rules5e::RollTrait t{};
    t.roll = rules5e::TraitRoll::Attack;
    t.mode = rules5e::RollMode::Advantage;
    t.when = rules5e::kWhenAllyNearTarget;
    for (const rules5e::RollTrait& have : a.base.roll_traits)
        if (have.roll == t.roll && have.mode == t.mode && have.when == t.when && have.target_conditions == 0 &&
            have.target_types == 0)
            return std::nullopt;
    a.base.roll_traits.push_back(t);
    return std::nullopt;
}

// R-G12 transforms (VERSION_BUMP_BATCH 7.2q hand-off, sim side; docs/proposals/SUMMONS_TRANSFORMS.md). Keys exactly as
// common@2 $defs/wild_shape_effect / shapechanger_effect (#163); any other key is a load error ("unknown key", like the
// rules polymorph loader), so the old sim-only keys (uses, recharge, ai, max_cr_by_level[].cr, forms[].speed_walk) and a
// duration key are refused.
namespace {
bool only_keys(const Json& o, std::initializer_list<const char*> keys, std::string& bad) {
    for (auto it = o.begin(); it != o.end(); ++it)
        if (std::none_of(keys.begin(), keys.end(), [&](const char* k) { return it.key() == k; })) { bad = it.key(); return false; }
    return true;
}
}  // namespace

// wild_shape {pool (required: a limited resources[] pool that recharges on short_rest; SRD max 2), cost: action (default) |
//   bonus_action, class (default druid), forms: [beast content ids] (1..255), max_cr_by_level: [{level 1..20, max_cr: CR
//   string}] (required, levels strictly ascending), no_fly_below 1..20 (default 8), no_swim_below 1..20 (default 4)}.
//   Uses and recharge come from the pool: each use spends 1 (FeatureSpec::pool / pool_cost); the feature has no uses of its
//   own. No duration key: form_rounds = 600 * (class level / 2) (hours = half the level). max_cr is CR * 8 in integer math
//   (cr_x8_of). The forms' ids resolve when the transform is requested (build_form): content load order cannot see every
//   pack yet. Features (features[] / traits[] entries) with requires_humanoid_form: true become the form's blocked_features.
// Version gate (same mechanism as extra_attack / gaze): wild_shape is a common@2 kind, live at character@4 only, and not in the
// frozen 3 -> 4 kind list, so a file that declared < 4 lists it inert (CONTENT_COMPAT.md: never a load error). @1 / @2 effects
// are already inert through the 2 -> 3 migration; this covers @3.
std::optional<std::string> apply_wild_shape(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx& ctx) {
    if (ctx.declared_version != 0 && ctx.declared_version < 4) {
        a.inert_features.push_back(f.id + " (wild_shape)");
        return std::nullopt;
    }
    std::string bad;
    if (!only_keys(f.effect, {"kind", "pool", "cost", "class", "forms", "max_cr_by_level", "no_fly_below", "no_swim_below"}, bad))
        return f.id + ": wild_shape effect: unknown key '" + bad + "'";
    if (f.requires_humanoid_form) return f.id + ": requires_humanoid_form on the wild_shape feature itself";
    Archetype::FormFeature ff;
    ff.kind = uint8_t(rules5e::FormKind::WildShape);
    const auto fl = f.effect.find("forms");
    if (fl == f.effect.end() || !fl->is_array() || fl->empty() || fl->size() > 255) return f.id + ": wild_shape needs forms[] (1..255 ids)";
    for (const Json& x : *fl) {
        if (!x.is_string() || x.get<std::string>().empty()) return f.id + ": wild_shape forms[] must be content ids";
        ff.forms.push_back(x.get<std::string>());
    }
    const auto cl = f.effect.find("class");
    if (cl != f.effect.end() && (!cl->is_string() || cl->get<std::string>().empty())) return f.id + ": wild_shape class must be a class name";
    const std::string cls = cl == f.effect.end() ? std::string("druid") : cl->get<std::string>();
    int level = 0;
    for (const Json& c : ctx.doc.value("classes", Json::array()))
        if (c.is_object() && c.value("class", "") == cls && c.value("level", Json()).is_number_integer()) level += c["level"].get<int>();
    if (level < 2 || level > 20) return f.id + ": wild_shape needs " + cls + " level 2..20 in classes[]";
    ff.level = uint8_t(level);
    const auto t = f.effect.find("max_cr_by_level");
    if (t == f.effect.end() || !t->is_array() || t->empty() || t->size() > 20)
        return f.id + ": wild_shape needs max_cr_by_level (1..20 rows)";
    int prev = 0;
    ff.max_cr_x8 = -1;
    for (const Json& e : *t) {
        if (!e.is_object() || !only_keys(e, {"level", "max_cr"}, bad))
            return f.id + ": wild_shape max_cr_by_level: unknown key '" + bad + "'";
        if (!e.contains("level") || !e["level"].is_number_integer() || !e.contains("max_cr"))
            return f.id + ": wild_shape max_cr_by_level entries need level and max_cr";
        const int lv = e["level"].get<int>();
        if (lv < 1 || lv > 20) return f.id + ": wild_shape max_cr_by_level level must be 1..20";
        if (lv <= prev) return f.id + ": wild_shape max_cr_by_level levels must be strictly ascending";
        prev = lv;
        if (!e["max_cr"].is_string()) return f.id + ": wild_shape max_cr must be a CR string (\"1/4\", \"1\", ...)";
        const auto cr = cr_x8_of(e["max_cr"]);
        if (!cr) return f.id + ": wild_shape bad max_cr in max_cr_by_level";
        if (lv <= level) ff.max_cr_x8 = int16_t(*cr);   // ascending: the last row at or below the class level applies
    }
    if (ff.max_cr_x8 < 0) return f.id + ": wild_shape max_cr_by_level has no row at or below " + cls + " level " + std::to_string(level);
    auto lvl_key = [&](const char* k, uint8_t& out) -> std::optional<std::string> {
        const auto it = f.effect.find(k);
        if (it == f.effect.end()) return std::nullopt;   // defaults: FormFeature (no_fly_below 8, no_swim_below 4)
        if (!it->is_number_integer() || it->get<int64_t>() < 1 || it->get<int64_t>() > 20) return f.id + ": wild_shape " + k + " must be 1..20";
        out = uint8_t(it->get<int64_t>());
        return std::nullopt;
    };
    if (auto e = lvl_key("no_fly_below", ff.no_fly_below)) return e;
    if (auto e = lvl_key("no_swim_below", ff.no_swim_below)) return e;
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    rules5e::FeatureSpec spec;
    const std::string cost = f.effect.value("cost", "action");
    if (cost == "action") { af.cost = FeatureCost::Action; spec.cost = 1; }
    else if (cost == "bonus_action") { af.cost = FeatureCost::BonusAction; spec.cost = 2; }
    else return f.id + ": wild_shape cost must be action or bonus_action";
    const std::string pid = f.effect.value("pool", "");
    if (pid.empty()) return f.id + ": wild_shape needs pool (a resources[] id)";
    const Archetype::Resource* res = nullptr;
    for (const auto& r : a.resources) if (r.id == pid) res = &r;
    if (!res) return f.id + ": wild_shape names unknown (or unlimited) pool '" + pid + "'";
    if (res->recharge != uint8_t(rules5e::FeatureRecharge::ShortRest)) return f.id + ": wild_shape pool '" + pid + "' must recharge on short_rest";
    spec.pool = res->pool_id;
    spec.pool_cost = 1;
    af.uses = 0;                      // the pool counts the uses (and recharges on a short rest)
    af.recharge = Recharge::None;
    spec.form = uint8_t(rules5e::FormKind::WildShape);
    spec.form_rounds = uint16_t(600 * (level / 2));
    spec.form_choices = uint8_t(ff.forms.size());
    for (std::size_t i = 0; i < a.features.size(); ++i)
        if (a.features[i].requires_humanoid_form) a.humanoid_only_features.push_back(uint32_t(i + 1));
    a.feature_specs[fid] = spec;
    a.form_features[fid] = std::move(ff);
    a.active.push_back(std::move(af));
    return std::nullopt;
}

// shapechanger {cost: action (default) | bonus_action, forms: [{id, name, size?, armor_class?, speed?, actions?,
// creature_type?}] (1..255)}: the SRD Shapechanger trait (werewolf, doppelganger, ...). Needs the creature fact
// shapechanger: true (else a load error, like the validator). "Statistics other than size are the same" unless the form
// lists them: speed replaces the speed object (sim keeps its walk speed: FormStats has one speed), actions = this
// document's own actions[] ids usable in the form (omitted = all; the others' attacks / multiattacks are dropped),
// creature_type replaces the type. One HP pool, no timer, no uses (at will).
// Version gate as wild_shape: live at monster@4 / character@4 only; a file that declared < 4 lists it inert (no form swap;
// the creature fact `shapechanger` is read as before).
std::optional<std::string> apply_shapechanger(Archetype& a, const FeatureDecl& f, uint32_t fid, const FeatureBuildCtx& ctx) {
    if (ctx.declared_version != 0 && ctx.declared_version < 4) {
        a.inert_features.push_back(f.id + " (shapechanger)");
        return std::nullopt;
    }
    std::string bad;
    if (!only_keys(f.effect, {"kind", "cost", "forms"}, bad)) return f.id + ": shapechanger effect: unknown key '" + bad + "'";
    if (ctx.doc.value("shapechanger", Json()) != Json(true)) return f.id + ": shapechanger trait requires shapechanger: true";
    Archetype::FormFeature ff;
    ff.kind = uint8_t(rules5e::FormKind::Shapechanger);
    const auto fl = f.effect.find("forms");
    if (fl == f.effect.end() || !fl->is_array() || fl->empty() || fl->size() > 255) return f.id + ": shapechanger needs forms[] (1..255)";
    std::vector<std::string> own_actions;
    for (const Json& x : ctx.doc.value("actions", Json::array()))
        if (x.is_object() && x.value("id", Json()).is_string()) own_actions.push_back(x["id"].get<std::string>());
    static const char* sizes[] = {"tiny", "small", "medium", "large", "huge", "gargantuan"};
    for (const Json& x : *fl) {
        if (!x.is_object() || !x.value("id", Json()).is_string() || x["id"].get<std::string>().empty())
            return f.id + ": shapechanger forms[] entries need an id";
        if (!only_keys(x, {"id", "name", "size", "armor_class", "speed", "actions", "creature_type"}, bad))
            return f.id + ": shapechanger form: unknown key '" + bad + "'";
        Archetype::FormFeature::Shape sh;
        sh.id = x["id"].get<std::string>();
        if (!x.value("name", Json()).is_string() || x["name"].get<std::string>().empty())
            return f.id + ": shapechanger form '" + sh.id + "' needs a name";
        for (const auto& o : ff.shapes) if (o.id == sh.id) return f.id + ": shapechanger duplicate form id '" + sh.id + "'";
        if (auto sz = x.find("size"); sz != x.end()) {
            const std::string n = sz->is_string() ? sz->get<std::string>() : std::string();
            for (int i = 0; i < 6; ++i) if (n == sizes[i]) sh.size = CreatureSize(i);
            if (!sh.size) return f.id + ": shapechanger form '" + sh.id + "' has a bad size";
        }
        if (auto ac = x.find("armor_class"); ac != x.end()) {
            if (!ac->is_number_integer() || ac->get<int>() < 0 || ac->get<int>() > 40) return f.id + ": shapechanger bad armor_class";
            sh.ac = int16_t(ac->get<int>());
        }
        if (auto sp = x.find("speed"); sp != x.end()) {
            if (!sp->is_object()) return f.id + ": shapechanger bad speed";
            const Json w = sp->value("walk", Json(0));
            if (!w.is_number_integer() || w.get<int>() < 0 || w.get<int>() > 255) return f.id + ": shapechanger bad speed.walk";
            sh.speed_ft = int16_t(w.get<int>());
        }
        if (auto ct = x.find("creature_type"); ct != x.end()) {
            const auto t = ct->is_string() ? creature_type_from_name(ct->get<std::string>()) : std::nullopt;
            if (!t) return f.id + ": shapechanger form '" + sh.id + "' has a bad creature_type";
            sh.creature_type = *t;
        }
        if (auto ac = x.find("actions"); ac != x.end()) {
            if (!ac->is_array() || ac->size() > 255) return f.id + ": shapechanger bad actions";
            std::vector<std::string> keep;
            for (const Json& id : *ac) {
                if (!id.is_string() || std::find(own_actions.begin(), own_actions.end(), id.get<std::string>()) == own_actions.end())
                    return f.id + ": shapechanger form '" + sh.id + "' names unknown action '" + (id.is_string() ? id.get<std::string>() : "?") + "'";
                keep.push_back(id.get<std::string>());
            }
            for (const auto& o : own_actions)
                if (std::find(keep.begin(), keep.end(), o) == keep.end()) sh.blocked_actions.push_back(o);
        }
        ff.shapes.push_back(std::move(sh));
    }
    ActiveFeature af;
    af.feature_id = fid;
    af.id = f.id;
    af.kind = f.kind;
    rules5e::FeatureSpec spec;
    const std::string cost = f.effect.value("cost", "action");
    if (cost == "action") { af.cost = FeatureCost::Action; spec.cost = 1; }
    else if (cost == "bonus_action") { af.cost = FeatureCost::BonusAction; spec.cost = 2; }
    else return f.id + ": shapechanger cost must be action or bonus_action";
    spec.form = uint8_t(rules5e::FormKind::Shapechanger);
    spec.form_rounds = 0;
    spec.form_choices = uint8_t(ff.shapes.size());
    a.base.shapechanger = true;
    a.feature_specs[fid] = spec;
    a.form_features[fid] = std::move(ff);
    a.active.push_back(std::move(af));
    return std::nullopt;
}

// extra_attack {attacks_per_action: 2..4} (character@4 features[]; contract agreed with T&C): the Attack action makes N weapon
// attacks (rules5e Combatant::attacks_per_action, TurnBudget::attacks_remaining; grapple / shove replace one of them, moving
// between them is allowed). N is required, no default. Several declarations: the highest wins (no stacking, no error).
// Characters only. Error strings are the contract's exact text.
// Like gaze, it is not in the migration's frozen 3 -> 4 kind list, so the @4 gate is here: a file that declared < 4 lists it inert.
std::optional<std::string> apply_extra_attack(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx& ctx) {
    if (ctx.declared_version != 0 && ctx.declared_version < 4) {
        a.inert_features.push_back(f.id + " (extra_attack)");
        return std::nullopt;
    }
    for (auto it = f.effect.begin(); it != f.effect.end(); ++it)
        if (it.key() != "kind" && it.key() != "attacks_per_action") return "extra_attack: unknown key '" + it.key() + "'";
    if (a.schema_type != "character") return std::string("extra_attack: only valid on characters");
    const auto n = f.effect.find("attacks_per_action");
    if (n == f.effect.end() || !n->is_number_integer() || n->get<int64_t>() < 2 || n->get<int64_t>() > 4)
        return std::string("extra_attack: attacks_per_action must be an integer 2..4");
    a.base.attacks_per_action = std::max<uint8_t>(a.base.attacks_per_action, uint8_t(n->get<int64_t>()));
    return std::nullopt;
}

// transparent {dc: 1..30} (monster@4 traits[]; T&C spec, 7.2w): SRD Transparent (gelatinous cube): rules5e
// Combatant::transparent_dc, read by sim's surprise setup. Monsters only; several declarations: the highest wins (the
// validator warns). Like extra_attack it is not in the frozen 3 -> 4 kind list, so the @4 gate is here.
std::optional<std::string> apply_transparent(Archetype& a, const FeatureDecl& f, uint32_t, const FeatureBuildCtx& ctx) {
    if (ctx.declared_version != 0 && ctx.declared_version < 4) {
        a.inert_features.push_back(f.id + " (transparent)");
        return std::nullopt;
    }
    for (auto it = f.effect.begin(); it != f.effect.end(); ++it)
        if (it.key() != "kind" && it.key() != "dc") return "transparent: unknown key '" + it.key() + "'";
    if (a.schema_type != "monster") return std::string("transparent: only valid on monsters");
    const auto n = f.effect.find("dc");
    if (n == f.effect.end() || !n->is_number_integer() || n->get<int64_t>() < 1 || n->get<int64_t>() > 30)
        return std::string("transparent: dc must be an integer 1..30");
    a.base.transparent_dc = std::max<int8_t>(a.base.transparent_dc, int8_t(n->get<int64_t>()));
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
    r.add_hook("bonus_attack", {[](Archetype&, const FeatureDecl&, uint32_t, const FeatureBuildCtx&) -> std::optional<std::string> {
        return std::nullopt; }});   // 7.2z Frenzy (no minimum: like gaze / transparent, the @4 gate is compile_bonus_attacks': compiled by content.cpp compile_bonus_attacks (requires_effect)
    r.add_hook("attack_bonus", {apply_attack_bonus});
    r.add_hook("crit_range", {apply_crit_range});
    r.add_hook("ac_bonus", {apply_ac_bonus});
    r.add_hook("grant_action", {apply_grant_action}, 4);
    r.add_hook("heal_pool", {apply_heal_pool}, 4);
    r.add_hook("reroll_ones", {apply_reroll_ones});
    r.add_hook("great_weapon_fighting", {apply_great_weapon_fighting});
    r.add_hook("evasion", {apply_evasion});
    r.add_hook("aggressive", {apply_aggressive});
    r.add_hook("reckless_attack", {apply_reckless_attack});
    r.add_hook("pack_tactics", {apply_pack_tactics});
    r.add_hook("metamagic", {apply_metamagic});
    r.add_hook("font_of_magic", {apply_font_of_magic});
    r.add_hook("sculpt_spells", {apply_sculpt_spells});
    r.add_hook("slot_recovery", {apply_slot_recovery});
    r.add_hook("wild_shape", {apply_wild_shape});     // R-G12 (7.2q)
    r.add_hook("shapechanger", {apply_shapechanger}); // R-G12 (7.2q)
    r.add_hook("extra_attack", {apply_extra_attack});   // character@4: gated in the hook (the 3 -> 4 kind list is frozen)
    r.add_hook("transparent", {apply_transparent});     // monster@4 (7.2w): gated in the hook, like extra_attack
    // Kinds the @4 schemas introduce (frozen list of the 3 -> 4 migration; hooks arrive with their rows).
    for (const char* k : {"on_event", "save_effect", "reaction"})
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
            d.requires_humanoid_form = f.value("requires_humanoid_form", Json()) == Json(true);   // R-G12: a trait / feature key
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
