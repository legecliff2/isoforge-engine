#include "assets/content_migrations.hpp"

#include <stdexcept>

namespace eng::assets {

namespace {
void monster_1_to_2(Json& j, MigrationCtx&) {
    auto pres = j.find("presentation");
    if (pres == j.end() || !pres->is_object()) return;
    auto ai = pres->find("ai");
    if (ai == pres->end()) return;
    if (j.contains("behavior")) throw std::runtime_error("monster@1 has both presentation.ai and behavior");
    j["behavior"] = Json{{"ai", *ai}};
    pres->erase(ai);
    if (pres->empty()) j.erase("presentation");
}
// character@2 = character@1 + optional behavior.ai (append-only): nothing to rewrite.
void character_1_to_2(Json&, MigrationCtx&) {}
// character@3 / monster@3 activate feature effects. Pre-@3 effects were descriptive (numbers baked into
// the stat block), so they stay inert: the engine records them but applies nothing.
void mark_effects_inert(Json& j) {
    for (const char* key : {"features", "traits"}) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array()) continue;
        for (auto& f : *it) {
            if (!f.is_object()) continue;
            auto e = f.find("effect");
            if (e != f.end() && e->is_object()) (*e)["inert"] = true;
        }
    }
}
// character@3 / monster@3 also make defenses and multi-component attack damage mechanical. Pre-@3 files
// listed damage_resistances / damage_immunities / damage_vulnerabilities / condition_immunities and
// extra damage[] entries as descriptive text, and the engine read none of them (only damage[0]).
// The migration records which of those a pre-@3 file carries in "inert_mechanics" so the consumer
// keeps ignoring them. Files without any of them are left byte-identical.
void mark_defenses_inert(Json& j) {
    Json marks = Json::array();
    for (const char* key : {"damage_resistances", "damage_immunities", "damage_vulnerabilities", "condition_immunities"}) {
        auto it = j.find(key);
        if (it != j.end() && it->is_array() && !it->empty()) marks.push_back(key);
    }
    bool multi = false;
    for (const char* key : {"actions", "bonus_actions", "reactions", "legendary_actions"}) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array()) continue;
        for (const auto& a : *it) {
            auto d = a.is_object() ? a.find("damage") : a.end();
            if (a.is_object() && d != a.end() && d->is_array() && d->size() > 1) multi = true;
        }
    }
    if (multi) marks.push_back("damage[1..]");
    // Weapon qualifiers on actions (magical / silvered / adamantine) are mechanical at @3 only.
    bool quals = false;
    for (const char* key : {"actions", "bonus_actions", "reactions", "legendary_actions"}) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_array()) continue;
        for (const auto& a : *it) {
            auto q = a.is_object() ? a.find("qualifiers") : a.end();
            if (a.is_object() && q != a.end() && q->is_array() && !q->empty()) quals = true;
        }
    }
    if (quals) marks.push_back("attack_qualifiers");
    // Spellcasting and creature type also become mechanical at @3. Pre-@3 `type` was descriptive and the
    // sim treated everything as Humanoid, so only a non-humanoid type changes behaviour when read.
    if (j.contains("spellcasting")) marks.push_back("spellcasting");
    if (auto t = j.find("type"); t != j.end() && t->is_string() && t->get<std::string>() != "humanoid")
        marks.push_back("creature_type");
    // Size becomes a footprint at @3 (Large 2x2, Huge 3x3, Gargantuan 4x4). Pre-@3 it was descriptive and every
    // creature took one tile, so a pre-@3 Large+ file keeps a 1x1 footprint.
    if (auto sz = j.find("size"); sz != j.end() && sz->is_string()) {
        const std::string& v = sz->get<std::string>();
        if (v == "large" || v == "huge" || v == "gargantuan") marks.push_back("size");
    }
    if (!marks.empty()) j["inert_mechanics"] = std::move(marks);
}
void character_2_to_3(Json& j, MigrationCtx&) { mark_effects_inert(j); mark_defenses_inert(j); }
// monster@3: same gate as character@3 (docs/CONTENT_COMPAT.md).
void monster_2_to_3(Json& j, MigrationCtx&) { mark_effects_inert(j); mark_defenses_inert(j); }
} // namespace

void register_content_schemas(SchemaRegistry& reg) {
    reg.register_type("manifest", 1);
    reg.register_type("character", kCharacterSchemaVersion);
    reg.register_type("chunk", 1);
    reg.register_type("monster", kMonsterSchemaVersion);
    reg.register_type("spell", kSpellSchemaVersion);
    reg.register_migration("monster", 1, 2, monster_1_to_2);
    reg.register_migration("character", 1, 2, character_1_to_2);
    reg.register_migration("character", 2, 3, character_2_to_3);
    reg.register_migration("monster", 2, 3, monster_2_to_3);
}

std::optional<std::string> behavior_ai_script(const Json& doc) {
    auto b = doc.find("behavior");
    if (b == doc.end() || !b->is_object()) return std::nullopt;
    auto ai = b->find("ai");
    if (ai == b->end() || !ai->is_string()) return std::nullopt;
    return ai->get<std::string>();
}

std::optional<std::string> monster_ai_script(const Json& monster) { return behavior_ai_script(monster); }

std::vector<std::string> effect_kinds_introduced_at(uint32_t) { return {}; }

} // namespace eng::assets
