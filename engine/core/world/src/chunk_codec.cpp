#include "world/chunk_codec.hpp"

#include <limits>

#include "base/canonical_hash.hpp"

namespace eng::world {

using assets::Json;

uint32_t spawn_stable_id(ChunkCoord coord, uint32_t spawn_index) {
    CanonicalHasher h;
    h.i32(coord.x);
    h.i32(coord.y);
    h.i16(coord.level);
    h.u32(spawn_index);
    const uint64_t v = h.value();
    return uint32_t(v) ^ uint32_t(v >> 32);
}

uint32_t chunk_light_stable_id(ChunkCoord coord, uint32_t light_index) {
    CanonicalHasher h;
    h.i32(coord.x);
    h.i32(coord.y);
    h.i16(coord.level);
    h.u32(light_index);
    h.byte(uint8_t('L'));
    const uint64_t v = h.value();
    return uint32_t(v) ^ uint32_t(v >> 32);
}

uint64_t compute_content_hash(const Chunk& c) {
    CanonicalHasher h;
    const bool v3 = c.elevation_is_ground;   // chunk@3: written as 3 and always carries the chunk@2 tail
    const bool v2 = v3 || !c.lights.empty() || c.ambient.has_value();
    // chunk@1 content (no lights, no ambient) keeps its chunk@1 hash; chunk@2 keeps its chunk@2 hash.
    h.u16(v3 ? uint16_t(3) : v2 ? uint16_t(2) : uint16_t(1));
    h.i32(c.coord.x);
    h.i32(c.coord.y);
    h.i16(c.coord.level);
    h.u32(uint32_t(c.palette.size()));
    for (const auto& p : c.palette) h.str(p);
    for (const auto& layer : c.layers)
        for (const TileCell& cell : layer) {
            h.u16(cell.tile);
            h.byte(cell.elevation);
            h.byte(cell.flags);
        }
    h.u32(uint32_t(c.spawns.size()));
    for (const auto& s : c.spawns) {
        h.str(s.archetype);
        h.i32(s.tile.x);
        h.i32(s.tile.y);
        h.u32(s.stable_id);
        h.str(s.overrides_json);
    }
    h.u32(uint32_t(c.triggers.size()));
    for (const auto& t : c.triggers) h.str(t);
    if (v2) {   // chunk@2 fields, only when present (always for chunk@3)
        h.u32(uint32_t(c.lights.size()));
        for (const auto& l : c.lights) {
            h.i32(l.tile.x);
            h.i32(l.tile.y);
            h.u16(l.bright_ft);
            h.u16(l.dim_ft);
            h.byte(l.flags);
            h.str(l.preset);
            h.u32(l.stable_id);
        }
        h.byte(c.ambient ? 1 : 0);
        if (c.ambient) { h.byte(c.ambient->kind); h.byte(c.ambient->level); }
    }
    return h.value();
}

std::string_view to_string(Chunk::Origin o) {
    switch (o) {
        case Chunk::Origin::Handcrafted: return "handcrafted";
        case Chunk::Origin::Streamed: return "streamed";
        case Chunk::Origin::Procedural: return "procedural";
    }
    return "?";
}

std::optional<Chunk::Origin> parse_origin(std::string_view s) {
    if (s == "handcrafted") return Chunk::Origin::Handcrafted;
    if (s == "streamed") return Chunk::Origin::Streamed;
    if (s == "procedural") return Chunk::Origin::Procedural;
    return std::nullopt;
}

std::string hash_to_hex(uint64_t h) {
    static const char* digits = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, h >>= 4) s[std::size_t(i)] = digits[h & 0xF];
    return s;
}

void register_chunk_schema(assets::SchemaRegistry& reg) {
    reg.register_type("chunk", kChunkFormatVersion);
    // chunk@1 -> chunk@2: identity (no lights, no ambient); an explicit format_version follows the document.
    reg.register_migration("chunk", 1, 2, [](Json& j, assets::MigrationCtx&) {
        if (auto fv = j.find("format_version"); fv != j.end()) *fv = 2;
    });
    // chunk@2 -> chunk@3: the document keeps its declared version in format_version (2 unless already set), so a migrated
    // chunk@1 / chunk@2 stays flat for the sim (elevation render-only) and keeps its hash. Only an authored chunk@3 reads
    // elevation as ground.
    reg.register_migration("chunk", 2, 3, [](Json& j, assets::MigrationCtx&) {
        if (!j.contains("format_version")) j["format_version"] = 2;
    });
}

namespace {

struct DecodeError { std::string msg; };

template <class T> T int_field(const Json& j, const char* key, int64_t lo, int64_t hi) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer())
        throw DecodeError{std::string("'") + key + "' must be an integer"};
    const int64_t v = it->get<int64_t>();
    if (v < lo || v > hi) throw DecodeError{std::string("'") + key + "' out of range"};
    return T(v);
}

const Json& field(const Json& j, const char* key, bool (Json::*pred)() const noexcept, const char* what) {
    auto it = j.find(key);
    if (it == j.end() || !((*it).*pred)()) throw DecodeError{std::string("'") + key + "' must be " + what};
    return *it;
}

template <class F> void dense_array(const Json& layer, const char* key, int l, int64_t max, F&& set) {
    auto it = layer.find(key);
    if (it == layer.end()) return;   // omitted = zeros
    const std::string where = "layers[" + std::to_string(l) + "]." + key;
    if (!it->is_array() || it->size() != std::size_t(kChunkSize * kChunkSize))
        throw DecodeError{where + " must be an array of 1024 integers"};
    for (std::size_t i = 0; i < it->size(); ++i) {
        const Json& v = (*it)[i];
        if (!v.is_number_integer()) throw DecodeError{where + "[" + std::to_string(i) + "] not an integer"};
        const int64_t n = v.get<int64_t>();
        if (n < 0 || n > max) throw DecodeError{where + "[" + std::to_string(i) + "] out of range"};
        set(i, n);
    }
}

Chunk decode(const Json& doc) {
    Chunk c{};   // value-init: all cells zero
    // 1 (chunk@1, e.g. procgen output decoded directly), 2 or 3; chunk@1 has no lights / ambient. Without format_version
    // "$schema" chunk@3 means 3 and chunk@1 / chunk@2 mean 2 (as before), else the current version. Only 3 makes elevation
    // the sim's ground.
    int64_t declared = kChunkFormatVersion;
    if (auto sc = doc.find("$schema"); sc != doc.end() && sc->is_string()) {
        const std::string& sv = sc->get_ref<const std::string&>();
        if (sv == "chunk@1" || sv == "chunk@2") declared = 2;   // as before chunk@3: lights / ambient allowed
        else if (sv == "chunk@3") declared = 3;
    }
    if (auto fv = doc.find("format_version"); fv != doc.end()) {
        if (!fv->is_number_integer() || fv->get<int64_t>() < 1 || fv->get<int64_t>() > kChunkFormatVersion)
            throw DecodeError{"format_version must be 1, 2 or " + std::to_string(kChunkFormatVersion)};
        declared = fv->get<int64_t>();
    }
    c.format_version = kChunkFormatVersion;
    c.elevation_is_ground = declared >= 3;

    const Json& coord = field(doc, "coord", &Json::is_object, "an object");
    c.coord.x = int_field<int32_t>(coord, "x", std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
    c.coord.y = int_field<int32_t>(coord, "y", std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
    c.coord.level = int_field<int16_t>(coord, "level", std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());

    auto origin = parse_origin(field(doc, "origin", &Json::is_string, "a string").get<std::string>());
    if (!origin) throw DecodeError{"origin must be handcrafted|streamed|procedural"};
    c.origin = *origin;

    const Json& palette = field(doc, "palette", &Json::is_array, "an array");
    if (palette.empty() || palette.size() > 65536) throw DecodeError{"palette must have 1..65536 entries"};
    for (const Json& p : palette) {
        if (!p.is_string()) throw DecodeError{"palette entries must be strings"};
        c.palette.push_back(p.get<std::string>());
    }
    if (!c.palette[0].empty()) throw DecodeError{"palette[0] is reserved for the empty tile and must be \"\""};
    for (std::size_t i = 1; i < c.palette.size(); ++i)
        if (c.palette[i].empty()) throw DecodeError{"palette[" + std::to_string(i) + "] must not be empty"};

    const Json& layers = field(doc, "layers", &Json::is_array, "an array");
    if (layers.size() != std::size_t(kChunkLayers)) throw DecodeError{"layers must have exactly 4 entries"};
    for (int l = 0; l < kChunkLayers; ++l) {
        const Json& layer = layers[std::size_t(l)];
        if (!layer.is_object()) throw DecodeError{"layers[" + std::to_string(l) + "] must be an object"};
        for (auto it = layer.begin(); it != layer.end(); ++it)
            if (it.key() != "tiles" && it.key() != "elevation" && it.key() != "flags")
                throw DecodeError{"layers[" + std::to_string(l) + "]: unknown field '" + it.key() + "'"};
        auto& cells = c.layers[std::size_t(l)];
        dense_array(layer, "tiles", l, int64_t(c.palette.size()) - 1, [&](std::size_t i, int64_t v) { cells[i].tile = TileId(v); });
        dense_array(layer, "elevation", l, 255, [&](std::size_t i, int64_t v) { cells[i].elevation = uint8_t(v); });
        dense_array(layer, "flags", l, tile_flags::kKnownMask, [&](std::size_t i, int64_t v) { cells[i].flags = uint8_t(v); });
    }

    const Json& spawns = field(doc, "spawns", &Json::is_array, "an array");
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        const Json& s = spawns[i];
        const std::string where = "spawns[" + std::to_string(i) + "]";
        if (!s.is_object()) throw DecodeError{where + " must be an object"};
        if (s.contains("stable_id")) throw DecodeError{where + ": stable_id is computed by the loader and must not be authored"};
        EntitySpawn sp;
        sp.archetype = field(s, "archetype", &Json::is_string, "a string").get<std::string>();
        if (sp.archetype.empty()) throw DecodeError{where + ".archetype must not be empty"};
        const Json& tile = field(s, "tile", &Json::is_object, "an object");
        sp.tile.x = int_field<int32_t>(tile, "x", 0, kChunkSize - 1);
        sp.tile.y = int_field<int32_t>(tile, "y", 0, kChunkSize - 1);
        sp.stable_id = spawn_stable_id(c.coord, uint32_t(i));
        if (auto ov = s.find("overrides"); ov != s.end()) {
            if (!ov->is_object()) throw DecodeError{where + ".overrides must be an object"};
            if (!ov->empty()) sp.overrides_json = ov->dump();
        }
        c.spawns.push_back(std::move(sp));
    }

    const Json& triggers = field(doc, "triggers", &Json::is_array, "an array");
    for (std::size_t i = 0; i < triggers.size(); ++i) {
        const Json& t = triggers[i];
        const std::string where = "triggers[" + std::to_string(i) + "]";
        if (!t.is_object()) throw DecodeError{where + " must be an object"};
        field(t, "id", &Json::is_string, "a string");
        field(t, "kind", &Json::is_string, "a string");
        c.triggers.push_back(t.dump());
    }

    if (declared == 1 && (doc.contains("lights") || doc.contains("ambient")))
        throw DecodeError{"lights / ambient need format_version 2 (chunk@2)"};
    if (auto ls = doc.find("lights"); ls != doc.end()) {
        if (!ls->is_array()) throw DecodeError{"'lights' must be an array"};
        for (std::size_t i = 0; i < ls->size(); ++i) {
            const Json& l = (*ls)[i];
            const std::string where = "lights[" + std::to_string(i) + "]";
            if (!l.is_object()) throw DecodeError{where + " must be an object"};
            for (auto it = l.begin(); it != l.end(); ++it)
                if (it.key() != "tile" && it.key() != "bright_ft" && it.key() != "dim_ft" && it.key() != "magical" &&
                    it.key() != "sunlight" && it.key() != "starts_unlit" && it.key() != "preset" && it.key() != "visual")
                    throw DecodeError{where + ": unknown field '" + it.key() + "'"};
            if (l.contains("stable_id")) throw DecodeError{where + ": stable_id is computed by the loader"};
            Chunk::Light out;
            const Json& tile = field(l, "tile", &Json::is_object, "an object");
            out.tile.x = int_field<int32_t>(tile, "x", 0, kChunkSize - 1);
            out.tile.y = int_field<int32_t>(tile, "y", 0, kChunkSize - 1);
            out.bright_ft = int_field<uint16_t>(l, "bright_ft", 0, kMaxLightFt);
            out.dim_ft = l.contains("dim_ft") ? int_field<uint16_t>(l, "dim_ft", 0, kMaxLightFt) : uint16_t(0);
            if (out.bright_ft % 5 || out.dim_ft % 5) throw DecodeError{where + ": radii must be multiples of 5 ft"};
            if (int(out.bright_ft) + int(out.dim_ft) > kMaxLightFt)
                throw DecodeError{where + ": bright_ft + dim_ft exceeds " + std::to_string(kMaxLightFt) + " ft"};
            auto flag = [&](const char* key, uint8_t bit) {
                auto it = l.find(key);
                if (it == l.end()) return;
                if (!it->is_boolean()) throw DecodeError{where + "." + key + " must be a boolean"};
                if (it->get<bool>()) out.flags = uint8_t(out.flags | bit);
            };
            flag("magical", 1);
            flag("sunlight", 2);
            flag("starts_unlit", 4);
            if (auto pr = l.find("preset"); pr != l.end()) {
                if (!pr->is_string()) throw DecodeError{where + ".preset must be a string"};
                out.preset = pr->get<std::string>();
            }
            // "visual" (colour, flicker, glow sprite): presentation, accepted and dropped; never hashed or stored.
            if (auto vi = l.find("visual"); vi != l.end() && !vi->is_object()) throw DecodeError{where + ".visual must be an object"};
            out.stable_id = chunk_light_stable_id(c.coord, uint32_t(i));
            c.lights.push_back(std::move(out));
        }
    }
    if (auto am = doc.find("ambient"); am != doc.end()) {
        if (!am->is_object()) throw DecodeError{"'ambient' must be an object"};
        Chunk::Ambient a;
        const std::string kind = field(*am, "kind", &Json::is_string, "a string").get<std::string>();
        const std::string level = field(*am, "level", &Json::is_string, "a string").get<std::string>();
        if (kind == "none") a.kind = 0; else if (kind == "sun") a.kind = 1; else if (kind == "moon") a.kind = 2;
        else throw DecodeError{"ambient.kind must be none|sun|moon"};
        if (level == "dark") a.level = 0; else if (level == "dim") a.level = 1; else if (level == "bright") a.level = 2;
        else throw DecodeError{"ambient.level must be dark|dim|bright"};
        for (auto it = am->begin(); it != am->end(); ++it)
            if (it.key() != "kind" && it.key() != "level") throw DecodeError{"ambient: unknown field '" + it.key() + "'"};
        c.ambient = a;
    }

    c.content_hash = compute_content_hash(c);
    if (auto ch = doc.find("content_hash"); ch != doc.end()) {
        if (!ch->is_string()) throw DecodeError{"content_hash must be a 16-digit hex string"};
        if (ch->get<std::string>() != hash_to_hex(c.content_hash))
            throw DecodeError{"content_hash mismatch: file says " + ch->get<std::string>() + ", computed " + hash_to_hex(c.content_hash)};
    }
    return c;
}

} // namespace

ChunkResult chunk_from_json(const Json& doc) {
    try {
        return ChunkResult{ChunkStatus::Ready, decode(doc), {}};
    } catch (const DecodeError& e) {
        return ChunkResult{ChunkStatus::Error, std::nullopt, "chunk: " + e.msg};
    }
}

} // namespace eng::world
