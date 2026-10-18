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
} // namespace

std::optional<std::string> ContentLibrary::add(const Json& doc, const std::string& schema_type, const std::string& pack_id) {
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
        for (const Json& sp : sc->at("spells")) {
            if (!sp.is_string()) return a.id + ": bad spell id";
            a.spell_ids.push_back(sp.get<std::string>());
            c.spells_known.push_back(rules5e::spell_id_of(a.spell_ids.back()));
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
    const FeatureBuildCtx fctx{*features_, doc, attack_actions};
    for (std::size_t i = 0; i < a.features.size(); ++i) {
        const FeatureDecl& f = a.features[i];
        // Schema-version gate (docs/CONTENT_COMPAT.md): effects from pre-character@3 files are
        // marked inert by the 2 -> 3 migration; their numbers are already baked in.
        if (!f.kind.empty() && f.effect.value("inert", false)) {
            a.inert_features.push_back(f.id + " (" + f.kind + ")");
            continue;
        }
        const FeatureHook* h = f.kind.empty() ? nullptr : features_->hook(f.kind);
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
            if (auto e = add(d.doc.data, d.doc.current.type, pack_id)) errors.push_back(pack_id + ":" + d.path + ": " + *e);
        } catch (const std::exception& ex) {
            errors.push_back(pack_id + ":" + d.path + ": " + ex.what());
        }
    }
    // Spellcasting references must resolve to a loaded spell@1 (same rule as the validator).
    for (const auto& [id, a] : by_id_)
        for (const std::string& sid : a.spell_ids) {
            const rules5e::SpellDef* d = spells_->find(rules5e::spell_id_of(sid));
            if (!d || d->content_id != sid) errors.push_back(a.pack_id + ":" + id + ": spellcasting.spells '" + sid + "' is not a loaded spell@1");
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
