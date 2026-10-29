#include "assets/clip_registry.hpp"

#include <fstream>

namespace eng::assets {

namespace {
std::string norm_key(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c == '-') c = '_';
    return out;
}
bool known_bin(std::string_view s) {
    return s == "tiny" || s == "small" || s == "medium" || s == "large";
}
} // namespace

bool ClipRegistry::has_clip(std::string_view id) const {
    for (const Clip& c : clips)
        if (c.id == id) return true;
    return false;
}

std::optional<std::string> ClipRegistry::resolve(std::string_view name) const {
    const std::string key = norm_key(name);
    if (has_clip(key)) return key;
    if (auto it = aliases.find(key); it != aliases.end()) return it->second;
    if (!fallback.empty()) return fallback;
    return std::nullopt;
}

std::optional<std::string> load_clip_file(const Json& doc, ClipFile& out) {
    auto fail = [](std::string m) { return std::optional<std::string>{std::move(m)}; };
    if (!doc.is_object()) return fail("clip registry must be an object");
    if (contains_float(doc)) return fail("clip registry: floats are not allowed");
    for (auto it = doc.begin(); it != doc.end(); ++it)
        if (it.key() != "kits" && it.key() != "description") return fail("clip registry: unknown key '" + it.key() + "'");
    auto kits = doc.find("kits");
    if (kits == doc.end() || !kits->is_array() || kits->empty()) return fail("clip registry needs kits");
    if (auto d = doc.find("description"); d != doc.end() && !d->is_string()) return fail("clip registry: description must be a string");

    ClipFile file;
    auto take_kit = [&](const Json& k, const std::string& id, const std::string& bin) -> std::optional<std::string> {
        ClipRegistry reg;
        reg.id = id;
        reg.kind = k.value("kind", "");
        reg.bin = bin;
        if (reg.kind != "paperdoll" && reg.kind != "standin") return fail("kit '" + id + "': kind must be paperdoll or standin");
        if (reg.kind == "paperdoll" && !bin.empty()) return fail("kit '" + id + "': paperdoll takes no bin");
        auto clips = k.find("clips");
        if (clips == k.end() || !clips->is_array() || clips->empty()) return fail("kit '" + id + "': needs clips");
        for (const Json& c : *clips) {
            if (!c.is_object()) return fail("kit '" + id + "': clip must be an object");
            for (auto it = c.begin(); it != c.end(); ++it)
                if (it.key() != "id" && it.key() != "frames" && it.key() != "fps" && it.key() != "mode")
                    return fail("kit '" + id + "': clip unknown key '" + it.key() + "'");
            Clip clip;
            auto idv = c.find("id");
            if (idv == c.end() || !idv->is_string() || idv->get<std::string>().empty())
                return fail("kit '" + id + "': clip needs an id");
            clip.id = idv->get<std::string>();
            if (clip.id.find('-') != std::string::npos) return fail("kit '" + id + "': clip id '" + clip.id + "' must not contain '-'");
            if (reg.has_clip(clip.id)) return fail("kit '" + id + "': duplicate clip '" + clip.id + "'");
            auto pos_int = [&](const char* key, int& dest) -> std::optional<std::string> {
                auto it = c.find(key);
                if (it == c.end()) return std::nullopt;
                if (!it->is_number_integer() || it->get<int>() < 1) return fail("kit '" + id + "': clip '" + clip.id + "' " + key + " must be an integer >= 1");
                dest = it->get<int>();
                return std::nullopt;
            };
            if (auto e = pos_int("frames", clip.frames)) return e;
            if (auto e = pos_int("fps", clip.fps)) return e;
            if (auto m = c.find("mode"); m != c.end()) {
                if (!m->is_string()) return fail("kit '" + id + "': clip '" + clip.id + "' mode must be a string");
                clip.mode = m->get<std::string>();
                if (clip.mode != "loop" && clip.mode != "once" && clip.mode != "hold_last")
                    return fail("kit '" + id + "': clip '" + clip.id + "' mode must be loop, once, or hold_last");
            }
            reg.clips.push_back(std::move(clip));
        }
        if (auto al = k.find("aliases"); al != k.end()) {
            if (!al->is_object()) return fail("kit '" + id + "': aliases must be an object");
            for (auto it = al->begin(); it != al->end(); ++it) {
                if (!it.value().is_string()) return fail("kit '" + id + "': alias '" + it.key() + "' must name a clip");
                const std::string key = norm_key(it.key());
                const std::string target = it.value().get<std::string>();
                if (key.empty()) return fail("kit '" + id + "': empty alias");
                if (reg.has_clip(key)) return fail("kit '" + id + "': alias '" + key + "' is already a clip");
                if (!reg.has_clip(target)) return fail("kit '" + id + "': alias '" + key + "' target '" + target + "' is not a clip");
                if (!reg.aliases.emplace(key, target).second) return fail("kit '" + id + "': duplicate alias '" + key + "'");
            }
        }
        if (auto fb = k.find("fallback"); fb != k.end()) {
            if (!fb->is_string()) return fail("kit '" + id + "': fallback must be a string");
            reg.fallback = fb->get<std::string>();
            if (!reg.has_clip(reg.fallback)) return fail("kit '" + id + "': fallback '" + reg.fallback + "' is not a clip");
        }
        file.kits.push_back(std::move(reg));
        return std::nullopt;
    };

    for (const Json& k : *kits) {
        if (!k.is_object()) return fail("clip registry: kit must be an object");
        for (auto it = k.begin(); it != k.end(); ++it)
            if (it.key() != "id" && it.key() != "kind" && it.key() != "bin" && it.key() != "bins" && it.key() != "clips" &&
                it.key() != "aliases" && it.key() != "fallback" && it.key() != "description")
                return fail("clip registry: kit unknown key '" + it.key() + "'");
        auto idv = k.find("id");
        if (idv == k.end() || !idv->is_string() || idv->get<std::string>().empty()) return fail("clip registry: kit needs an id");
        const std::string id = idv->get<std::string>();
        if (k.contains("bin") && k.contains("bins")) return fail("kit '" + id + "': bin and bins are exclusive");
        if (auto b = k.find("bins"); b != k.end()) {
            if (k.value("kind", "") != "standin") return fail("kit '" + id + "': bins are stand-in only");
            if (!b->is_array() || b->empty()) return fail("kit '" + id + "': bins must be a non-empty array");
            if (id.find('.') != std::string::npos) return fail("kit '" + id + "': a binned kit id must not contain '.'");
            for (const Json& one : *b) {
                if (!one.is_string() || !known_bin(one.get<std::string>()))
                    return fail("kit '" + id + "': bin must be tiny, small, medium, or large");
                const std::string bin = one.get<std::string>();
                for (const ClipRegistry& prev : file.kits)
                    if (prev.id == id + "." + bin) return fail("clip registry: duplicate kit '" + id + "." + bin + "'");
                if (auto e = take_kit(k, id + "." + bin, bin)) return e;
            }
        } else {
            std::string bin;
            if (auto b = k.find("bin"); b != k.end()) {
                if (!b->is_string() || !known_bin(b->get<std::string>()))
                    return fail("kit '" + id + "': bin must be tiny, small, medium, or large");
                bin = b->get<std::string>();
            }
            for (const ClipRegistry& prev : file.kits)
                if (prev.id == id) return fail("clip registry: duplicate kit '" + id + "'");
            if (auto e = take_kit(k, id, bin)) return e;
        }
    }
    out = std::move(file);
    return std::nullopt;
}

std::optional<std::string> load_clip_file(const std::filesystem::path& path, ClipFile& out) {
    std::ifstream in(path);
    if (!in) return std::optional<std::string>{"cannot read " + path.generic_string()};
    Json doc;
    try {
        doc = Json::parse(in);
    } catch (const std::exception& ex) {
        return std::optional<std::string>{std::string("invalid JSON: ") + ex.what()};
    }
    return load_clip_file(doc, out);
}

} // namespace eng::assets
