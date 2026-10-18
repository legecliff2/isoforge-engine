#include "sim/content.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <set>

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
            // Durations are not enforced by rules until timed modifiers (row 12): a condition lasts until a
            // successful repeat save (until_save / repeat_save) or the end of the combat. Compile only those
            // that end by a save; anything else is listed, not compiled.
            const std::string rs = x.value("repeat_save", "");
            const Json dur = x.contains("duration") ? x["duration"] : Json::object();
            const std::string dk = dur.is_object() ? dur.value("kind", "") : "";
            if (rs.empty() && dk != "until_save") { unsupported = true; return std::nullopt; }
            if (first) {   // the first condition rides the primary Save effect
                d.applies_condition = true;
                d.condition = *cond;
                d.repeat_save = repeat_of(rs.empty() ? "end_of_turn" : rs);
                first = false;
            } else {       // further conditions: ApplyCondition on the creatures that failed the save
                rules5e::SpellEffect e;
                e.kind = rules5e::SpellEffectKind::ApplyCondition;
                e.applies_condition = true;
                e.condition = *cond;
                e.repeat_save = repeat_of(rs.empty() ? "end_of_turn" : rs);
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
    // 0 = parse the document's own $schema tag (never "latest"); a missing or unparsable tag means no refusal.
    uint32_t declared = declared_version;
    if (declared == 0)
        if (auto s = doc.find("$schema"); s != doc.end() && s->is_string())
            if (auto ref = assets::parse_schema_ref(s->get<std::string>())) declared = ref->version;
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
                rules5e::Ability ab = rules5e::Ability::Cha;
                if (auto it = in->find("ability"); it != in->end()) {
                    const auto o = it->is_string() ? ability_of(it->get<std::string>()) : std::nullopt;
                    if (!o) return a.id + ": bad innate ability";
                    has_ab = true;
                    ab = *o;
                }
                auto add_spell = [&](const Json& id, uint8_t uses_max, uint8_t uses_left, uint8_t recharge) -> std::optional<std::string> {
                    if (!id.is_string()) return a.id + ": bad innate spell id";
                    rules5e::InnateSpell is;
                    is.spell_id = rules5e::spell_id_of(id.get<std::string>());
                    is.uses_max = uses_max;
                    is.uses_left = uses_left;
                    is.recharge_min = recharge;
                    is.has_ability = has_ab;
                    is.ability = ab;
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
    // Feature hooks (data-driven by effect.kind; see features.hpp).
    a.features = declared_features(doc);
    // 0 = parse the document's own $schema tag (never "latest"); a missing or unparsable tag means no refusal.
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
        if (!h) {
            a.unsupported_features.push_back(f.id + " (" + (f.kind.empty() ? "no effect" : f.kind) + ")");
            continue;
        }
        if (auto e = h->apply(a, f, uint32_t(i + 1), fctx)) return a.id + ": " + *e;
    }
    // multiattack options -> rules5e::AttackSequence (ids owned by the library, stable addresses).
    for (const auto& m : a.multiattack_decls) {
        strings_.push_back(m.id);
        c.multiattacks.push_back({strings_.back(), m.attacks});
    }
    a.ai = assets::behavior_ai_script(doc);   // monster@2 / character@2
    // monster@4 / character@4 save_effect actions -> synthetic SpellDefs (never for a file that declared < 4).
    std::vector<rules5e::SpellDef> synth;
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
        if (auto prev = by_id_.find(a.id); prev != by_id_.end())
            for (const auto& sa : prev->second.save_actions)
                if (const auto* d = spells_->find(sa.spell_id)) old.push_back(*d);
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
