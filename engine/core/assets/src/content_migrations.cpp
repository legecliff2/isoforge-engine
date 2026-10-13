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
void character_2_to_3(Json& j, MigrationCtx&) { mark_effects_inert(j); }
// monster@3: same gate as character@3 (docs/CONTENT_COMPAT.md).
void monster_2_to_3(Json& j, MigrationCtx&) { mark_effects_inert(j); }
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

} // namespace eng::assets
