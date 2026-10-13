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
} // namespace

void register_content_schemas(SchemaRegistry& reg) {
    reg.register_type("manifest", 1);
    reg.register_type("character", 1);
    reg.register_type("chunk", 1);
    reg.register_type("monster", kMonsterSchemaVersion);
    reg.register_migration("monster", 1, 2, monster_1_to_2);
}

std::optional<std::string> monster_ai_script(const Json& monster) {
    auto b = monster.find("behavior");
    if (b == monster.end() || !b->is_object()) return std::nullopt;
    auto ai = b->find("ai");
    if (ai == b->end() || !ai->is_string()) return std::nullopt;
    return ai->get<std::string>();
}

} // namespace eng::assets
