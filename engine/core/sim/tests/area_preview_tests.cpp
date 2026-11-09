// Combat::area_preview: the tiles of the template Combat::area_targets resolves, not a second geometry.
// 1x1 expectations are the documented rules (Chebyshev tiles, cone half-angle atan 1/2, line width and length,
// cube beside the caster or centred on the aim). Cylinder height is not modelled; the test pins that it changes nothing.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::AreaShape;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";
constexpr int kMap = 32;

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json drake(const std::string& id, const char* size) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["size"] = size;
    m["hit_points"]["average"] = 200;
    return m;
}
struct Spawn { std::string archetype; int x, y; };
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> sight_walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : sight_walls) c.layers[2][std::size_t(w.y * kMap + w.x)].flags = world::tile_flags::kBlocksSight;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
ContentLibrary preview_lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(drake("t.large", "large"), "monster"));
    REQUIRE_FALSE(l.add(drake("t.huge", "huge"), "monster"));
    REQUIRE_FALSE(l.add(drake("t.gargantuan", "gargantuan"), "monster"));
    return l;
}
rules5e::SpellDef tmpl(AreaShape s, int range_ft, int size_ft, int width_ft = 0, int height_ft = 0) {
    rules5e::SpellDef d;
    d.area = s;
    d.range_ft = uint16_t(range_ft);
    d.area_size_ft = uint16_t(size_ft);
    d.area_width_ft = uint16_t(width_ft);
    d.area_height_ft = uint16_t(height_ft);
    return d;
}
std::string show(const std::vector<Vec2i>& v) {
    std::string o;
    for (Vec2i t : v) o += "(" + std::to_string(t.x) + "," + std::to_string(t.y) + ")";
    return o;
}
void expect_tiles(const std::string& what, const std::vector<Vec2i>& got, const std::vector<Vec2i>& exp) {
    CHECK_MESSAGE(got == exp, what << "\n got " << show(got) << "\n exp " << show(exp));
}

// Documented 1x1 membership on a clear map, scanned in row order and clipped to the chunk. Not the sim's doubled grid:
// a cone is Chebyshev length with half-angle atan(1/2); a line is projection (0, length] and perpendicular <= width/2.
int tiles_of(int ft) { return ft / kFeetPerTile; }
bool cone_1x1(Vec2i caster, Vec2i aim, Vec2i t, int size) {
    const int vx = t.x - caster.x, vy = t.y - caster.y;
    const int dx = aim.x - caster.x, dy = aim.y - caster.y;
    const int64_t dot = int64_t(vx) * dx + int64_t(vy) * dy;
    const int64_t cross = int64_t(vx) * dy - int64_t(vy) * dx;
    const int cheb = std::max(std::abs(vx), std::abs(vy));
    if (dot <= 0 || cheb <= 0 || cheb > size) return false;
    const int64_t cr = cross < 0 ? -cross : cross;
    return cr * 2 <= dot;
}
bool line_1x1(Vec2i caster, Vec2i aim, Vec2i t, int size, int width) {
    const int vx = t.x - caster.x, vy = t.y - caster.y;
    const int dx = aim.x - caster.x, dy = aim.y - caster.y;
    const int64_t dot = int64_t(vx) * dx + int64_t(vy) * dy;
    const int64_t cross = int64_t(vx) * dy - int64_t(vy) * dx;
    const int64_t aa = int64_t(dx) * dx + int64_t(dy) * dy;
    if (aa == 0 || dot <= 0) return false;
    if (cross * cross * 4 > int64_t(width) * width * aa) return false;
    return dot * dot <= int64_t(size) * size * aa;
}
bool cube_1x1(Vec2i caster, Vec2i aim, Vec2i t, int size, bool self) {
    const int lo = -((size - 1) / 2), hi = size / 2;
    if (!self) return t.x >= aim.x + lo && t.x <= aim.x + hi && t.y >= aim.y + lo && t.y <= aim.y + hi;
    const int dx = aim.x - caster.x, dy = aim.y - caster.y;
    if (std::abs(dx) >= std::abs(dy)) {
        const int x0 = dx > 0 ? caster.x + 1 : caster.x - size;
        return t.x >= x0 && t.x < x0 + size && t.y >= caster.y + lo && t.y <= caster.y + hi;
    }
    const int y0 = dy > 0 ? caster.y + 1 : caster.y - size;
    return t.y >= y0 && t.y < y0 + size && t.x >= caster.x + lo && t.x <= caster.x + hi;
}
bool disk(Vec2i centre, Vec2i t, int radius) {
    return std::max(std::abs(t.x - centre.x), std::abs(t.y - centre.y)) <= radius;
}
std::vector<Vec2i> scan_1x1(AreaShape shape, int range_ft, int size_ft, int width_ft, Vec2i caster, Vec2i aim) {
    const int size = tiles_of(size_ft);
    const int width = std::max(1, tiles_of(width_ft));
    const bool self = range_ft == 0;
    std::vector<Vec2i> out;
    for (int y = 0; y < kMap; ++y)
        for (int x = 0; x < kMap; ++x) {
            const Vec2i t{x, y};
            bool in = false;
            switch (shape) {
                case AreaShape::Cone: in = cone_1x1(caster, aim, t, size); break;
                case AreaShape::Line: in = line_1x1(caster, aim, t, size, width); break;
                case AreaShape::Cube: in = cube_1x1(caster, aim, t, size, self); break;
                case AreaShape::Sphere:
                case AreaShape::Cylinder: in = disk(self ? caster : aim, t, size); break;
                default: break;
            }
            if (in) out.push_back(t);
        }
    return out;
}

struct Board {
    ecs::Registry reg;
    world::Chunk ch;
    std::optional<Combat> c;
    std::vector<Vec2i> at;
    Board(const ContentLibrary& lib, std::vector<Spawn> sp, std::vector<Vec2i> walls = {}) {
        for (const auto& s : sp) at.push_back({s.x, s.y});
        ch = arena(std::move(sp), std::move(walls));
        std::string err;
        c = Combat::create(reg, ch, lib, 1, &err);
        REQUIRE_MESSAGE(c, err);
    }
    CombatantId id(std::size_t i) const { return ch.spawns[i].stable_id; }
    const Combat& view() const { return *c; }
};
} // namespace

TEST_CASE("area preview: 1x1 cone, line, cube, sphere and cylinder match the documented tile set") {
    auto lib = preview_lib();
    Board b(lib, {{kFighter, 10, 10}});
    const CombatantId me = b.id(0);
    const Vec2i caster{10, 10};
    const uint64_t hash = b.view().hash();
    const uint64_t log = b.view().log_hash();
    const std::size_t events = b.view().events().size();

    // Hand list for a 15 ft cone aimed east: length 3, and 2*|dy| <= dx. Anchors the scan below.
    const std::vector<Vec2i> cone_east{{12, 9}, {13, 9}, {11, 10}, {12, 10}, {13, 10}, {12, 11}, {13, 11}};
    CHECK(scan_1x1(AreaShape::Cone, 0, 15, 0, caster, {11, 10}) == cone_east);
    // 15 ft cone aimed north: 2*|dx| <= -dy, dy < 0.
    const std::vector<Vec2i> cone_north{{9, 7}, {10, 7}, {11, 7}, {9, 8}, {10, 8}, {11, 8}, {10, 9}};
    CHECK(scan_1x1(AreaShape::Cone, 0, 15, 0, caster, {10, 9}) == cone_north);

    auto preview = [&](const rules5e::SpellDef& d, Vec2i aim, std::string* err = nullptr) {
        return b.view().area_preview(me, d, aim, err);
    };
    expect_tiles("cone east", preview(tmpl(AreaShape::Cone, 0, 15), {11, 10}), cone_east);
    expect_tiles("cone east, aim distance ignored", preview(tmpl(AreaShape::Cone, 0, 15), {18, 10}), cone_east);
    expect_tiles("cone north", preview(tmpl(AreaShape::Cone, 0, 15), {10, 9}), cone_north);
    // Diagonal: (12,8) is on the 15 ft cone toward (11,9); (14,7) is past the length.
    const auto diagonal = preview(tmpl(AreaShape::Cone, 0, 15), {11, 9});
    CHECK(std::find(diagonal.begin(), diagonal.end(), Vec2i{12, 8}) != diagonal.end());
    CHECK(std::find(diagonal.begin(), diagonal.end(), Vec2i{14, 7}) == diagonal.end());
    expect_tiles("cone diagonal", diagonal, scan_1x1(AreaShape::Cone, 0, 15, 0, caster, {11, 9}));
    expect_tiles("cone 30 east", preview(tmpl(AreaShape::Cone, 0, 30), {11, 10}),
                 scan_1x1(AreaShape::Cone, 0, 30, 0, caster, {11, 10}));

    const std::vector<Vec2i> line_east{{11, 10}, {12, 10}, {13, 10}, {14, 10}, {15, 10}, {16, 10}};
    CHECK(scan_1x1(AreaShape::Line, 0, 30, 5, caster, {12, 10}) == line_east);
    expect_tiles("line 30x5", preview(tmpl(AreaShape::Line, 0, 30, 5), {12, 10}), line_east);
    expect_tiles("line width 0 is one tile, same as width 5", preview(tmpl(AreaShape::Line, 0, 30, 0), {12, 10}), line_east);
    expect_tiles("line 30x15", preview(tmpl(AreaShape::Line, 0, 30, 15), {12, 10}),
                 scan_1x1(AreaShape::Line, 0, 30, 15, caster, {12, 10}));
    expect_tiles("line diagonal", preview(tmpl(AreaShape::Line, 0, 30, 5), {12, 8}),
                 scan_1x1(AreaShape::Line, 0, 30, 5, caster, {12, 8}));
    // A line is directional: a non-zero range does not move its origin onto the aim or shorten it.
    expect_tiles("line ignores range", preview(tmpl(AreaShape::Line, 5, 30, 5), {12, 10}), line_east);

    const auto cube = tmpl(AreaShape::Cube, 0, 15);
    expect_tiles("cube east", preview(cube, {11, 10}), scan_1x1(AreaShape::Cube, 0, 15, 0, caster, {11, 10}));
    expect_tiles("cube north", preview(cube, {10, 9}), scan_1x1(AreaShape::Cube, 0, 15, 0, caster, {10, 9}));
    expect_tiles("cube west", preview(cube, {9, 10}), scan_1x1(AreaShape::Cube, 0, 15, 0, caster, {9, 10}));
    expect_tiles("cube south", preview(cube, {10, 11}), scan_1x1(AreaShape::Cube, 0, 15, 0, caster, {10, 11}));
    // A diagonal tie prefers the x axis, so north-east is the east cube.
    expect_tiles("cube northeast is east", preview(cube, {11, 9}), preview(cube, {11, 10}));
    expect_tiles("point cube", preview(tmpl(AreaShape::Cube, 60, 10), {13, 11}),
                 scan_1x1(AreaShape::Cube, 60, 10, 0, caster, {13, 11}));

    const auto sphere = tmpl(AreaShape::Sphere, 0, 10);
    const auto self_disk = preview(sphere, {10, 11});
    expect_tiles("self sphere", self_disk, scan_1x1(AreaShape::Sphere, 0, 10, 0, caster, {10, 11}));
    CHECK(std::find(self_disk.begin(), self_disk.end(), caster) != self_disk.end());   // the caster's tile is inside
    expect_tiles("12 ft sphere quantizes to 10 ft", preview(tmpl(AreaShape::Sphere, 0, 12), {10, 11}),
                 preview(sphere, {10, 11}));
    expect_tiles("self cylinder ignores height", preview(tmpl(AreaShape::Cylinder, 0, 10, 0, 100), {10, 11}),
                 preview(sphere, {10, 11}));
    expect_tiles("point sphere", preview(tmpl(AreaShape::Sphere, 60, 20), {15, 10}),
                 scan_1x1(AreaShape::Sphere, 60, 20, 0, caster, {15, 10}));
    expect_tiles("point cylinder ignores height", preview(tmpl(AreaShape::Cylinder, 60, 20, 0, 40), {15, 10}),
                 preview(tmpl(AreaShape::Sphere, 60, 20), {15, 10}));

    CHECK(b.view().hash() == hash);
    CHECK(b.view().log_hash() == log);
    CHECK(b.view().events().size() == events);
    CHECK(preview(sphere, {10, 11}) == b.view().area_preview(me, sphere, {10, 11}));
}

TEST_CASE("area preview: the same aim rejections as area_targets, except an empty creature list") {
    auto lib = preview_lib();
    Board b(lib, {{kFighter, 10, 10}});
    const CombatantId me = b.id(0);
    const Combat& view = b.view();
    auto both = [&](const rules5e::SpellDef& d, Vec2i aim, const char* why) {
        std::string preview_err = "unset", target_err = "unset";
        CHECK(view.area_preview(me, d, aim, &preview_err).empty());
        CHECK(view.area_targets(me, d, aim, &target_err).empty());
        CHECK(preview_err == why);
        CHECK(target_err == why);
    };
    both(tmpl(AreaShape::None, 0, 15), {11, 10}, "spell has no area");
    {
        std::string preview_err, target_err;
        const auto cone = tmpl(AreaShape::Cone, 0, 15);
        CHECK(view.area_preview(0, cone, {11, 10}, &preview_err).empty());
        CHECK(view.area_targets(0, cone, {11, 10}, &target_err).empty());
        CHECK(preview_err == "bad caster");
        CHECK(target_err == "bad caster");
    }
    both(tmpl(AreaShape::Cone, 0, 15), {32, 10}, "aim tile out of bounds");
    both(tmpl(AreaShape::Sphere, 0, 10), {-1, 0}, "aim tile out of bounds");
    both(tmpl(AreaShape::Cone, 0, 15), {10, 10}, "aim must differ from the caster's tile");
    both(tmpl(AreaShape::Line, 0, 30, 5), {10, 10}, "aim must differ from the caster's tile");
    both(tmpl(AreaShape::Cube, 0, 15), {10, 10}, "aim must differ from the caster's tile");
    both(tmpl(AreaShape::Sphere, 20, 10), {15, 10}, "aim beyond range");
    both(tmpl(AreaShape::Cube, 20, 10), {15, 10}, "aim beyond range");

    // A self sphere may be aimed at the caster's own tile. The disk is still about the caster.
    // A success leaves *error alone, including a string the caller had already filled.
    std::string err;
    CHECK_FALSE(view.area_preview(me, tmpl(AreaShape::Sphere, 0, 10), {10, 10}, &err).empty());
    CHECK(err.empty());
    err = "stale";
    CHECK_FALSE(view.area_preview(me, tmpl(AreaShape::Sphere, 0, 10), {10, 11}, &err).empty());
    CHECK(err == "stale");
    // A rejected call with a null error pointer does not throw; a successful one leaves a fresh string empty.
    CHECK(view.area_preview(me, tmpl(AreaShape::Cone, 0, 15), {40, 40}).empty());
    CHECK_FALSE(view.area_preview(me, tmpl(AreaShape::Cone, 0, 15), {11, 10}).empty());

    // Nobody stands in a point sphere in the empty corner. The tiles are still the disk.
    // area_targets reports no creature; the preview does not.
    const auto far = tmpl(AreaShape::Sphere, 90, 5);
    const Vec2i corner{28, 28};
    err.clear();
    const auto tiles = view.area_preview(me, far, corner, &err);
    expect_tiles("empty corner", tiles, scan_1x1(AreaShape::Sphere, 90, 5, 0, {10, 10}, corner));
    CHECK(err.empty());
    std::string target_err;
    CHECK(view.area_targets(me, far, corner, &target_err).empty());
    CHECK(target_err == "no creature in the area");
    const auto self_targets = view.area_targets(me, tmpl(AreaShape::Sphere, 0, 10), {10, 11});
    CHECK(std::find(self_targets.begin(), self_targets.end(), me) == self_targets.end());
}

TEST_CASE("area preview: a 1x1 creature is hit exactly when its tile is in the preview") {
    auto lib = preview_lib();
    const Vec2i caster{10, 10};
    const auto cone = tmpl(AreaShape::Cone, 0, 15);
    const Vec2i aim{11, 10};
    const auto shape = scan_1x1(AreaShape::Cone, 0, 15, 0, caster, aim);
    std::vector<Spawn> sp{{kFighter, caster.x, caster.y}};
    std::vector<Vec2i> probes = shape;
    probes.push_back({13, 12});   // one step past the half-angle
    probes.push_back({14, 10});   // one step past the length
    probes.push_back({9, 10});    // behind the caster
    for (Vec2i t : probes) sp.push_back({kGoblin, t.x, t.y});
    Board b(lib, sp);
    const auto tiles = b.view().area_preview(b.id(0), cone, aim);
    expect_tiles("crowded cone", tiles, shape);
    std::vector<Vec2i> hit;
    std::string err;
    for (CombatantId id : b.view().area_targets(b.id(0), cone, aim, &err))
        for (std::size_t i = 1; i < b.at.size(); ++i)
            if (b.id(i) == id) hit.push_back(b.at[i]);
    CHECK(err.empty());
    std::sort(hit.begin(), hit.end(), [](Vec2i p, Vec2i q) { return p.y != q.y ? p.y < q.y : p.x < q.x; });
    expect_tiles("targets are the preview tiles", hit, shape);
}

TEST_CASE("area preview: line of sight punches the same holes area_targets uses") {
    auto lib = preview_lib();
    const auto cone = tmpl(AreaShape::Cone, 0, 30);
    Board open(lib, {{kFighter, 10, 10}});
    const auto clear = open.view().area_preview(open.id(0), cone, {13, 10});
    CHECK(std::find(clear.begin(), clear.end(), Vec2i{13, 10}) != clear.end());

    Board walled(lib, {{kFighter, 10, 10}, {kGoblin, 11, 10}, {kGoblin, 13, 10}}, {{12, 10}});
    const auto blocked = walled.view().area_preview(walled.id(0), cone, {13, 10});
    CHECK(std::find(blocked.begin(), blocked.end(), Vec2i{11, 10}) != blocked.end());
    CHECK(std::find(blocked.begin(), blocked.end(), Vec2i{12, 10}) != blocked.end());   // the wall tile is the endpoint
    CHECK(std::find(blocked.begin(), blocked.end(), Vec2i{13, 10}) == blocked.end());
    std::string err;
    const auto ids = walled.view().area_targets(walled.id(0), cone, {13, 10}, &err);
    CHECK(err.empty());
    CHECK(ids.size() == 1);
    CHECK(ids[0] == walled.id(1));   // (11,10) is seen; (13,10) is not

    // Point origin: the aim itself has to be seen. A 1x1 caster cannot see past the wall; the preview is empty.
    const auto ball = tmpl(AreaShape::Sphere, 60, 10);
    err = "unset";
    CHECK(walled.view().area_preview(walled.id(0), ball, {15, 10}, &err).empty());
    CHECK(err == "aim not in line of sight");
}

TEST_CASE("area preview: footprints use the caster's centre, not the anchor tile") {
    auto lib = preview_lib();
    // Self sphere, radius 5 ft, is the footprint plus its adjacent ring (rect_distance), corners included.
    {
        Board b(lib, {{"t.large", 10, 10}});
        const auto got = b.view().area_preview(b.id(0), tmpl(AreaShape::Sphere, 0, 5), {12, 10});
        std::vector<Vec2i> exp;
        for (int y = 0; y < kMap; ++y)
            for (int x = 0; x < kMap; ++x)
                if (rect_distance({10, 10}, 2, {x, y}, 1) <= 1) exp.push_back({x, y});
        expect_tiles("large self sphere", got, exp);
        // A point sphere of the same radius centred on the anchor is a different set: (12,11) touches the
        // footprint but is two tiles from the anchor. range_ft is what selects which one.
        const auto point = b.view().area_preview(b.id(0), tmpl(AreaShape::Sphere, 30, 5), {10, 10});
        CHECK(std::find(got.begin(), got.end(), Vec2i{12, 11}) != got.end());
        CHECK(std::find(point.begin(), point.end(), Vec2i{12, 11}) == point.end());
        expect_tiles("point sphere on the anchor", point, scan_1x1(AreaShape::Sphere, 30, 5, 0, {10, 10}, {10, 10}));
        expect_tiles("large cylinder ignores height",
                     b.view().area_preview(b.id(0), tmpl(AreaShape::Cylinder, 0, 5, 0, 40), {12, 10}), exp);
    }
    // 15 ft cube east of a 2x2: x 12..14, y 9..11. A 4x4's south cube is x 10..12, y 14..16.
    {
        Board b(lib, {{"t.large", 10, 10}});
        std::vector<Vec2i> exp;
        for (int y = 9; y <= 11; ++y)
            for (int x = 12; x <= 14; ++x) exp.push_back({x, y});
        expect_tiles("large cube east", b.view().area_preview(b.id(0), tmpl(AreaShape::Cube, 0, 15), {12, 10}), exp);
    }
    {
        Board b(lib, {{"t.gargantuan", 10, 10}});
        std::vector<Vec2i> exp;
        for (int y = 14; y <= 16; ++y)
            for (int x = 10; x <= 12; ++x) exp.push_back({x, y});
        expect_tiles("gargantuan cube south", b.view().area_preview(b.id(0), tmpl(AreaShape::Cube, 0, 15), {11, 14}), exp);
    }
    // Huge cone, 30 ft east from the centre tile (11,11). Pins from the footprint hand check, not a full listing.
    {
        const std::vector<Vec2i> pins{{13, 11}, {17, 11}, {17, 8}, {18, 11}, {17, 7}, {9, 11}};
        const std::vector<Vec2i> inside{{13, 11}, {17, 11}, {17, 8}};
        std::vector<Spawn> sp{{"t.huge", 10, 10}};
        for (Vec2i t : pins) sp.push_back({kGoblin, t.x, t.y});
        Board b(lib, sp);
        const auto cone = tmpl(AreaShape::Cone, 0, 30);
        const auto tiles = b.view().area_preview(b.id(0), cone, {13, 11});
        for (Vec2i t : inside) CHECK(std::find(tiles.begin(), tiles.end(), t) != tiles.end());
        for (Vec2i t : pins)
            if (std::find(inside.begin(), inside.end(), t) == inside.end())
                CHECK(std::find(tiles.begin(), tiles.end(), t) == tiles.end());
        std::vector<Vec2i> hit;
        for (CombatantId id : b.view().area_targets(b.id(0), cone, {13, 11}))
            for (std::size_t i = 1; i < b.at.size(); ++i)
                if (b.id(i) == id) hit.push_back(b.at[i]);
        std::sort(hit.begin(), hit.end(), [](Vec2i p, Vec2i q) { return p.y != q.y ? p.y < q.y : p.x < q.x; });
        std::vector<Vec2i> exp = inside;
        std::sort(exp.begin(), exp.end(), [](Vec2i p, Vec2i q) { return p.y != q.y ? p.y < q.y : p.x < q.x; });
        expect_tiles("huge cone probes", hit, exp);
    }
    // Gargantuan 30x5 line aimed at (14,11). The centre sits between tiles, so the line is not the anchor's row.
    {
        const std::vector<Vec2i> inside{{14, 11}, {15, 11}, {16, 11}, {17, 10}};
        const std::vector<Vec2i> outside{{17, 11}, {18, 10}};
        std::vector<Spawn> sp{{"t.gargantuan", 10, 10}};
        for (Vec2i t : inside) sp.push_back({kGoblin, t.x, t.y});
        for (Vec2i t : outside) sp.push_back({kGoblin, t.x, t.y});
        Board b(lib, sp);
        const auto line = tmpl(AreaShape::Line, 0, 30, 5);
        const auto tiles = b.view().area_preview(b.id(0), line, {14, 11});
        for (Vec2i t : inside) CHECK(std::find(tiles.begin(), tiles.end(), t) != tiles.end());
        for (Vec2i t : outside) CHECK(std::find(tiles.begin(), tiles.end(), t) == tiles.end());
        CHECK(b.view().area_targets(b.id(0), line, {14, 11}).size() == inside.size());
    }
}

TEST_CASE("area preview: a partly covered footprint is one target, and the map edge clips the disk") {
    auto lib = preview_lib();
    {
        Board b(lib, {{kFighter, 10, 10}, {"t.gargantuan", 20, 10}});
        const auto ball = tmpl(AreaShape::Sphere, 120, 10);
        const auto tiles = b.view().area_preview(b.id(0), ball, {18, 10});
        CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{20, 10}) != tiles.end());
        CHECK(std::find(tiles.begin(), tiles.end(), Vec2i{21, 10}) == tiles.end());
        const auto ids = b.view().area_targets(b.id(0), ball, {18, 10});
        CHECK(ids.size() == 1);
        CHECK(ids[0] == b.id(1));
    }
    {
        Board b(lib, {{kFighter, 0, 0}});
        const auto got = b.view().area_preview(b.id(0), tmpl(AreaShape::Sphere, 0, 10), {1, 0});
        std::vector<Vec2i> exp;
        for (int y = 0; y <= 2; ++y)
            for (int x = 0; x <= 2; ++x) exp.push_back({x, y});
        expect_tiles("corner sphere", got, exp);
        for (Vec2i t : got) CHECK(t.x >= 0);
    }
    // A 2x2 still sees an aim the anchor's line cannot, so the point sphere is the disk about that aim.
    {
        Board blocked(lib, {{"t.large", 10, 10}}, {{12, 10}});
        std::string err;
        const auto disk = blocked.view().area_preview(blocked.id(0), tmpl(AreaShape::Sphere, 90, 5), {16, 10}, &err);
        CHECK(err.empty());
        expect_tiles("large caster sees the aim", disk, scan_1x1(AreaShape::Sphere, 90, 5, 0, {10, 10}, {16, 10}));
        Board small(lib, {{kFighter, 10, 10}}, {{12, 10}});
        err = "unset";
        CHECK(small.view().area_preview(small.id(0), tmpl(AreaShape::Sphere, 90, 5), {16, 10}, &err).empty());
        CHECK(err == "aim not in line of sight");
    }
}
