#include "procgen/dungeon.hpp"

#include <algorithm>

#include "base/canonical_hash.hpp"
#include "procgen/noise.hpp"
#include "rng/rng.hpp"

namespace procgen {

namespace {

constexpr uint64_t kLayoutStream = 1;   // Pcg32 stream id for room/corridor layout

struct Rect { int32_t x, y, w, h; };   // floor rect, half-open [x, x+w)

bool overlaps_with_gap(const Rect& a, const Rect& b) {
    // Keep >= 2 cells between floors so each room has its own wall ring.
    return a.x - 2 < b.x + b.w && b.x - 2 < a.x + a.w && a.y - 2 < b.y + b.h && b.y - 2 < a.y + a.h;
}

constexpr int idx(int x, int y) { return y * kChunkSize + x; }

void carve(std::array<bool, kCells>& floor, int x, int y) { floor[std::size_t(idx(x, y))] = true; }

// Axis-aligned segment (x0 == x1 or y0 == y1), inclusive.
void carve_line(std::array<bool, kCells>& floor, int x0, int y0, int x1, int y1) {
    for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y)
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) carve(floor, x, y);
}

std::optional<GenChunk> generate_v1(WorldSeed w, int32_t cx, int32_t cy, int16_t level) {
    GenChunk c;
    c.x = cx; c.y = cy; c.level = level;
    c.palette = {"", "tiles.dungeon.floor_01", "tiles.dungeon.wall_01", "tiles.dungeon.rubble_01",
                 "tiles.dungeon.water_01"};

    rng::Pcg32 r(chunk_seed(w, cx, cy, level), kLayoutStream);

    // 1. Rooms: floors inside [2, 29] so walls fit in [1, 30]; nothing touches the chunk edge.
    std::vector<Rect> rooms;
    const int want = r.range(3, 6);
    for (int attempt = 0; attempt < 64 && int(rooms.size()) < want; ++attempt) {
        Rect q;
        q.w = r.range(4, 9);
        q.h = r.range(4, 9);
        q.x = r.range(2, 30 - q.w);
        q.y = r.range(2, 30 - q.h);
        bool ok = true;
        for (const Rect& o : rooms) ok = ok && !overlaps_with_gap(q, o);
        if (ok) rooms.push_back(q);
    }
    // First attempt can never collide, so rooms is non-empty.

    std::array<bool, kCells> floor{};
    for (const Rect& q : rooms)
        for (int y = q.y; y < q.y + q.h; ++y)
            for (int x = q.x; x < q.x + q.w; ++x) carve(floor, x, y);

    // 2. Corridors: room i joins room i-1 with an L between centres. Every room is linked to
    //    the previous one, so all floor is one 4-connected component by construction.
    for (std::size_t i = 1; i < rooms.size(); ++i) {
        const Rect &a = rooms[i - 1], &b = rooms[i];
        const int ax = a.x + a.w / 2, ay = a.y + a.h / 2, bx = b.x + b.w / 2, by = b.y + b.h / 2;
        const bool horizontal_first = r.chance(1, 2);
        const int kx = horizontal_first ? bx : ax, ky = horizontal_first ? ay : by;   // elbow
        carve_line(floor, ax, ay, kx, ky);
        carve_line(floor, kx, ky, bx, by);
    }

    // 3. Ground + walls: any non-floor cell 8-adjacent to floor becomes a wall.
    auto& ground = c.layers[kGround];
    auto& detail = c.layers[kDetail];
    auto& walls = c.layers[kWalls];
    for (int y = 0; y < kChunkSize; ++y)
        for (int x = 0; x < kChunkSize; ++x) {
            const std::size_t i = std::size_t(idx(x, y));
            if (floor[i]) { ground[i].tile = tiles::kFloor; continue; }
            bool near = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < kChunkSize && ny < kChunkSize && floor[std::size_t(idx(nx, ny))])
                        near = true;
                }
            if (near) walls[i] = GenCell{tiles::kWall, 0, uint8_t(flags::kBlocksMove | flags::kBlocksSight)};
        }

    // 4. Detail from world-continuous noise (sampled at world tile coords, so features line up
    //    across chunk borders): rubble = difficult, water = water. Neither blocks movement.
    const uint64_t ns = noise_seed(w);
    const int64_t wx0 = int64_t(cx) * kChunkSize, wy0 = int64_t(cy) * kChunkSize;
    const uint64_t level_seed = ns ^ (uint64_t(uint16_t(level)) * 0x9E3779B97F4A7C15ull);
    for (int y = 0; y < kChunkSize; ++y)
        for (int x = 0; x < kChunkSize; ++x) {
            const std::size_t i = std::size_t(idx(x, y));
            if (!floor[i]) continue;
            const int32_t n = noise::fbm2(level_seed, wx0 + x, wy0 + y, 3, 3).raw;   // [0, 65536)
            if (n >= 44000) detail[i] = GenCell{tiles::kRubble, 0, flags::kDifficult};
            else if (n < 18000) detail[i] = GenCell{tiles::kWater, 0, flags::kWater};
        }

    // 5. One goblin in the centre of the last room.
    const Rect& last = rooms.back();
    c.spawns.push_back(GenSpawn{"srd51.monster.goblin", last.x + last.w / 2, last.y + last.h / 2, ""});
    return c;
}

} // namespace

namespace {
// D-007: append only. Never edit or remove an entry; new output = new version.
constexpr GeneratorVersion kGenerators[] = {
    {1, &generate_v1, "v0 rooms + L corridors, value-noise rubble/water, one goblin"},
};
constexpr bool registry_is_contiguous() {
    for (std::size_t i = 0; i < std::size(kGenerators); ++i)
        if (kGenerators[i].gen_version != i + 1 || kGenerators[i].fn == nullptr) return false;
    return true;
}
static_assert(registry_is_contiguous(), "generator registry must be contiguous from gen_version 1");
} // namespace

std::span<const GeneratorVersion> generator_versions() noexcept { return kGenerators; }
uint16_t latest_gen_version() noexcept { return kGenerators[std::size(kGenerators) - 1].gen_version; }

std::optional<GenChunk> generate(WorldSeed w, int32_t x, int32_t y, int16_t level) {
    if (w.gen_version == 0 || w.gen_version > std::size(kGenerators)) return std::nullopt;
    return kGenerators[w.gen_version - 1].fn(w, x, y, level);
}

uint32_t spawn_stable_id(int32_t x, int32_t y, int16_t level, uint32_t index) {
    eng::CanonicalHasher h;
    h.i32(x); h.i32(y); h.i16(level); h.u32(index);
    const uint64_t v = h.value();
    return uint32_t(v) ^ uint32_t(v >> 32);
}

uint64_t content_hash(const GenChunk& c) {
    eng::CanonicalHasher h;
    h.u16(kChunkFormatVersion);
    h.i32(c.x); h.i32(c.y); h.i16(c.level);
    h.u32(uint32_t(c.palette.size()));
    for (const auto& p : c.palette) h.str(p);
    for (const auto& layer : c.layers)
        for (const GenCell& cell : layer) { h.u16(cell.tile); h.byte(cell.elevation); h.byte(cell.flags); }
    h.u32(uint32_t(c.spawns.size()));
    for (std::size_t i = 0; i < c.spawns.size(); ++i) {
        const GenSpawn& s = c.spawns[i];
        h.str(s.archetype); h.i32(s.x); h.i32(s.y);
        h.u32(spawn_stable_id(c.x, c.y, c.level, uint32_t(i)));
        h.str(s.overrides_json);
    }
    h.u32(uint32_t(c.triggers.size()));
    for (const auto& t : c.triggers) h.str(t);
    return h.value();
}

std::string hash_hex(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[std::size_t(i)] = d[v & 0xF];
    return s;
}

namespace {
std::string json_str(const std::string& s) {   // palette/archetype ids are plain ASCII ids
    std::string o = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\') o.push_back('\\');
        o.push_back(ch);
    }
    return o + "\"";
}
} // namespace

std::string to_json(const GenChunk& c) {
    std::string o;
    o += "{\n  \"$schema\": \"chunk@1\",\n  \"format_version\": 1,\n";
    o += "  \"coord\": {\"x\": " + std::to_string(c.x) + ", \"y\": " + std::to_string(c.y) +
         ", \"level\": " + std::to_string(c.level) + "},\n";
    o += "  \"origin\": \"procedural\",\n";
    o += "  \"content_hash\": \"" + hash_hex(content_hash(c)) + "\",\n";
    o += "  \"palette\": [";
    for (std::size_t i = 0; i < c.palette.size(); ++i) o += (i ? ", " : "") + json_str(c.palette[i]);
    o += "],\n  \"layers\": [\n";
    for (int l = 0; l < kChunkLayers; ++l) {
        const auto& L = c.layers[std::size_t(l)];
        struct Arr { const char* name; int which; };
        std::vector<Arr> present;
        for (Arr a : {Arr{"tiles", 0}, Arr{"elevation", 1}, Arr{"flags", 2}}) {
            bool any = false;
            for (const GenCell& cell : L) any = any || (a.which == 0 ? cell.tile : a.which == 1 ? cell.elevation : cell.flags);
            if (any) present.push_back(a);
        }
        if (present.empty()) {
            o += "    {}";
        } else {
            o += "    {\n";
            for (std::size_t k = 0; k < present.size(); ++k) {
                o += "      \"" + std::string(present[k].name) + "\": [\n";
                for (int y = 0; y < kChunkSize; ++y) {
                    o += "        ";
                    for (int x = 0; x < kChunkSize; ++x) {
                        const GenCell& cell = L[std::size_t(idx(x, y))];
                        const int v = present[k].which == 0 ? cell.tile : present[k].which == 1 ? cell.elevation : cell.flags;
                        o += std::to_string(v);
                        if (x + 1 < kChunkSize || y + 1 < kChunkSize) o += ",";
                    }
                    o += "\n";
                }
                o += std::string("      ]") + (k + 1 < present.size() ? "," : "") + "\n";
            }
            o += "    }";
        }
        o += (l + 1 < kChunkLayers ? ",\n" : "\n");
    }
    o += "  ],\n  \"spawns\": [";
    for (std::size_t i = 0; i < c.spawns.size(); ++i) {
        const GenSpawn& s = c.spawns[i];
        o += (i ? ",\n    " : "\n    ");
        o += "{\"archetype\": " + json_str(s.archetype) + ", \"tile\": {\"x\": " + std::to_string(s.x) +
             ", \"y\": " + std::to_string(s.y) + "}";
        if (!s.overrides_json.empty()) o += ", \"overrides\": " + s.overrides_json;
        o += "}";
    }
    o += c.spawns.empty() ? "],\n" : "\n  ],\n";
    o += "  \"triggers\": [";
    for (std::size_t i = 0; i < c.triggers.size(); ++i) o += (i ? ", " : "") + c.triggers[i];
    o += "]\n}\n";
    return o;
}

bool walkable_connected(const GenChunk& c) {
    auto walkable = [&](int i) {
        if (c.layers[kGround][std::size_t(i)].tile == tiles::kEmpty) return false;
        for (const auto& L : c.layers)
            if (L[std::size_t(i)].flags & flags::kBlocksMove) return false;
        return true;
    };
    int start = -1, total = 0;
    for (int i = 0; i < kCells; ++i)
        if (walkable(i)) { ++total; if (start < 0) start = i; }
    if (total == 0) return true;
    std::array<bool, kCells> seen{};
    std::vector<int> stack{start};
    seen[std::size_t(start)] = true;
    int reached = 0;
    while (!stack.empty()) {
        const int i = stack.back(); stack.pop_back(); ++reached;
        const int x = i % kChunkSize, y = i / kChunkSize;
        const int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
        for (const auto& n : nb) {
            if (n[0] < 0 || n[1] < 0 || n[0] >= kChunkSize || n[1] >= kChunkSize) continue;
            const int j = idx(n[0], n[1]);
            if (!seen[std::size_t(j)] && walkable(j)) { seen[std::size_t(j)] = true; stack.push_back(j); }
        }
    }
    return reached == total;
}

} // namespace procgen
