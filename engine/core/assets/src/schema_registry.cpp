#include "assets/schema_registry.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace eng::assets {

std::optional<SchemaRef> parse_schema_ref(std::string_view s) {
    const auto at = s.find('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= s.size()) return std::nullopt;
    const std::string_view type = s.substr(0, at), ver = s.substr(at + 1);
    for (char c : type)
        if (!((c >= 'a' && c <= 'z') || c == '_')) return std::nullopt;
    if (ver[0] == '0') return std::nullopt;
    uint32_t v = 0;
    auto [p, ec] = std::from_chars(ver.data(), ver.data() + ver.size(), v);
    if (ec != std::errc{} || p != ver.data() + ver.size()) return std::nullopt;
    return SchemaRef{std::string(type), v};
}

std::string to_string(const SchemaRef& r) { return r.type + "@" + std::to_string(r.version); }

bool contains_float(const Json& j) {
    if (j.is_number_float()) return true;
    if (j.is_array() || j.is_object())
        for (const auto& v : j) if (contains_float(v)) return true;
    return false;
}

void SchemaRegistry::register_type(std::string type, uint32_t current_version) {
    if (current_version == 0) throw std::logic_error("assets: schema version must be >= 1");
    auto& t = types_[type];
    if (t.current != 0 && t.current != current_version)
        throw std::logic_error("assets: type '" + type + "' re-declared with a different version");
    t.current = current_version;
}

void SchemaRegistry::register_migration(std::string type, uint32_t from, uint32_t to, MigrationFn fn) {
    if (from == 0 || to != from + 1)
        throw std::logic_error("assets: migration must be a single step N -> N+1");
    auto& t = types_[type];
    if (!t.migrations.emplace(from, std::move(fn)).second)
        throw std::logic_error("assets: duplicate migration " + type + " " + std::to_string(from));
}

void SchemaRegistry::register_validator(std::string type, uint32_t version, ValidatorFn fn) {
    types_[type].validators[version] = std::move(fn);
}

std::optional<uint32_t> SchemaRegistry::current_version(std::string_view type) const {
    auto it = types_.find(type);
    if (it == types_.end() || it->second.current == 0) return std::nullopt;
    return it->second.current;
}

std::vector<std::string> SchemaRegistry::check_chains() const {
    std::vector<std::string> errs;
    for (const auto& [name, t] : types_) {
        if (t.current == 0) { errs.push_back(name + ": migrations registered but type not declared"); continue; }
        for (uint32_t v = 1; v < t.current; ++v)
            if (!t.migrations.count(v)) errs.push_back(name + ": missing migration " + std::to_string(v) + " -> " + std::to_string(v + 1));
        for (const auto& [from, fn] : t.migrations)
            if (from >= t.current) errs.push_back(name + ": migration from " + std::to_string(from) + " is at/after current");
    }
    return errs;
}

LoadResult SchemaRegistry::load(Json doc) const {
    LoadResult r;
    auto fail = [&](std::string e) { r.ok = false; r.error = std::move(e); return r; };
    if (!doc.is_object()) return fail("document is not a JSON object");
    auto sit = doc.find("$schema");
    if (sit == doc.end() || !sit->is_string()) return fail("missing \"$schema\": \"<type>@<version>\"");
    auto ref = parse_schema_ref(sit->get<std::string>());
    if (!ref) return fail("malformed $schema '" + sit->get<std::string>() + "'");
    r.declared = *ref;
    auto tit = types_.find(ref->type);
    if (tit == types_.end() || tit->second.current == 0) return fail("unknown schema type '" + ref->type + "'");
    const TypeInfo& t = tit->second;
    r.current = SchemaRef{ref->type, t.current};
    if (ref->version > t.current)
        return fail(to_string(*ref) + " is newer than this engine supports (" + to_string(r.current) + ")");
    if (contains_float(doc)) return fail("floating-point numbers are not allowed (use integers or decimal strings)");

    auto validate = [&](uint32_t v) -> std::optional<std::string> {
        auto vit = t.validators.find(v);
        if (vit == t.validators.end()) return std::nullopt;
        return vit->second(doc);
    };
    if (auto e = validate(ref->version)) return fail("invalid " + to_string(*ref) + ": " + *e);

    for (uint32_t v = ref->version; v < t.current; ++v) {
        auto mit = t.migrations.find(v);
        if (mit == t.migrations.end())
            return fail("no migration " + ref->type + " " + std::to_string(v) + " -> " + std::to_string(v + 1));
        MigrationCtx ctx{ref->type, v, v + 1, {}};
        try {
            mit->second(doc, ctx);
        } catch (const std::exception& ex) {
            return fail("migration " + ref->type + " " + std::to_string(v) + " -> " + std::to_string(v + 1) + " failed: " + ex.what());
        }
        if (!doc.is_object()) return fail("migration produced a non-object");
        doc["$schema"] = to_string(SchemaRef{ref->type, v + 1});
        for (auto& w : ctx.warnings) r.warnings.push_back(std::move(w));
    }
    if (ref->version != t.current) {
        if (contains_float(doc)) return fail("migration introduced a floating-point number");
        if (auto e = validate(t.current)) return fail("migrated document invalid against " + to_string(r.current) + ": " + *e);
    }
    r.data = std::move(doc);
    r.ok = true;
    return r;
}

LoadResult SchemaRegistry::load_text(std::string_view text) const {
    Json doc = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) { LoadResult r; r.error = "JSON parse error"; return r; }
    return load(std::move(doc));
}

LoadResult SchemaRegistry::load_file(const std::filesystem::path& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) { LoadResult r; r.error = "cannot open " + path.generic_string(); return r; }
    std::ostringstream ss;
    ss << in.rdbuf();
    LoadResult r = load_text(ss.str());
    if (!r.ok) r.error = path.generic_string() + ": " + r.error;
    return r;
}

SchemaRegistry& global_registry() {
    static SchemaRegistry reg;
    return reg;
}

} // namespace eng::assets
