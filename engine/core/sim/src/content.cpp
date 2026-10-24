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
// Conditions a save_condition rider (on_hit[] or on_event) may apply with NO duration and no repeat save: they end by their
// own rules, not by a timer or a save, so the rules apply them untimed (rules5e SpellDef / SpellEffect::condition_untracked:
// no ActiveSpellEffect entry), the way Shove applies prone. Prone ends when the creature stands up (half its speed, rules5e
// ActionKind::StandUp). Keep this list explicit; every other condition without a duration stays unsupported.
constexpr rules5e::Condition kEndsByOwnRules[] = {rules5e::Condition::Prone};
bool ends_by_own_rules(rules5e::Condition c) {
    for (rules5e::Condition x : kEndsByOwnRules) if (x == c) return true;
    return false;
}
// Batch 3 (Lich ruling, death dog): duration {kind: until_cured} = no timer and no repeat save; the condition ends only through
// an explicit cure. Only conditions an existing cure ends are allowed: Lesser Restoration (SRD 5.1: "The condition can be
// blinded, deafened, paralyzed, or poisoned"). Applied untracked (rules5e condition_untracked, no ActiveSpellEffect entry), so
// rules5e EndCondition removes the bare bit. Conditions with a natural SRD end (prone: stand up; grappled: escape) are refused.
constexpr rules5e::Condition kCuredOnly[] = {rules5e::Condition::Blinded, rules5e::Condition::Deafened,
                                             rules5e::Condition::Paralyzed, rules5e::Condition::Poisoned};
bool cured_only_ok(rules5e::Condition c) {
    for (rules5e::Condition x : kCuredOnly) if (x == c) return true;
    return false;
}
std::optional<rules5e::CreatureType> creature_type_of(const std::string& s) {
    static const char* names[] = {"aberration", "beast", "celestial", "construct", "dragon", "elemental", "fey",
                                  "fiend", "giant", "humanoid", "monstrosity", "ooze", "plant", "undead"};
    for (int i = 0; i < 14; ++i)
        if (s == names[i]) return rules5e::CreatureType(i);
    return std::nullopt;
}

// 7.2y `bonus_vs: {creature_types: [..], dice}` on a damage entry (closed; T&C keys). Absent = nothing. Error string or empty.
std::string read_bonus_vs(const Json& dmg, uint32_t& types, rules5e::DiceExpr& dice) {
    types = 0; dice = {0, 0, 0};
    auto bv = dmg.is_object() ? dmg.find("bonus_vs") : dmg.end();
    if (!dmg.is_object() || bv == dmg.end()) return {};
    if (!bv->is_object()) return "bad bonus_vs";
    for (auto it = bv->begin(); it != bv->end(); ++it)
        if (it.key() != "creature_types" && it.key() != "dice") return "bonus_vs: unknown key '" + it.key() + "'";
    const auto ct = bv->find("creature_types");
    if (ct == bv->end() || !ct->is_array() || ct->empty()) return "bonus_vs needs creature_types[]";
    for (const Json& t : *ct) {
        auto c = creature_type_of(t.is_string() ? t.get<std::string>() : "");
        if (!c) return "bonus_vs: bad creature type";
        types |= rules5e::bit(*c);
    }
    auto d = bv->contains("dice") && (*bv)["dice"].is_string() ? parse_dice((*bv)["dice"].get<std::string>()) : std::nullopt;
    if (!d || d->count == 0) return "bonus_vs needs dice";
    dice = *d;
    return {};
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

// `untimed_ok`: an apply[] entry with no duration and no repeat save is accepted for the conditions that end by their own
// rules (kEndsByOwnRules), applied untracked; only save_condition riders pass true.
std::optional<std::string> compile_save_action(const std::string& arch_id, const Json& act, rules5e::SpellDef& d,
                                               Archetype::SaveAction& sa, const Json& doc, bool& unsupported,
                                               bool untimed_ok = false);

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
            if (auto er = read_bonus_vs(d, x.vs_types, x.vs_dice); !er.empty()) return where + ": " + er;   // 7.2y
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
    if (k == "forced_move") {   // 7.2zb Open Hand: a synthetic SpellDef: [Save (fixed DC) ->] ForcedMove push (forced-movement path)
        for (auto it = r.begin(); it != r.end(); ++it)
            if (it.key() != "kind" && it.key() != "save" && it.key() != "mode" && it.key() != "feet" && it.key() != "when")
                return where + ": forced_move: unknown key '" + it.key() + "'";
        if (r.value("mode", "") != "push") return where + ": forced_move: mode must be push";
        const int ft = geti(r, "feet");
        if (ft < 5 || ft > 60 || ft % 5 != 0) return where + ": forced_move: feet must be 5..60, a multiple of 5";
        rules5e::SpellDef d;
        d.content_id = rc.arch + "#forced_move." + std::to_string(rc.synth.size());
        d.id = rules5e::spell_id_of(d.content_id);
        d.level = 0; d.casting_time = rules5e::CastingTime::Action; d.is_spell = false; d.magical = false; d.targets = 1;
        rules5e::ForcedMoveSpec fm{}; fm.mode = 1; fm.feet = uint16_t(ft);
        if (auto sv = r.find("save"); sv != r.end()) {
            const auto ab = sv->is_object() ? ability_of(sv->value("ability", "")) : std::nullopt;
            const int dc = sv->is_object() ? geti(*sv, "dc") : 0;
            if (!ab || dc < 1 || dc > 30) return where + ": forced_move: bad save";
            d.kind = rules5e::SpellEffectKind::Save; d.save_ability = *ab; d.fixed_dc = int8_t(dc); d.on_save = rules5e::OnSave::None;
            rules5e::SpellEffect x; x.kind = rules5e::SpellEffectKind::ForcedMove; x.forced = fm; x.affected_only = true;   // failed savers
            d.extra_effects.push_back(x);
        } else { d.kind = rules5e::SpellEffectKind::ForcedMove; d.forced = fm; }
        e.kind = RK::Effect; e.spell_id = d.id;
        rc.synth.push_back(std::move(d));
        out.push_back(e);
        return std::nullopt;
    }
    if (k == "apply_modifier") {   // 7.2zb Open Hand: a synthetic ApplyModifier SpellDef on the creature hit (no_reactions)
        for (auto it = r.begin(); it != r.end(); ++it)
            if (it.key() != "kind" && it.key() != "modifier" && it.key() != "when")
                return where + ": apply_modifier: unknown key '" + it.key() + "'";
        const auto m = r.find("modifier");
        if (m == r.end()) return where + ": apply_modifier needs modifier";
        rules5e::SpellDef d;
        d.content_id = rc.arch + "#apply_modifier." + std::to_string(rc.synth.size());
        d.id = rules5e::spell_id_of(d.content_id);
        d.level = 0; d.casting_time = rules5e::CastingTime::Action; d.is_spell = false; d.magical = false; d.targets = 1;
        d.kind = rules5e::SpellEffectKind::ApplyModifier;
        if (auto er = rules5e_content::parse_modifier(*m, d.modifier); !er.empty()) return where + ": apply_modifier: " + er;
        if (d.modifier.applies_to == rules5e::ModAppliesTo::Self) return where + ": apply_modifier: applies to the creature hit, not self";
        d.modifier.id = rules5e::spell_id_of(d.content_id + "#modifier");
        e.kind = RK::Effect; e.spell_id = d.id;
        rc.synth.push_back(std::move(d));
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
        if (auto er = compile_save_action(rc.arch, act, d, sa, doc, unsupported, true)) return er;
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
        else if (dk == "until_end_of_caster_next_turn") d.expiry = uint8_t(rules5e::Expiry::EndOfCasterNextTurn);   // #183 expiry
        else if (dk == "until_start_of_target_turn") d.expiry = uint8_t(rules5e::Expiry::StartOfTargetNextTurn);    // #183 expiry
        else return where + ": unsupported apply_condition duration";
        e.kind = RK::ApplyCondition;
        e.spell_id = d.id;
        rc.synth.push_back(std::move(d));
        out.push_back(e);
        return std::nullopt;
    }
    // heal_self / temp_hp / set_hp: fired by rules5e run_event_triggers on the event triggers effect_applies allows
    // (heal_self / temp_hp: every event but hit / crit; set_hp: drop_to_zero); temp HP keeps the larger pool (SRD: no stacking).
    static const std::pair<const char*, RK> kinds[] = {{"heal_self", RK::HealSelf}, {"temp_hp", RK::TempHp}, {"set_hp", RK::SetHp}};
    for (const auto& [name, kind] : kinds)
        if (k == name) {
            e.kind = kind;
            e.value = int16_t(geti(r, "value", geti(r, "amount")));
            // common@2 additive `amount_from` {ability, class, min} (Dark One's Blessing): ability modifier + that class's level
            // (owner_level rules, like scale.per_levels), at least `min` (default 1); computed at load, integer math.
            if (auto af = r.find("amount_from"); af != r.end()) {
                if (kind == RK::SetHp || !af->is_object()) return where + ": amount_from needs heal_self / temp_hp and an object";
                const auto ab = ability_of(af->value("ability", ""));
                if (!ab) return where + ": bad amount_from ability";
                static const char* abn[] = {"str", "dex", "con", "int", "wis", "cha"};
                const auto abil = doc.find("abilities");
                const int score = abil != doc.end() && abil->is_object() ? geti(*abil, abn[int(*ab)], 10) : 10;
                const int mod = (score >= 10 ? score - 10 : score - 11) / 2;   // floor((score - 10) / 2)
                const int mn = geti(*af, "min", 1);
                if (mn < 0 || mn > 32767) return where + ": bad amount_from min";
                const int lvl = af->contains("class") ? owner_level(doc, *af) : 0;
                e.value = int16_t(std::min(32767, std::max(mn, mod + lvl)));
            }
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
    bool effect_frenzy = false;   // 7.2z: {kind: bonus_attack, requires_effect: "<feature id>"} (a melee weapon attack)
    for (const auto& f : feats) {
        if (f.kind != "bonus_attack") continue;
        for (auto it = f.effect.begin(); it != f.effect.end(); ++it)
            if (it.key() != "kind" && it.key() != "requires_effect") return aid + ": " + f.id + ": bonus_attack: unknown key '" + it.key() + "'";
        const std::string req = f.effect.value("requires_effect", "");
        bool found = false;
        for (const auto& g : feats) found = found || (g.id == req && g.kind == "apply_modifier" && !g.effect.contains("passive"));
        if (!found) return aid + ": " + f.id + ": requires_effect '" + req + "' is not an apply_modifier feature of this character";
        rules5e::BonusAttack b;
        b.any_melee = true;
        b.requires_modifier = rules5e::spell_id_of(aid + "#" + req + "#m0");
        add(f.id, b);
        effect_frenzy = effect_frenzy || f.id == "frenzy";
    }
    if (has_feature("frenzy") && !effect_frenzy) {
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
                                            std::map<uint32_t, std::string>& ids, rules5e::Combatant& c, bool& unsupported,
                                            std::vector<Archetype::Resource>& usage_pools) {
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
    if (auto po = effect.find("pick_one"); po != effect.end()) {   // 7.2zb Open Hand: the attacker applies one of effects[]
        if (!po->is_boolean()) return where + ": pick_one must be a boolean";
        t.pick_one = po->get<bool>();
        if (t.pick_one && (t.choice != 1 || (t.on != rules5e::TriggerOn::Hit && t.on != rules5e::TriggerOn::Crit)))
            return where + ": pick_one needs choice ask on a hit or crit trigger";
    }
    // usage (common@2 $defs/usage): at_will = none; per_day / per_rest {uses, rest} = a trigger-owned pool
    // "<owner>#usage" (registered by Combat::create like resources[], recharged by rests, folded with the pools, 0xA3) paid by a
    // Spend rider; pool {pool, cost} = Spend from a declared resources[] pool. recharge stays unsupported (listed, not compiled).
    std::optional<rules5e::RiderEffect> usage_spend;
    if (auto u = effect.find("usage"); u != effect.end()) {
        if (!u->is_object()) return where + ": bad usage";
        const std::string k = u->value("kind", "");
        if (k == "per_day" || k == "per_rest") {
            const int uses = geti(*u, "uses");
            if (uses < 1 || uses > 255) return where + ": bad uses";
            const std::string rest = u->value("rest", "long");
            if (k == "per_rest" && rest != "short" && rest != "long") return where + ": bad rest";
            Archetype::Resource r;
            r.id = owner + "#usage";
            r.pool_id = rules5e::spell_id_of(arch + "#" + r.id);
            r.max = r.start = uint16_t(uses);
            r.recharge = uint8_t(k == "per_rest" && rest == "short" ? rules5e::FeatureRecharge::ShortRest : rules5e::FeatureRecharge::LongRest);
            rules5e::RiderEffect sp;
            sp.kind = rules5e::RiderKind::Spend;
            sp.pool = r.pool_id;
            sp.value = 1;
            usage_spend = sp;
            usage_pools.push_back(std::move(r));
        } else if (k == "pool") {
            const std::string pid = u->value("pool", "");
            const int cost = geti(*u, "cost", 1);
            if (pid.empty() || cost < 1 || cost > 255) return where + ": bad usage pool";
            bool declared_pool = false;
            for (const auto& r : usage_pools) declared_pool = declared_pool || r.id == pid;
            if (!declared_pool) return where + ": usage pool '" + pid + "' is not a declared resource";
            rules5e::RiderEffect sp;
            sp.kind = rules5e::RiderKind::Spend;
            sp.pool = rules5e::spell_id_of(pid);
            sp.value = int16_t(cost);
            usage_spend = sp;
        } else if (k == "recharge") {   // EC deferred monsters: an ActionUse row keyed (owner, trigger id), Combat::create
            const int mn = geti(*u, "min");
            if (mn < 2 || mn > 6) return where + ": bad recharge min";
            t.recharge_min = uint8_t(mn);
        } else if (k != "at_will") { unsupported = true; return std::nullopt; }
    }
    if (auto w = effect.find("when"); w != effect.end()) {
        if (!w->is_array()) return where + ": bad when";
        for (const Json& p : *w) {
            rules5e::Predicate pr;
            if (auto e = compile_predicate(where, p, pr)) return e;
            if (pr.kind == rules5e::PredicateKind::AttackFromFeature) {   // 7.2zb: the compiled bonus attack of that feature
                const std::string fid = p.value("feature", "");
                bool feature_found = false, is_bonus = false;   // the validator's check_monk_warlock_hooks uses the same strings
                for (const char* key : {"traits", "features"})
                    if (auto fs = doc.find(key); fs != doc.end() && fs->is_array())
                        for (const Json& f : *fs) {
                            if (!f.is_object() || f.value("id", "") != fid) continue;
                            feature_found = true;
                            const auto ef = f.find("effect");
                            is_bonus = is_bonus || fid == "martial_arts" || fid == "flurry_of_blows" || fid == "frenzy" ||
                                       (ef != f.end() && ef->is_object() && ef->value("kind", "") == "bonus_attack" && !ef->value("inert", false));
                        }
                if (!feature_found) return where + ": attack_from_feature names unknown feature '" + fid + "'";
                if (!is_bonus) return where + ": attack_from_feature '" + fid + "' is not a bonus-attack feature of this document";
                pr.mask = rules5e::spell_id_of(arch + "#bonus#" + fid);
            }
            t.when.push_back(pr);
        }
    }
    auto ef = effect.find("effects");
    if (ef == effect.end() || !ef->is_array() || ef->empty()) return where + ": needs effects[]";
    for (const Json& r : *ef) if (auto e = compile_rider(where, r, doc, rc, t.effects)) return e;
    if (usage_spend) t.effects.push_back(*usage_spend);
    if (t.pick_one)
        for (const auto& e : t.effects) if (e.kind == rules5e::RiderKind::Spend && e.slot != 0) return where + ": pick_one cannot spend a spell slot";
    return add_trigger(arch, owner, 0, std::move(t), ids, c);
}

// Petrifying Gaze (VERSION_BUMP_BATCH 7.2p): a monster@4 / character@4 `gaze` feature
// { range_ft, save: {ability, dc}, condition, repeat_save, escalates, escalate_to, escalate_margin } compiles to a synthetic
// Save SpellDef (SpellBook only, not spells_known: it never is an action) and a Combatant::Gaze{spell_id, range_ft}. Sim fills
// ActionRequest::gaze_seen from range_ft at the target's turn start (Combat::gaze_seen).
std::optional<std::string> compile_gaze(const std::string& arch_id, const std::string& fid, const Json& g, rules5e::SpellDef& d,
                                        rules5e::Combatant::Gaze& out) {
    const std::string where = arch_id + ": gaze '" + fid + "'";
    if (fid.empty() || fid.find('#') != std::string::npos || arch_id.find('#') != std::string::npos)
        return where + ": id must be non-empty and contain no '#'";
    const int range = geti(g, "range_ft");
    if (range < 5 || range > 65530 || range % 5 != 0) return where + ": bad range_ft";
    const auto sv = g.find("save");
    if (sv == g.end() || !sv->is_object()) return where + ": missing save";
    const auto ab = ability_of(sv->value("ability", ""));
    const int dc = geti(*sv, "dc");
    if (!ab || dc < 1 || dc > 30) return where + ": bad save";
    const auto cond = condition_of(g.value("condition", "restrained"));
    if (!cond) return where + ": bad condition";
    const std::string rs = g.value("repeat_save", "end_of_turn");
    const rules5e::RepeatSave rep = rs == "end_of_turn" ? rules5e::RepeatSave::EndOfTurn
                                  : rs == "start_of_turn" ? rules5e::RepeatSave::StartOfTurn : rules5e::RepeatSave::None;
    if (rep == rules5e::RepeatSave::None && rs != "none") return where + ": bad repeat_save";
    const bool esc = g.value("escalates", false);
    const auto to = condition_of(g.value("escalate_to", "petrified"));
    if (!to) return where + ": bad escalate_to";
    const int margin = geti(g, "escalate_margin", 0);
    if (margin < 0 || margin > 30) return where + ": bad escalate_margin";
    d = rules5e::SpellDef{};
    d.content_id = arch_id + "#" + fid;
    d.id = rules5e::spell_id_of(d.content_id);
    d.level = 0;
    d.kind = rules5e::SpellEffectKind::Save;
    d.is_spell = false;   // not a spell: no SpellCast window, no components, not Counterspellable
    d.targets = 1;
    d.range_ft = uint16_t(range);
    d.save_ability = *ab;
    d.fixed_dc = int8_t(dc);
    d.applies_condition = true;
    d.condition = *cond;
    d.repeat_save = rep;
    d.escalates = esc;
    d.escalate_to = *to;
    d.escalate_margin = uint8_t(margin);
    out = rules5e::Combatant::Gaze{d.id, uint16_t(range)};
    return std::nullopt;
}

// Action usage (save_effect, attack_effect): at_will, recharge N-6 (ActionUse rows, rolled at the creature's turn start), per
// day / rest (FeatureUses), pool. Pools need resource rows: such actions are listed (unsupported), not compiled.
std::optional<std::string> compile_action_usage(const std::string& where, const Json& act, const Json& doc, rules5e::SpellDef& d,
                                                Archetype::SaveAction& sa, bool& unsupported) {
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

// Batch 3 (Lich ruling): monster@5 action {kind: "teleport", feet, usage?, with_attack?}. SRD 5.1 blink dog: "Teleport (Recharge
// 4–6). The dog magically teleports, along with any equipment it is wearing or carrying, up to 40 ft. to an unoccupied space it can
// see. Before or after teleporting, the dog can make one bite attack." A synthetic Teleport SpellDef (is_spell false, magical,
// self, sim destination checks); with_attack arms a follow-up AttackSequence (after_action) once the teleport is used, so the
// bite comes after the teleport. "Before" (bite, then teleport in the same action) is deferred: it needs a pending-teleport flag.
std::optional<std::string> compile_teleport_action(const std::string& arch, const Json& act, uint32_t declared,
                                                   const std::vector<const Json*>& attack_actions, rules5e::SpellDef& d,
                                                   Archetype::SaveAction& sa, rules5e::Combatant& c,
                                                   std::deque<std::string>& strings, const Json& doc) {
    const std::string aid = act.value("id", "");
    const std::string where = arch + ": action " + aid;
    if (declared != 0 && declared < 5) return where + ": teleport needs a monster@5 file";
    for (auto it = act.begin(); it != act.end(); ++it)
        if (it.key() != "id" && it.key() != "name" && it.key() != "kind" && it.key() != "description" && it.key() != "feet" &&
            it.key() != "usage" && it.key() != "with_attack")
            return where + ": teleport: unknown key '" + it.key() + "'";
    const int ft = act.contains("feet") && act["feet"].is_number_integer() ? act["feet"].get<int>() : 0;
    if (ft < 5 || ft > 500 || ft % 5 != 0) return where + ": teleport: feet must be 5..500, a multiple of 5";
    d.content_id = arch + "#" + aid;
    d.id = rules5e::spell_id_of(d.content_id);
    d.level = 0; d.casting_time = rules5e::CastingTime::Action; d.is_spell = false; d.magical = true; d.targets = 1;
    d.kind = rules5e::SpellEffectKind::Teleport;
    d.teleport.max_feet = uint16_t(ft);
    d.teleport.needs_sight = true;   // "to an unoccupied space it can see"
    bool unsupported = false;
    if (auto e = compile_action_usage(where, act, doc, d, sa, unsupported)) return e;
    if (unsupported) return where + ": teleport: pool usage is not supported";
    sa.id = aid;
    sa.spell_id = d.id;
    sa.teleport = true;
    if (auto w = act.find("with_attack"); w != act.end()) {
        const std::string wid = w->is_string() ? w->get<std::string>() : "";
        int idx = -1;
        for (std::size_t i = 0; i < attack_actions.size() && idx < 0; ++i)
            if (!wid.empty() && attack_actions[i]->value("id", "") == wid) idx = int(i);
        if (idx < 0) return where + ": teleport: with_attack names unknown attack action '" + wid + "'";
        strings.push_back(aid + ".with_attack");
        rules5e::AttackSequence seq{strings.back(), {uint8_t(idx)}};
        seq.after_action = d.id;
        c.multiattacks.push_back(std::move(seq));
    }
    return std::nullopt;
}

// EC deferred monsters (giant spider Web): a monster@4 / character@4 `attack_effect` action, an attack roll that only applies
// conditions: { id, attack: "ranged" | "melee", to_hit, range_ft, apply: [{ condition, escape: { ability: "str", dc } }],
// usage }. Compiles to a synthetic SpellAttack SpellDef (is_spell false, not magical, fixed_attack, no damage) whose
// ApplyCondition riders resolve on a hit; `escape` = rules5e escape_dc (an action, a Strength check: special 4). Every apply[]
// entry needs `escape` (no other duration yet). New content keys (T&C schema pending).
// 7.2w grab and contain: an apply[] entry's optional `contain` {kind engulf | swallow, damage {dice, type}} and `max_size`
// (tiny .. gargantuan: the condition skips a larger target). Filled into the effect-shaped fields of `T` (SpellDef or SpellEffect).
template <class T>
std::optional<std::string> read_contain(const Json& x, const std::string& where, T& e) {
    if (auto ms = x.find("max_size"); ms != x.end()) {
        static const char* kSizes[] = {"tiny", "small", "medium", "large", "huge", "gargantuan"};
        int k = -1;
        for (int i = 0; i < 6; ++i) if (ms->is_string() && ms->get<std::string>() == kSizes[i]) k = i;
        if (k < 0) return where + ": bad max_size in apply[]";
        e.condition_max_size = int8_t(k);
    }
    if (auto ct = x.find("contain"); ct != x.end()) {
        if (!ct->is_object()) return where + ": bad contain";
        const std::string k = ct->value("kind", "");
        if (k != "engulf" && k != "swallow") return where + ": contain kind must be engulf or swallow";
        e.contain = k == "engulf" ? 1 : 2;
        const auto dm = ct->find("damage");
        if (dm == ct->end() || !dm->is_object()) return where + ": contain needs damage {dice, type}";
        auto dice = parse_dice(dm->value("dice", ""));
        auto ty = damage_type(dm->value("type", ""));
        if (!dice || !ty) return where + ": bad contain damage";
        e.contain_dice = *dice; e.contain_damage_type = *ty;
        if (auto rg = ct->find("regurgitate"); rg != ct->end()) {   // behir / purple worm: {dc, damage (default 30)}
            if (k != "swallow" || !rg->is_object()) return where + ": regurgitate is a swallow option {dc, damage}";
            const int dc = geti(*rg, "dc"), dm2 = geti(*rg, "damage", 30);
            if (dc < 1 || dc > 30 || dm2 < 1 || dm2 > 255) return where + ": bad regurgitate (dc 1..30, damage 1..255)";
            e.regurgitate_dc = int8_t(dc); e.regurgitate_damage = uint8_t(dm2);
        }
    }
    if (auto ef = x.find("excludes_formless"); ef != x.end()) {   // the Net: no effect on a formless creature
        if (!ef->is_boolean()) return where + ": bad excludes_formless";
        e.excludes_formless = ef->get<bool>();
    }
    return std::nullopt;
}

std::optional<std::string> compile_attack_effect(const std::string& arch_id, const Json& act, rules5e::SpellDef& d,
                                                 Archetype::SaveAction& sa, const Json& doc, bool& unsupported) {
    const std::string aid = act.value("id", "");
    const std::string where = arch_id + ": attack_effect '" + aid + "'";
    if (aid.empty() || aid.find('#') != std::string::npos) return where + ": id must be non-empty and contain no '#'";
    d = rules5e::SpellDef{};
    d.content_id = arch_id + "#" + aid;
    d.id = rules5e::spell_id_of(d.content_id);
    d.level = 0;
    d.casting_time = rules5e::CastingTime::Action;
    d.kind = rules5e::SpellEffectKind::SpellAttack;
    d.is_spell = false;
    d.magical = false;
    d.targets = 1;
    const std::string mode = act.value("attack", "ranged");
    if (mode != "ranged" && mode != "melee") return where + ": bad attack";
    d.melee = mode == "melee";
    const int hit = geti(act, "to_hit", -100);
    if (hit < -20 || hit > 30 || hit == 0) return where + ": bad to_hit (non-zero, -20..30)";
    d.fixed_attack = int8_t(hit);
    const int range = geti(act, "range_ft", 5);
    if (range < 5 || range > 65535) return where + ": bad range_ft";
    d.range_ft = uint16_t(range);
    // EC spider follow-up: long range (ranged only): legal out to long_range_ft, at disadvantage beyond range_ft
    if (act.contains("long_range_ft")) {
        const int lr = geti(act, "long_range_ft");
        if (d.melee || lr <= range || lr > 65535) return where + ": bad long_range_ft (ranged, > range_ft)";
        d.long_range_ft = uint16_t(lr);
    }
    sa = {};
    sa.id = aid;
    sa.spell_id = d.id;
    sa.range_ft = d.range_ft;
    const auto ap = act.find("apply");
    if (ap == act.end() || !ap->is_array() || ap->empty()) return where + ": needs apply[]";
    for (const Json& x : *ap) {
        auto cond = condition_of(x.is_object() ? x.value("condition", "") : "");
        if (!cond) return where + ": bad condition in apply[]";
        const auto es = x.find("escape");
        const bool swallow = x.contains("contain") && x["contain"].is_object() && x["contain"].value("kind", "") == "swallow";
        if (rules5e::SpellEffect probe; x.contains("contain") || x.contains("max_size"))   // 7.2w: bad keys are errors first
            if (auto m = read_contain(x, where, probe)) return m;
        if ((es == x.end() || !es->is_object()) && !swallow) { unsupported = true; return std::nullopt; }
        int dc = 0;
        if (es != x.end()) {
            if (!es->is_object() || es->value("ability", "") != "str") return where + ": escape ability must be str";
            dc = geti(*es, "dc");
            if (dc < 1 || dc > 30) return where + ": bad escape dc";
        }
        rules5e::SpellEffect e;
        if (auto m = read_contain(x, where, e)) return m;   // 7.2w: Swallow (no escape: freed when the swallower dies), Net max_size
        e.kind = rules5e::SpellEffectKind::ApplyCondition;
        e.applies_condition = true;
        e.condition = *cond;
        e.affected_only = true;   // only a creature the attack hit
        e.escape_dc = int8_t(dc);
        // EC spider follow-up: object {ac, hp, vulnerable[], immune[]}: the restraint is a destructible object (special 6)
        if (const auto ob = x.find("object"); ob != x.end()) {
            if (!ob->is_object()) return where + ": bad object";
            const int oac = geti(*ob, "ac"), ohp = geti(*ob, "hp");
            if (oac < 1 || oac > 30 || ohp < 1 || ohp > 1000) return where + ": bad object ac / hp";
            e.object_ac = int8_t(oac);
            e.object_hp = int16_t(ohp);
            for (const char* key : {"vulnerable", "immune"}) {
                uint32_t& m = key[0] == 'v' ? e.object_vulnerable : e.object_immune;
                if (auto ls = ob->find(key); ls != ob->end()) {
                    if (!ls->is_array()) return where + ": bad object " + key;
                    for (const Json& n : *ls) {
                        auto ty = damage_type(n.is_string() ? n.get<std::string>() : "");
                        if (!ty) return where + ": bad damage type in object " + key;
                        m |= 1u << unsigned(*ty);
                    }
                }
            }
        }
        d.extra_effects.push_back(e);
    }
    return compile_action_usage(where, act, doc, d, sa, unsupported);
}

std::optional<std::string> compile_save_action(const std::string& arch_id, const Json& act, rules5e::SpellDef& d,
                                               Archetype::SaveAction& sa, const Json& doc, bool& unsupported,
                                               bool untimed_ok) {
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
    // EC spider follow-up: zero_hp {condition, while, duration {kind rounds, n}}: when the (first) damage drops the target to
    // 0 HP it is stable and gains `condition` for n rounds (600 = 1 hour) and `while` for as long as that lasts.
    if (auto zh = act.find("zero_hp"); zh != act.end()) {
        if (!zh->is_object() || (d.dice.count == 0 && d.dice.bonus == 0)) return where + ": zero_hp needs damage";
        auto c1 = condition_of(zh->value("condition", "")), c2 = condition_of(zh->value("while", ""));
        if (!c1 || !c2 || *c1 == *c2) return where + ": bad zero_hp condition / while";
        const auto du = zh->find("duration");
        const int n = du != zh->end() && du->is_object() && du->value("kind", "") == "rounds" ? geti(*du, "n") : 0;
        if (n < 1 || n > 65535) return where + ": bad zero_hp duration (rounds 1..65535)";
        d.zero_hp_rounds = uint16_t(n);
        d.zero_hp_condition = *c1;
        d.zero_hp_while = *c2;
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
            // which the engine does not model for conditions yet: listed, not compiled. Exception (`untimed_ok`, save_condition
            // riders): a condition that ends by its own rules (kEndsByOwnRules: prone) is applied untracked, with no timer.
            const std::string rs = x.value("repeat_save", "");
            const bool has_dur = x.contains("duration");
            const bool untracked = (untimed_ok && !has_dur && rs.empty() && ends_by_own_rules(*cond)) ||
                                   (has_dur && x["duration"].is_object() && x["duration"].value("kind", "") == "until_cured");
            // 7.2w: an escape {ability str, dc} (Engulf) lasts until the creature escapes: no duration needed
            int esc = 0;
            if (auto es = x.find("escape"); es != x.end()) {
                if (!es->is_object() || es->value("ability", "") != "str") return where + ": escape ability must be str";
                esc = geti(*es, "dc");
                if (esc < 1 || esc > 30) return where + ": bad escape dc";
            }
            if (has_dur && !x["duration"].is_object()) return where + ": bad duration in apply[]";
            const Json dur = has_dur ? x["duration"] : Json::object();
            const std::string dk = dur.is_object() && dur.contains("kind") && dur["kind"].is_string() ? dur["kind"].get<std::string>() : "";
            uint8_t expiry = 0;
            uint16_t rounds = 0;
            const bool until_cured = dk == "until_cured";
            if (until_cured) {
                if (!cured_only_ok(*cond))
                    return where + ": until_cured needs blinded, deafened, paralyzed or poisoned (a condition Lesser Restoration ends)";
                if (!rs.empty()) return where + ": until_cured takes no repeat_save";
                if (esc > 0) return where + ": until_cured takes no escape";
            }
            if (until_cured) {
                // no expiry, no repeat save: untracked below
            } else if (dk == "rounds") {
                const int n = geti(dur, "n");
                if (n < 1 || n > 65535) return where + ": bad duration rounds";
                expiry = uint8_t(rules5e::Expiry::Rounds);
                rounds = uint16_t(n);
            } else if (dk == "until_end_of_target_next_turn") {
                expiry = uint8_t(rules5e::Expiry::EndOfTargetNextTurn);
            } else if (dk == "until_end_of_caster_next_turn") {   // common@2 modifier_duration; rules5e Expiry since #183
                expiry = uint8_t(rules5e::Expiry::EndOfCasterNextTurn);
            } else if (dk == "until_start_of_target_turn") {      // likewise (ends as the target's next turn starts)
                expiry = uint8_t(rules5e::Expiry::StartOfTargetNextTurn);
            } else if (dk != "until_save" && (has_dur || rs.empty()) && !untracked && !(esc > 0 && !has_dur)) {
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
                d.condition_untracked = untracked;
                d.escape_dc = int8_t(esc);
                if (x.contains("max_size") && !x.contains("contain"))
                    return where + ": max_size on a save_effect needs contain (only a containing condition skips a larger target)";
                if (auto m = read_contain(x, where, d)) return m;
                first = false;
            } else {       // further conditions: ApplyCondition on the creatures that failed the save
                rules5e::SpellEffect e;
                e.kind = rules5e::SpellEffectKind::ApplyCondition;
                e.applies_condition = true;
                e.condition = *cond;
                e.repeat_save = rep;
                e.expiry = expiry;
                e.duration_rounds = rounds;
                e.condition_untracked = untracked;
                e.affected_only = true;
                e.escape_dc = int8_t(esc);
                if (x.contains("max_size") && !x.contains("contain"))
                    return where + ": max_size on a save_effect needs contain (only a containing condition skips a larger target)";
                if (auto m = read_contain(x, where, e)) return m;
                d.extra_effects.push_back(e);
            }
        }
    }
    // 7.2y class feature hooks (T&C): the spell@1 control keys, target_creature_types and a control duration.
    if (auto tt = act.find("target_creature_types"); tt != act.end()) {
        if (!tt->is_array() || tt->empty()) return where + ": bad target_creature_types";
        for (const Json& t : *tt) {
            auto c = creature_type_of(t.is_string() ? t.get<std::string>() : "");
            if (!c) return where + ": bad target_creature_types";
            d.target_types |= rules5e::bit(*c);
        }
    }
    if (auto ck = act.find("control"); ck != act.end()) {
        const std::string k = ck->is_string() ? ck->get<std::string>() : "";
        d.control = k == "turned" ? rules5e::ControlKind::Turned : k == "fear" ? rules5e::ControlKind::Fear
                  : k == "laughter" ? rules5e::ControlKind::Laughter : k == "command" ? rules5e::ControlKind::Command
                  : rules5e::ControlKind::None;
        if (d.control == rules5e::ControlKind::None) return where + ": bad control";
        if (d.control == rules5e::ControlKind::Command) return where + ": control command needs a spell (the word is a cast choice)";
        if (auto ex = act.find("control_excludes_creature_types"); ex != act.end()) {
            if (!ex->is_array() || ex->empty()) return where + ": bad control_excludes_creature_types";
            for (const Json& t : *ex) {
                auto c = creature_type_of(t.is_string() ? t.get<std::string>() : "");
                if (!c) return where + ": bad control_excludes_creature_types";
                d.control_excluded |= rules5e::bit(*c);
            }
        }
        if (auto du = act.find("destroy_undead_max_cr8"); du != act.end()) {
            const int n = du->is_number_integer() ? du->get<int>() : 0;
            if (n != 4 && n != 8 && n != 16 && n != 24 && n != 32) return where + ": destroy_undead_max_cr8 must be 4, 8, 16, 24 or 32";
            d.destroy_cr8_max = uint16_t(n);
        }
        if (auto du = act.find("duration"); du != act.end()) {   // the control effect's duration: rounds n (Turn Undead: 10)
            const int n = du->is_object() && du->value("kind", "") == "rounds" ? geti(*du, "n") : 0;
            if (n < 1 || n > 65535) return where + ": bad control duration (rounds 1..65535)";
            d.expiry = uint8_t(rules5e::Expiry::Rounds);
            d.duration_rounds = uint16_t(n);
        }
    } else if (act.contains("control_excludes_creature_types") || act.contains("destroy_undead_max_cr8") || act.contains("duration")) {
        return where + ": control_excludes_creature_types / destroy_undead_max_cr8 / duration need control";
    }
    if (d.dice.count == 0 && d.dice.bonus == 0 && !d.applies_condition && d.control == rules5e::ControlKind::None)
        return where + ": needs damage[], apply[] or control";
    if (auto e = compile_action_usage(where, act, doc, d, sa, unsupported); e || unsupported) return e;
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
        x.requires_sight = w->value("requires_sight", false);   // 7.2y Uncanny Dodge: an attacker the reactor can see
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
            if (auto at = ef->find("applies_to_attack"); at != ef->end()) {   // 7.2zb Deflect Missiles
                if (!at->is_array() || at->empty()) return where + ": applies_to_attack must be a non-empty list";
                for (const Json& v : *at) {
                    const std::string n = v.is_string() ? v.get<std::string>() : "";
                    const uint8_t b = n == "melee_weapon" ? rules5e::kAttackKindMeleeWeapon : n == "ranged_weapon" ? rules5e::kAttackKindRangedWeapon
                                    : n == "melee_spell" ? rules5e::kAttackKindMeleeSpell : n == "ranged_spell" ? rules5e::kAttackKindRangedSpell : 0;
                    if (b == 0 || (x.attack_kinds & b)) return where + ": applies_to_attack: bad or repeated '" + n + "'";
                    x.attack_kinds = uint8_t(x.attack_kinds | b);
                }
            }
        } else if (ek == "subtract_roll") {   // R-G7 Cutting Words: subtract `dice` (the bardic die) from an enemy's attack roll
            if (!attack_rolled) return where + ": subtract_roll needs the roll_attack window";
            x.kind = RE::SubtractRoll;
            auto d = parse_dice(ef->contains("dice") && (*ef)["dice"].is_string() ? (*ef)["dice"].get<std::string>() : "");
            if (!d || d->count == 0) return where + ": subtract_roll needs dice";
            x.dice = *d;
            if (auto ap = ef->find("applies_to"); ap != ef->end()) {   // 7.2za: attack | save | check | damage (unique); absent = attack
                static const char* const kinds[] = {"attack", "save", "check", "damage"};
                bool ok = ap->is_array() && !ap->empty();
                if (ok) for (const Json& v : *ap) {
                    uint8_t hit = 0;
                    for (int i = 0; i < 4; ++i) if (v.is_string() && v.get<std::string>() == kinds[i]) hit = uint8_t(1u << i);
                    if (hit == 0 || (x.applies & hit)) { ok = false; break; }
                    x.applies = uint8_t(x.applies | hit);
                }
                if (!ok) return where + ": subtract_roll: bad applies_to";
            }
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
std::optional<rules5e::CreatureType> creature_type_from_name(const std::string& s) { return creature_type_of(s); }

std::optional<int> cr_x8_of(const Json& r) {
    if (r.is_number_integer()) {
        const int64_t v = r.get<int64_t>();
        if (v < 0 || v > 30) return std::nullopt;
        return int(v) * 8;
    }
    if (!r.is_string()) return std::nullopt;
    const std::string t = r.get<std::string>();
    if (t == "0") return 0;
    if (t == "1/8") return 1;
    if (t == "1/4") return 2;
    if (t == "1/2") return 4;
    if (t.empty() || t.size() > 2 || t.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
    const int v = std::stoi(t);
    if (v > 30) return std::nullopt;
    return v * 8;
}

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
    // R-G12 (7.2q): facts build_form checks (CR, fly / swim speeds, character level). Data only: nothing reads them unless a
    // transform is requested.
    a.fly_ft = uint16_t(std::max(0, geti(doc.at("speed"), "fly", 0)));
    a.swim_ft = uint16_t(std::max(0, geti(doc.at("speed"), "swim", 0)));
    // feat/move-modes: the rules' movement modes (fly / swim / climb speeds, hover).
    c.fly_ft = uint8_t(std::min<int>(255, a.fly_ft));
    c.swim_ft = uint8_t(std::min<int>(255, a.swim_ft));
    c.climb_ft = uint8_t(std::clamp(geti(doc.at("speed"), "climb", 0), 0, 255));
    if (auto hv = doc.at("speed").find("hover"); hv != doc.at("speed").end() && hv->is_boolean()) c.hover = hv->get<bool>();
    if (auto ch = doc.find("challenge"); ch != doc.end() && ch->is_object())
        if (auto rt = ch->find("rating"); rt != ch->end())
            if (auto x = cr_x8_of(*rt)) a.cr_x8 = int16_t(*x);
    a.base.cr8 = a.cr_x8;   // 7.2v Destroy Undead reads Combatant::cr8 (-1 = no challenge rating)
    if (schema_type == "character") {
        int lv = 0;
        for (const Json& k : doc.value("classes", Json::array()))
            if (k.is_object()) lv += std::max(0, geti(k, "level"));
        if (lv == 0) lv = geti(doc, "level", 0);
        a.char_level = uint8_t(std::clamp(lv, 0, 20));
    }
    c.proficiency_bonus = int8_t(geti(doc, "proficiency_bonus", 2));
    // R-G11 rules PR 2 creature facts (CONTENT_CONVENTIONS 4.6; schema keys from #142): Stinking Cloud's no_breath automatic
    // success, Moonbeam's shapechanger disadvantage, Spirit Guardians' evil damage type. Default false.
    c.no_breath = doc.value("no_breath", false);
    c.shapechanger = doc.value("shapechanger", false);
    c.evil = doc.value("evil", false);
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
        // R-G12 sim: the creature itself carries its size (Heavy weapons, forms); an inert size stays Medium, matching footprint 1.
        if (!inert.count("size")) c.size = rules5e::Size(uint8_t(a.size));
    }
    if (!inert.count("creature_type") && doc.contains("type")) {
        auto ct = creature_type_of(doc.value("type", ""));
        if (!ct) return a.id + ": bad creature type";
        c.creature_type = *ct;
    }
    // EC deferred monsters: content tags for TargetTag / NotTargetTag (monster tags[], a character's race; elf races add "elf",
    // half_elf does not). Strings are case-folded.
    {
        auto add_tag = [&](std::string t) {
            for (auto& ch : t) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            const uint32_t id = rules5e::spell_id_of(t);
            if (std::find(c.tags.begin(), c.tags.end(), id) == c.tags.end()) c.tags.push_back(id);
        };
        if (auto tg = doc.find("tags"); tg != doc.end() && tg->is_array())
            for (const Json& t : *tg) if (t.is_string()) add_tag(t.get<std::string>());
        if (auto rc = doc.find("race"); rc != doc.end() && rc->is_string()) {
            const std::string r = rc->get<std::string>();
            add_tag(r);
            if (r != "half_elf" && r.size() >= 3 && r.compare(r.size() - 3, 3, "elf") == 0) add_tag("elf");
        }
        // EC swarm: the "swarm" tag turns on the SRD Swarm trait
        if (std::find(c.tags.begin(), c.tags.end(), rules5e::spell_id_of("swarm")) != c.tags.end()) a.swarm = c.no_regain_hp = true;
        // 7.2w: the "formless" tag (SRD: the Net has no effect on it)
        if (std::find(c.tags.begin(), c.tags.end(), rules5e::spell_id_of("formless")) != c.tags.end()) c.formless = true;
    }
    // R-G13 phase 2: senses (static content, not folded into any hash; 0 = none). The schema's range_ft is a non-negative
    // multiple of 5; values above 65535 are refused.
    if (auto se = doc.find("senses"); se != doc.end() && se->is_object()) {
        struct SenseKey { const char* key; uint16_t rules5e::Combatant::*field; };
        static const SenseKey keys[] = {{"darkvision", &rules5e::Combatant::darkvision_ft},
                                        {"blindsight", &rules5e::Combatant::blindsight_ft},
                                        {"tremorsense", &rules5e::Combatant::tremorsense_ft},
                                        {"truesight", &rules5e::Combatant::truesight_ft}};
        for (const SenseKey& k : keys) {
            auto it = se->find(k.key);
            if (it == se->end()) continue;
            if (!it->is_number_integer() || it->get<int64_t>() < 0 || it->get<int64_t>() > 65535)
                return a.id + ": bad senses." + k.key;
            c.*k.field = static_cast<uint16_t>(it->get<int64_t>());
        }
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
    // R-G16 Hit Dice (VERSION_BUMP_BATCH 7.2h hand-off (a)): character classes[].hit_die ("d6".."d12") and hit_dice_max = the
    // summed class levels, all dice unspent. rules5e models one die size, so a multiclass character uses classes[0]'s die.
    // Not part of Combat::hash (rests run outside the encounter), so no digest moves.
    if (c.is_pc)
        if (auto cl = doc.find("classes"); cl != doc.end() && cl->is_array() && !cl->empty()) {
            int total = 0;
            for (const Json& k : *cl)
                if (k.is_object()) total += std::max(0, geti(k, "level"));
            const Json& first = cl->front();
            const std::string hd = first.is_object() ? first.value("hit_die", "") : std::string();
            const int sides = hd.size() >= 2 && hd[0] == 'd' ? std::atoi(hd.c_str() + 1) : 0;
            if (sides == 6 || sides == 8 || sides == 10 || sides == 12) {
                c.hit_die = uint8_t(sides);
                c.hit_dice = c.hit_dice_max = uint8_t(std::clamp(total, 0, 255));
            }
        }
    {   // 7.2y: thieves' tools proficiency (UseObject 3): a rogue (SRD class proficiency) carrying thieves_tools
        bool rogue = false, tools = false;
        for (const Json& cl : doc.value("classes", Json::array())) rogue = rogue || (cl.is_object() && cl.value("class", "").find("rogue") != std::string::npos);
        if (auto eq = doc.find("equipment"); eq != doc.end() && eq->is_array())
            for (const Json& e : *eq) tools = tools || (e.is_string() && e.get<std::string>() == "thieves_tools") ||
                                             (e.is_object() && e.value("item", "").find("thieves_tools") != std::string::npos);
        c.thieves_tools = rogue && tools;
    }
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
        if (auto er = read_bonus_vs(dmg, at.vs_types, at.vs_dice); !er.empty()) return a.id + ": action " + act.value("id", "?") + ": " + er;
        at.ranged = ranged;
        at.finesse = false;
        // EC swarm: "half_hp_damage": "1d6" replaces component 1's dice while the attacker has half its HP or fewer
        if (auto hh = act.find("half_hp_damage"); hh != act.end()) {
            auto hd = hh->is_string() ? parse_dice(hh->get<std::string>()) : std::nullopt;
            if (!hd || hd->count == 0) return a.id + ": bad half_hp_damage in action " + act.value("id", "?");
            at.half_hp_damage = *hd;
        }
        // Batch 3 (Lich ruling): monster@5 `drain` {max_hp: true, save?: {ability: con, dc}}: SRD 5.1 Life Drain (wight, specter):
        // "its hit point maximum is reduced by an amount equal to the damage taken" (rules5e life_drain; long rest restores,
        // a maximum of 0 kills). No save = no saving throw. Static attack data: no hash tag (the reduction is already hashed).
        if (auto dr = act.find("drain"); dr != act.end()) {
            const std::string aid = act.value("id", "?");
            if (declared != 0 && declared < 5) return a.id + ": action " + aid + ": drain needs a monster@5 file";
            if (!dr->is_object()) return a.id + ": action " + aid + ": drain must be an object";
            for (auto it = dr->begin(); it != dr->end(); ++it)
                if (it.key() != "max_hp" && it.key() != "save" && it.key() != "damage_type" && it.key() != "heal_self")
                    return a.id + ": action " + aid + ": drain: unknown key '" + it.key() + "'";
            const auto mh = dr->find("max_hp");
            if (mh == dr->end() || !mh->is_boolean() || !mh->get<bool>()) return a.id + ": action " + aid + ": drain needs max_hp: true";
            at.drain_max_hp = true;
            if (auto sv = dr->find("save"); sv != dr->end()) {
                if (!sv->is_object() || sv->value("ability", "") != "con" || sv->size() != 2 || !sv->contains("dc") ||
                    !(*sv)["dc"].is_number_integer() || (*sv)["dc"].get<int64_t>() < 1 || (*sv)["dc"].get<int64_t>() > 30)
                    return a.id + ": action " + aid + ": drain.save must be {ability: con, dc: 1..30}";
                at.drain_save_dc = int8_t((*sv)["dc"].get<int64_t>());
            }
            // batch 3 vampire spawn Bite: "reduced by an amount equal to the necrotic damage taken, and the vampire regains hit
            // points equal to that amount"
            if (auto dt = dr->find("damage_type"); dt != dr->end()) {
                const auto ty = dt->is_string() ? damage_type(dt->get<std::string>()) : std::nullopt;
                if (!ty) return a.id + ": action " + aid + ": drain.damage_type must be a damage type";
                bool present = false;
                if (auto dm = act.find("damage"); dm != act.end() && dm->is_array())
                    for (const Json& p : *dm) present = present || (p.is_object() && p.value("type", "") == dt->get<std::string>());
                if (!present) return a.id + ": action " + aid + ": drain.damage_type is not one of the action's damage types";
                at.drain_damage_type = int8_t(*ty);
            }
            if (auto hs = dr->find("heal_self"); hs != dr->end()) {
                if (!hs->is_boolean()) return a.id + ": action " + aid + ": drain.heal_self must be a boolean";
                at.drain_heal_self = hs->get<bool>();
            }
        }
        // batch 3 vampire spawn Bite: target_requires_any (at least one): willing, grappled_by_self, incapacitated, restrained
        if (auto tg = act.find("target_requires_any"); tg != act.end()) {
            const std::string aid = act.value("id", "?");
            if (declared != 0 && declared < 5) return a.id + ": action " + aid + ": target_requires_any needs a monster@5 file";
            if (!tg->is_array() || tg->empty()) return a.id + ": action " + aid + ": target_requires_any must be a non-empty array";
            uint8_t g = 0;
            for (const Json& v : *tg) {
                const std::string k = v.is_string() ? v.get<std::string>() : "";
                const uint8_t b = k == "willing" ? rules5e::kTargetGateWilling : k == "grappled_by_self" ? rules5e::kTargetGateGrappledBySelf
                                : k == "incapacitated" ? rules5e::kTargetGateIncapacitated : k == "restrained" ? rules5e::kTargetGateRestrained : 0;
                if (b == 0) return a.id + ": action " + aid + ": target_requires_any: unknown gate '" + k + "'";
                if (g & b) return a.id + ": action " + aid + ": target_requires_any: duplicate gate '" + k + "'";
                g |= b;
            }
            at.target_requires_any = g;
        }
        // Batch 3 (Lich ruling): monster@5 `curse` {no_regain_hp: true, save: {ability: con, dc}}: SRD 5.1 mummy Rotting Fist
        // ("must succeed on a DC 12 Constitution saving throw or be cursed with mummy rot. The cursed target can't regain hit
        // points"). Deferred: the 3d6 maximum loss per 24 hours, dust on 0, Remove Curse (no day clock; spell code untouched).
        if (auto cu = act.find("curse"); cu != act.end()) {
            const std::string aid = act.value("id", "?");
            if (declared != 0 && declared < 5) return a.id + ": action " + aid + ": curse needs a monster@5 file";
            if (!cu->is_object()) return a.id + ": action " + aid + ": curse must be an object";
            for (auto it = cu->begin(); it != cu->end(); ++it)
                if (it.key() != "no_regain_hp" && it.key() != "save") return a.id + ": action " + aid + ": curse: unknown key '" + it.key() + "'";
            const auto nr = cu->find("no_regain_hp");
            if (nr == cu->end() || !nr->is_boolean() || !nr->get<bool>()) return a.id + ": action " + aid + ": curse needs no_regain_hp: true";
            const auto sv = cu->find("save");
            if (sv == cu->end() || !sv->is_object() || sv->value("ability", "") != "con" || sv->size() != 2 || !sv->contains("dc") ||
                !(*sv)["dc"].is_number_integer() || (*sv)["dc"].get<int64_t>() < 1 || (*sv)["dc"].get<int64_t>() > 30)
                return a.id + ": action " + aid + ": curse.save must be {ability: con, dc: 1..30}";
            at.curse_no_regain_dc = int8_t((*sv)["dc"].get<int64_t>());
        }
        // Batch 3: monster@5 `ability_drain` {ability: str, dice}: SRD 5.1 shadow Strength Drain ("the target's Strength score is
        // reduced by 1d4. The target dies if this reduces its Strength to 0. Otherwise, the reduction lasts until the target
        // finishes a short or long rest."). Only Strength (the one SRD 5.1 attack that reduces a score on a hit). Deferred: the
        // new shadow rising from a non-evil humanoid 1d4 hours later (no hour clock or spawn-on-death).
        if (auto ad = act.find("ability_drain"); ad != act.end()) {
            const std::string aid = act.value("id", "?");
            if (declared != 0 && declared < 5) return a.id + ": action " + aid + ": ability_drain needs a monster@5 file";
            if (!ad->is_object() || ad->size() != 2 || ad->value("ability", "") != "str" || !ad->contains("dice") || !(*ad)["dice"].is_string())
                return a.id + ": action " + aid + ": ability_drain must be {ability: str, dice}";
            const auto d = parse_dice((*ad)["dice"].get<std::string>());
            if (!d || d->count < 1) return a.id + ": action " + aid + ": ability_drain: bad dice";
            at.strength_drain = *d;
        }
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
                rules5e::DamagePart part{*xd, *xt};
                if (auto er = read_bonus_vs(extra, part.vs_types, part.vs_dice); !er.empty()) return a.id + ": action " + act.value("id", "?") + ": " + er;
                at.extra_damage.push_back(part);
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
            if (auto e = compile_on_event(a.id, f.id, f.effect, doc, rider_ctx, trigger_ids, c, unsupported, a.resources)) return *e;
            if (unsupported) a.unsupported_features.push_back(f.id + " (on_event: unsupported usage)");
            continue;
        }
        if (f.kind == "gaze") {   // Petrifying Gaze (7.2p): compiled here. Not a registry kind (the 3 -> 4 migration's list is
                                  // frozen), so the @4 gate is explicit: a file that declared < 4 lists it inert.
            if (declared != 0 && declared < 4) {
                a.inert_features.push_back(f.id + " (gaze)");
                continue;
            }
            rules5e::SpellDef d;
            rules5e::Combatant::Gaze gz;
            if (auto e = compile_gaze(a.id, f.id, f.effect, d, gz)) return *e;
            for (const auto& prev : synth)
                if (prev.id == d.id) return a.id + ": gaze ids '" + prev.content_id + "' and '" + d.content_id + "' collide";
            c.gazes.push_back(gz);
            synth.push_back(std::move(d));   // before n_trigger_spells: registered like area_save riders (SpellBook only)
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
                Json rider = (*oh)[ri];
                if (auto rw = rider.find("when"); rw != rider.end()) {   // EC deferred monsters: rider-level predicates (ghoul claws)
                    if (!rw->is_array()) return a.id + ": " + act.value("id", "?") + " on_hit: bad when";
                    for (const Json& p : *rw) {
                        rules5e::Predicate rp;
                        if (auto e = compile_predicate(a.id + ": " + act.value("id", "?") + " on_hit", p, rp)) return *e;
                        t.when.push_back(rp);
                    }
                    rider.erase("when");
                }
                if (auto e = compile_rider(a.id + ": " + act.value("id", "?") + " on_hit", rider, doc, rider_ctx, t.effects)) return *e;
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
        c.multiattacks.push_back({strings_.back(), m.attacks, m.count_dice});
    }
    a.ai = assets::behavior_ai_script(doc);   // monster@2 / character@2
    const std::size_t n_trigger_spells = synth.size();   // the area_save riders' defs come first
    for (std::size_t i = 0; i < n_trigger_spells; ++i) a.trigger_spell_ids.push_back(synth[i].id);
    // monster@4 / character@4 save_effect actions -> synthetic SpellDefs (never for a file that declared < 4).
    if (declared == 0 || declared >= 4)
        for (const Json& act : doc.at("actions")) {
            const std::string ak = act.value("kind", "");
            if (ak != "save_effect" && ak != "attack_effect" && ak != "teleport") continue;
            rules5e::SpellDef d;
            Archetype::SaveAction sa;
            bool unsupported = false;
            if (ak == "teleport") {   // batch 3 (Lich ruling, blink dog): synthetic Teleport SpellDef + optional follow-up attack
                if (auto e = compile_teleport_action(a.id, act, declared, attack_actions, d, sa, c, strings_, doc)) return e;
                for (const auto& prev : synth)
                    if (prev.id == d.id) return a.id + ": teleport ids '" + prev.content_id + "' and '" + d.content_id + "' collide";
                c.spells_known.push_back(d.id);
                a.save_actions.push_back(sa);
                synth.push_back(std::move(d));
                continue;
            }
            if (ak == "attack_effect") {   // EC deferred monsters: giant spider Web
                if (auto e = compile_attack_effect(a.id, act, d, sa, doc, unsupported)) return e;
                if (unsupported) { a.unsupported_features.push_back(act.value("id", "?") + " (attack_effect: unsupported usage or condition duration)"); continue; }
                for (const auto& prev : synth)
                    if (prev.id == d.id) return a.id + ": attack_effect ids '" + prev.content_id + "' and '" + d.content_id + "' collide";
                c.spells_known.push_back(d.id);
                a.save_actions.push_back(sa);
                synth.push_back(std::move(d));
                continue;
            }
            if (auto e = compile_save_action(a.id, act, d, sa, doc, unsupported)) return e;
            if (unsupported) { a.unsupported_features.push_back(act.value("id", "?") + " (save_effect: unsupported usage or condition duration)"); continue; }
            for (const auto& prev : synth)
                if (prev.id == d.id) return a.id + ": save_effect ids '" + prev.content_id + "' and '" + d.content_id + "' collide";
            c.spells_known.push_back(d.id);
            a.save_actions.push_back(sa);
            synth.push_back(std::move(d));
        }
    // batch 3 ghast Stench: aura traits (built by the "aura" hook) -> synthetic Zone SpellDefs, attached at Combat::create.
    for (auto& d : a.aura_defs) {
        for (const auto& prev : synth)
            if (prev.id == d.id) return a.id + ": aura ids '" + prev.content_id + "' and '" + d.content_id + "' collide";
        a.aura_spell_ids.push_back(d.id);
        synth.push_back(d);
    }
    a.aura_defs.clear();
    // Register in the shared SpellBook, replacing an overridden archetype's entries; all-or-nothing.
    if (!synth.empty() || by_id_.count(a.id)) {
        std::vector<rules5e::SpellDef> old;
        if (auto prev = by_id_.find(a.id); prev != by_id_.end()) {
            for (const auto& sa : prev->second.save_actions)
                if (const auto* d = spells_->find(sa.spell_id)) old.push_back(*d);
            for (uint32_t id : prev->second.trigger_spell_ids)
                if (const auto* d = spells_->find(id)) old.push_back(*d);
            for (uint32_t id : prev->second.aura_spell_ids)
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
    // Priced materials (CONTENT_CONVENTIONS §7): equipment[] entries with their value_gp (integer >= 0, default 0).
    if (auto eq = doc.find("equipment"); eq != doc.end() && eq->is_array())
        for (const auto& e : *eq) {
            EquipmentEntry ent;
            ent.id = equipment_entry_id(e);
            if (e.is_object())
                if (auto v = e.find("value_gp"); v != e.end()) {
                    if (!v->is_number_integer() || v->get<int64_t>() < 0 || v->get<int64_t>() > 0xFFFFFFFFll)
                        return a.id + ": equipment value_gp must be a non-negative integer";
                    ent.value_gp = uint32_t(v->get<int64_t>());
                }
            if (e.is_object())
                if (auto q = e.find("equipped"); q != e.end() && q->is_boolean()) ent.equipped = q->get<bool>();
            if (e.is_object())
                if (auto n = e.find("count"); n != e.end() && n->is_number_integer())
                    ent.count = uint16_t(std::clamp<int64_t>(n->get<int64_t>(), 0, 65535));
            a.equipment.push_back(std::move(ent));
        }
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
    // item@1 (EQUIPMENT.md 9): the def table; no archetype reads it (only Combat with CombatOptions::equipment on).
    for (const assets::PackDocument& d : packs.documents()) {
        if (!d.doc.ok || d.doc.current.type != "item") continue;
        const std::string& pack_id = packs.packs()[d.pack].id;
        try {
            if (auto e = add_item(d.doc.data)) errors.push_back(pack_id + ":" + d.path + ": " + *e);
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

std::optional<std::string> ContentLibrary::add_summon_spell(const rules5e::SpellDef& def, SummonOptions options) {
    if (def.kind != rules5e::SpellEffectKind::Summon) return def.content_id + ": not a Summon spell";
    if (options.archetypes.empty() || options.archetypes.size() > 255) return def.content_id + ": summon needs 1..255 archetypes";
    for (const auto& id : options.archetypes)
        if (!find(id)) return def.content_id + ": unknown summon archetype '" + id + "'";
    if (spells_->find(def.id) || summons_.count(def.id)) return def.content_id + ": duplicate spell id";
    if (options.max_count && *options.max_count == 0) return def.content_id + ": summon max_count must be 1..255";
    rules5e::SpellDef d = def;
    d.summon.max_count = options.max_count ? *options.max_count : uint8_t(255);   // absent = no cap
    if (options.max_count && options.table == SummonOptions::Table::CrBand)
        for (unsigned lvl = std::max<unsigned>(def.level, 1); lvl <= 9; ++lvl) {
            const unsigned mult = lvl >= 9 ? 4 : lvl >= 7 ? 3 : lvl >= 5 ? 2 : 1;
            for (unsigned n : {1u, 2u, 4u, 8u})
                if (n * mult > *options.max_count)
                    warnings_.push_back(def.content_id + ": summon max_count " + std::to_string(*options.max_count) +
                                        " clips the CR band count " + std::to_string(n * mult) + " at slot level " +
                                        std::to_string(lvl));
        }
    spells_->add(d);
    summons_[def.id] = std::move(options);
    return std::nullopt;
}

const SummonOptions* ContentLibrary::summon_options(uint32_t spell_id) const {
    auto it = summons_.find(spell_id);
    return it == summons_.end() ? nullptr : &it->second;
}

const Archetype* ContentLibrary::find(const std::string& id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &it->second;
}

} // namespace eng::sim
