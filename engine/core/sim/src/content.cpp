#include "sim/content.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>

#include "assets/content_migrations.hpp"

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
int geti(const Json& j, const char* k, int def = 0) {
    auto it = j.find(k);
    return (it != j.end() && it->is_number_integer()) ? it->get<int>() : def;
}
} // namespace

std::optional<std::string> ContentLibrary::add(const Json& doc, const std::string& schema_type) {
    Archetype a;
    a.id = doc.value("id", "");
    if (a.id.empty()) return "stat block without id";
    a.schema_type = schema_type;
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
        int normal = 0, lng = 0;
        if (ranged) {
            const Json& rg = act.at("range");
            normal = rg.at(0).get<int>();
            lng = rg.size() > 1 ? rg.at(1).get<int>() : normal;
            if (normal <= 0 || lng < normal || lng > 65535) return a.id + ": bad range in action " + act.value("id", "?");
        }
        at.reach_or_range_ft = uint8_t(melee ? geti(act, "reach", 5) : std::min(normal, 255));
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
        const FeatureHook* h = f.kind.empty() ? nullptr : features_->hook(f.kind);
        if (!h) {
            a.unsupported_features.push_back(f.id + " (" + (f.kind.empty() ? "no effect" : f.kind) + ")");
            continue;
        }
        if (auto e = h->apply(a, f, uint32_t(i + 1), fctx)) return a.id + ": " + *e;
    }
    a.ai = assets::behavior_ai_script(doc);   // monster@2 / character@2
    by_id_[a.id] = std::move(a);
    return std::nullopt;
}

std::vector<std::string> ContentLibrary::load_pack(const std::string& pack_dir) {
    std::vector<std::string> errors;
    assets::SchemaRegistry reg;
    assets::register_content_schemas(reg);
    const std::filesystem::path dir(pack_dir);
    auto man = reg.load_file(dir / "manifest.json");
    if (!man.ok) return {man.error};
    for (const Json& item : man.data.value("contents", Json::array())) {
        const std::string rel = item.get<std::string>();
        if (rel.size() < 5 || rel.compare(rel.size() - 5, 5, ".json") != 0) continue;
        auto doc = reg.load_file(dir / rel);
        if (!doc.ok) { errors.push_back(doc.error); continue; }
        if (doc.current.type != "monster" && doc.current.type != "character") continue;
        try {
            if (auto e = add(doc.data, doc.current.type)) errors.push_back(rel + ": " + *e);
        } catch (const std::exception& ex) {
            errors.push_back(rel + ": " + ex.what());
        }
    }
    return errors;
}

const Archetype* ContentLibrary::find(const std::string& id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &it->second;
}

} // namespace eng::sim
