#include "sim/combat.hpp"
#include "sim/light_driver.hpp"
#include "sim/trace.hpp"

#include <algorithm>
#include <cstdlib>

#include "assets/schema_registry.hpp"
#include "base/canonical_hash.hpp"
#include "rules5e/conditions.hpp"
#include "rules5e/equipment.hpp"

namespace eng::sim {

using rules5e::ActionKind;
using rules5e::ActionRequest;

std::optional<std::string> spawn_team_name(const world::EntitySpawn& sp, const Archetype& a, std::string* error) {
    std::string name = a.schema_type == "character" ? "party" : "monsters";
    if (!sp.overrides_json.empty()) {
        const assets::Json ov = assets::Json::parse(sp.overrides_json, nullptr, false);
        if (ov.is_object()) {
            if (auto t = ov.find("team"); t != ov.end()) {
                bool ok = t->is_string() && !t->get_ref<const std::string&>().empty();
                if (ok) {
                    const auto& v = t->get_ref<const std::string&>();
                    ok = v[0] >= 'a' && v[0] <= 'z';
                    for (char ch : v) ok = ok && ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_');
                    if (ok) name = v;
                }
                if (!ok) {
                    if (error) *error = "spawn overrides.team must match ^[a-z][a-z0-9_]*$";
                    return std::nullopt;
                }
            }
        }
    }
    return name;
}

std::optional<Combat> Combat::create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                     uint64_t seed, std::string* error) {
    return create(reg, chunk, content, seed, CombatOptions{}, error);
}

std::optional<Combat> Combat::create(ecs::Registry& reg, std::span<const world::Chunk* const> chunks_in,
                                     const ContentLibrary& content, uint64_t seed, std::string* error) {
    return create(reg, chunks_in, content, seed, CombatOptions{}, error);
}

std::optional<Combat> Combat::create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                     uint64_t seed, CombatOptions options, std::string* error) {
    // Single-chunk combat keeps the v0 chunk-local frame (local == world tile), whatever the
    // chunk's coord: goldens and existing callers are unchanged.
    world::Chunk local = chunk;
    local.coord.x = 0;
    local.coord.y = 0;
    const world::Chunk* one[] = {&local};
    auto c = create(reg, std::span<const world::Chunk* const>(one), content, seed, options, error);
    if (c) c->frame_offset_ = world::ChunkCoord{chunk.coord.x, chunk.coord.y, 0};
    return c;
}

std::optional<Combat> Combat::create(ecs::Registry& reg, std::span<const world::Chunk* const> chunks_in,
                                     const ContentLibrary& content, uint64_t seed, CombatOptions options, std::string* error) {
    // Canonical chunk order (coord), so callers' order (streamer, threads) cannot matter.
    std::vector<const world::Chunk*> chunks(chunks_in.begin(), chunks_in.end());
    std::sort(chunks.begin(), chunks.end(), [](const world::Chunk* a, const world::Chunk* b) { return a->coord < b->coord; });
    Grid grid{std::span<const world::Chunk* const>(chunks)};
    if (!grid.ok()) { if (error) *error = grid.error(); return std::nullopt; }
    struct WSpawn { const world::EntitySpawn* sp; Vec2i tile; };
    std::vector<WSpawn> spawns;
    for (const world::Chunk* ch : chunks)
        for (const auto& sp : ch->spawns) spawns.push_back({&sp, Grid::to_world(ch->coord, sp.tile)});
    Combat c(reg, std::move(grid), options);
    for (const world::Chunk* ch : chunks) c.frame_chunks_.push_back(ch->coord);
    c.seed_ = seed;
    c.content_ = &content;   // R-G12: forms resolve against the library (archetype pointers already live as long)
    for (const auto& [aid, arch] : content.all())   // Polymorph's choices: rated beasts, byte order of ids
        if (arch.schema_type == "monster" && arch.cr_x8 >= 0 && arch.base.creature_type == rules5e::CreatureType::Beast)
            c.beasts_.push_back(&arch);
    // Team names -> ids by byte order of the distinct names (spawn-order independent).
    std::vector<std::string> spawn_teams;
    for (const auto& ws : spawns) {
        const auto& sp = *ws.sp;
        const Archetype* a = content.find(sp.archetype);
        if (!a) continue;
        auto t = spawn_team_name(sp, *a, error);
        if (!t) return std::nullopt;
        spawn_teams.push_back(*t);
    }
    c.team_names_ = spawn_teams;
    std::sort(c.team_names_.begin(), c.team_names_.end());
    c.team_names_.erase(std::unique(c.team_names_.begin(), c.team_names_.end()), c.team_names_.end());
    if (c.team_names_.size() > 255) { if (error) *error = "too many teams"; return std::nullopt; }
    std::vector<rules5e::Combatant> cs;
    struct Placed { Vec2i anchor; int n; };
    std::vector<Placed> placed;   // footprints so far, for the overlap check (multi-tile spawns only)
    std::size_t ti = 0;
    for (const auto& ws : spawns) {
        const auto& sp = *ws.sp;
        const Archetype* a = content.find(sp.archetype);
        if (!a) continue;   // non-combatant spawn
        const std::string& team_name = spawn_teams[ti++];
        if (sp.stable_id == 0) { if (error) *error = "spawn stable_id 0 is reserved"; return std::nullopt; }
        if (reg.find(sp.stable_id)) { if (error) *error = "duplicate spawn stable_id"; return std::nullopt; }
        const int fp = a->footprint;
        for (const Placed& o : placed)   // 1x1 vs 1x1 overlap stays unchecked (v0 behaviour)
            if ((fp > 1 || o.n > 1) && rect_distance(ws.tile, fp, o.anchor, o.n) == 0) {
                if (error) *error = "spawn footprint overlaps another spawn";
                return std::nullopt;
            }
        if (fp > 1)
            for (int dy = 0; dy < fp; ++dy)
                for (int dx = 0; dx < fp; ++dx)
                    if (!c.grid_.walkable({ws.tile.x + dx, ws.tile.y + dy})) {
                        if (error) *error = "spawn footprint is not on walkable ground";
                        return std::nullopt;
                    }
        placed.push_back({ws.tile, fp});
        rules5e::Combatant k = a->base;
        k.id = sp.stable_id;
        k.team = uint8_t(std::lower_bound(c.team_names_.begin(), c.team_names_.end(), team_name) - c.team_names_.begin());
        const ecs::Entity e = reg.create(sp.stable_id);
        reg.emplace(e, Health{k.hp, k.hp_max});
        reg.emplace(e, Team{k.team});
        reg.emplace(e, TilePos{ws.tile.x, ws.tile.y});
        if (fp > 1) reg.emplace(e, Footprint{uint8_t(fp)});
        reg.emplace(e, CombatantLink{k.id});
        c.slots_.push_back({k.id, e, a});
        if (!a->equipment.empty()) c.equipment_[k.id] = a->equipment;
        cs.push_back(std::move(k));
    }
    std::vector<rules5e::Event> opening;
    if (!content.spells()->all().empty()) c.spells_ = content.spells();
    c.state_ = rules5e::begin_encounter(std::move(cs), seed, &opening);
    // R-G12 summons (7.2r, §3.0): seed rules' summon counter above every spawn id of every chunk (combatant or not). A seeded
    // counter alone never folds (has_summon_state), so this hashes as before.
    {
        uint32_t top = 0;
        for (const auto& ws : spawns) top = std::max<uint32_t>(top, ws.sp->stable_id);
        c.summon_ids_exhausted_ = top == UINT32_MAX;
        c.state_.next_summon_id = c.summon_ids_exhausted_ ? UINT32_MAX : top + 1;
    }
    c.created_count_ = c.slots_.size();
    // Limited-use features live in the rules state (hashed, rebuilt by replay).
    for (const auto& sl : c.slots_)
        for (const auto& f : sl.archetype->active)
            if (f.uses > 0)
                rules5e::register_feature(c.state_, sl.id, f.feature_id, f.uses,
                                          f.recharge == Recharge::ShortRest  ? rules5e::FeatureRecharge::ShortRest
                                          : f.recharge == Recharge::LongRest ? rules5e::FeatureRecharge::LongRest
                                                                             : rules5e::FeatureRecharge::None);
    // resources[] pools (row 13): registered first so features and save actions can spend from them (pools fold under 0xA3).
    for (const auto& sl : c.slots_)
        for (const auto& r : sl.archetype->resources)
            rules5e::register_pool(c.state_, sl.id, r.pool_id, r.max, r.start,
                                   r.recharge == uint8_t(rules5e::FeatureRecharge::ShortRest)  ? rules5e::FeatureRecharge::ShortRest
                                   : r.recharge == uint8_t(rules5e::FeatureRecharge::LongRest) ? rules5e::FeatureRecharge::LongRest
                                                                                              : rules5e::FeatureRecharge::None);
    // Save actions (monster@4 / character@4): recharge and per-day / per-rest usage rows, only for creatures
    // that have one (digest-neutral for every other combat).
    for (const auto& sl : c.slots_)
        for (const auto& sa : sl.archetype->save_actions) {
            if (sa.usage_kind == 3 && !rules5e::find_pool(c.state_, sl.id, sa.pool_id))
                rules5e::register_pool(c.state_, sl.id, sa.pool_id, sa.pool_max, sa.pool_start,
                                       sa.pool_recharge == Recharge::ShortRest  ? rules5e::FeatureRecharge::ShortRest
                                       : sa.pool_recharge == Recharge::LongRest ? rules5e::FeatureRecharge::LongRest
                                                                                : rules5e::FeatureRecharge::None);
            else if (sa.usage_kind == 1) rules5e::register_action_use(c.state_, sl.id, sa.spell_id, false);
            else if (sa.usage_kind == 2)
                rules5e::register_feature(c.state_, sl.id, sa.spell_id, sa.uses,
                                          sa.recharge == Recharge::ShortRest ? rules5e::FeatureRecharge::ShortRest
                                                                             : rules5e::FeatureRecharge::LongRest);
        }
    // EC deferred monsters: on_event triggers with usage recharge N-6 get an ActionUse row (owner, trigger id), only for creatures
    // that have one (action_uses folds under 0xA1 only when non-empty, so every other combat hashes as before).
    for (const auto& sl : c.slots_)
        for (const auto& tr : sl.archetype->base.triggers)
            if (tr.recharge_min != 0) rules5e::register_action_use(c.state_, sl.id, tr.id, false);
    c.apply_rules_events(opening);
    // v6: where each multi-tile creature stands, once, in spawn order (nothing for 1x1-only combats).
    for (const auto& sl : c.slots_)
        if (sl.archetype->footprint > 1) {
            SimEvent se;
            se.kind = SimEvent::Kind::Footprint;
            se.actor = sl.id;
            se.tile = c.pos(sl.id);
            se.rules.a = sl.archetype->footprint;
            c.events_.push_back(se);
        }
    if (c.options_.equipment) c.setup_equipment(content);   // EQUIPMENT.md 9: nothing at all with the option off
    return c;
}

const Combat::Slot* Combat::slot(CombatantId id) const {
    for (const auto& s : slots_) if (s.id == id) return &s;
    return nullptr;
}
ecs::Entity Combat::entity(CombatantId id) const { const Slot* s = slot(id); return s ? s->entity : ecs::kNullEntity; }
const Archetype* Combat::archetype(CombatantId id) const { const Slot* s = slot(id); return s ? s->archetype : nullptr; }
Vec2i Combat::pos(CombatantId id) const {
    const Slot* s = slot(id);
    if (!s) return {};
    if (s->gone) return s->last;   // R-G12: where it stood when it ended
    const TilePos* p = reg_->get<TilePos>(s->entity);
    return p ? Vec2i{p->x, p->y} : Vec2i{};
}
std::optional<uint8_t> Combat::multiattack_option(CombatantId id, std::string_view option_id) const {
    const auto* c = rules5e::find(state_, id);
    if (!c) return std::nullopt;
    for (std::size_t i = 0; i < c->multiattacks.size() && i < 255; ++i)
        if (c->multiattacks[i].id == option_id && c->multiattacks[i].after_action == 0) return uint8_t(i + 1);
    return std::nullopt;
}

bool Combat::hostile(CombatantId a, CombatantId b) const {
    const auto* ca = rules5e::find(state_, a);
    const auto* cb = rules5e::find(state_, b);
    return ca && cb && ca->team != cb->team;
}
const rules5e::SpellDef* Combat::spell_def(uint32_t id) const { return spells_ ? spells_->find(id) : nullptr; }

namespace {
using rules5e::AreaShape;
int64_t i64(int v) { return v; }
}

std::vector<CombatantId> Combat::area_targets(CombatantId caster, const rules5e::SpellDef& def, Vec2i aim,
                                              std::string* error) const {
    auto fail = [&](const char* m) { if (error) *error = m; return std::vector<CombatantId>{}; };
    if (def.area == AreaShape::None) return fail("spell has no area");
    if (!rules5e::find(state_, caster)) return fail("bad caster");
    if (!grid_.in_bounds(aim)) return fail("aim tile out of bounds");
    const Spot cs = spot(caster);
    const bool self_origin = def.range_ft == 0;
    const bool directional = def.area == AreaShape::Cone || def.area == AreaShape::Line;
    const int size = int(def.area_size_ft) / kFeetPerTile;   // cone/line length, cube side, sphere/cylinder radius (tiles)
    const int width = std::max(1, int(def.area_width_ft) / kFeetPerTile);
    // Doubled coordinates keep even footprints' centres integral.
    const Vec2i c2{2 * cs.p.x + cs.n - 1, 2 * cs.p.y + cs.n - 1};
    const Vec2i a2{2 * aim.x - c2.x, 2 * aim.y - c2.y};   // aim relative to the caster's centre
    if (!self_origin && !directional) {
        if (rect_distance(cs.p, cs.n, aim, 1) * kFeetPerTile > int(def.range_ft)) return fail("aim beyond range");
        bool seen = false;
        for (int y = 0; y < cs.n && !seen; ++y)
            for (int x = 0; x < cs.n && !seen; ++x) seen = grid_.line_of_sight({cs.p.x + x, cs.p.y + y}, aim);
        if (!seen) return fail("aim not in line of sight");
    } else if (directional || def.area == AreaShape::Cube) {
        if (a2.x == 0 && a2.y == 0) return fail("aim must differ from the caster's tile");
    }
    // Template membership of one tile.
    auto inside = [&](Vec2i t) {
        const Vec2i v2{2 * t.x - c2.x, 2 * t.y - c2.y};   // tile centre relative to the caster centre, doubled
        switch (def.area) {
            case AreaShape::Cone: {
                const int64_t dot = i64(v2.x) * a2.x + i64(v2.y) * a2.y, cross = i64(v2.x) * a2.y - i64(v2.y) * a2.x;
                const int cheb = std::max(std::abs(v2.x), std::abs(v2.y));
                const int64_t cr = cross < 0 ? -cross : cross;
                // 5e cone: as wide as it is long, i.e. half angle atan(1/2): |cross| * 2 <= dot.
                return dot > 0 && cr * 2 <= dot && cheb <= 2 * size && cheb > 0;
            }
            case AreaShape::Line: {
                const int64_t dot = i64(v2.x) * a2.x + i64(v2.y) * a2.y, cross = i64(v2.x) * a2.y - i64(v2.y) * a2.x;
                const int64_t aa = i64(a2.x) * a2.x + i64(a2.y) * a2.y;
                // Perpendicular distance <= width / 2 and projection in (0, length], all in tiles.
                return dot > 0 && cross * cross <= i64(width) * width * aa && dot * dot <= 4 * i64(size) * size * aa;
            }
            case AreaShape::Cube: {
                if (!self_origin) {   // centred on the aim tile
                    const int lo = -((size - 1) / 2), hi = size / 2;
                    return t.x >= aim.x + lo && t.x <= aim.x + hi && t.y >= aim.y + lo && t.y <= aim.y + hi;
                }
                // Starts at the caster: the side-`size` square touching the caster's footprint on the side the aim
                // points to (dominant axis; a tie prefers x), centred across on the footprint's centre.
                const bool horiz = std::abs(a2.x) >= std::abs(a2.y);
                const int lo = -((size - 1) / 2), hi = size / 2;
                const int mid_x = (c2.x - 1 + 1) / 2, mid_y = (c2.y - 1 + 1) / 2;   // centre tile (lower of an even pair)
                if (horiz) {
                    const int x0 = a2.x > 0 ? cs.p.x + cs.n : cs.p.x - size;
                    return t.x >= x0 && t.x < x0 + size && t.y >= mid_y + lo && t.y <= mid_y + hi;
                }
                const int y0 = a2.y > 0 ? cs.p.y + cs.n : cs.p.y - size;
                return t.y >= y0 && t.y < y0 + size && t.x >= mid_x + lo && t.x <= mid_x + hi;
            }
            case AreaShape::Sphere:
            case AreaShape::Cylinder: {   // 2D: radius in tiles by Chebyshev distance (the sim's distance metric)
                if (self_origin) return rect_distance(cs.p, cs.n, t, 1) <= size;
                return std::max(std::abs(t.x - aim.x), std::abs(t.y - aim.y)) <= size;
            }
            default: return false;
        }
    };
    // Where line of sight is taken from: the aim tile for a point origin, otherwise the caster's footprint.
    auto origin_sees = [&](Vec2i t) {
        if (!self_origin && !directional) return grid_.line_of_sight(aim, t);
        for (int y = 0; y < cs.n; ++y)
            for (int x = 0; x < cs.n; ++x)
                if (grid_.line_of_sight({cs.p.x + x, cs.p.y + y}, t)) return true;
        return false;
    };
    std::vector<CombatantId> out;
    for (const auto& sl : slots_) {
        const auto* c = rules5e::find(state_, sl.id);
        if (sl.id == caster || !c || c->dead) continue;
        const Spot s = spot(sl.id);
        bool hit = false;
        for (int y = 0; y < s.n && !hit; ++y)
            for (int x = 0; x < s.n && !hit; ++x) {
                const Vec2i t{s.p.x + x, s.p.y + y};
                hit = inside(t) && origin_sees(t);
            }
        if (hit) out.push_back(sl.id);
    }
    if (out.empty()) return fail("no creature in the area");
    return out;
}

bool Combat::spell_in_reach(const rules5e::SpellDef& def, CombatantId caster, CombatantId target) const {
    if (derived_area_) return true;   // area_targets already applied range, origin and line of sight
    if (def.area != AreaShape::None && def.range_ft == 0) {   // self-origin template: anyone inside its extent in sight
        if (caster == target) return true;
        return distance_tiles(caster, target) * kFeetPerTile <= int(def.area_size_ft) && line_of_sight(caster, target);
    }
    if (def.range_ft == 0) return caster == target;   // self
    // EC spider follow-up: a long range (attack_effect Web) extends the legal range (disadvantage beyond range_ft)
    if (distance_tiles(caster, target) * kFeetPerTile > std::max<int>(def.range_ft, def.long_range_ft)) return false;
    return caster == target || line_of_sight(caster, target);
}

int Combat::footprint_size(CombatantId id) const {
    const Slot* s = slot(id);
    if (!s) return 1;
    // The loader sets Combatant::size from the stat block (Medium when size is inert, so it matches Archetype::footprint);
    // a transform sets the form's size and a revert restores it.
    // SRD Squeezing (feat/size-space): a squeezing creature occupies a space one size smaller.
    if (const auto* k = rules5e::find(state_, id)) {
        const int n = int(footprint_of(CreatureSize(uint8_t(k->size))));
        return k->squeezing ? std::max(1, n - 1) : n;
    }
    return int(s->archetype->footprint);
}

int Combat::natural_footprint(CombatantId id) const {
    const Slot* s = slot(id);
    if (!s) return 1;
    if (const auto* k = rules5e::find(state_, id)) return int(footprint_of(CreatureSize(uint8_t(k->size))));
    return int(s->archetype->footprint);
}

// SRD 5.1 Moving Around Other Creatures (feat/size-space): a creature may move through an ally's space, and through a
// hostile's space only when the hostile is at least two sizes larger or smaller. Swarms already ignore spaces (occupied_except).
bool Combat::may_pass(CombatantId mover, CombatantId other) const {
    if (!hostile(mover, other)) return true;
    const auto* a = rules5e::find(state_, mover);
    const auto* b = rules5e::find(state_, other);
    return a && b && std::abs(int(a->size) - int(b->size)) >= 2;
}

// The living creature (other than `except`, swarms excluded like occupied_except) whose footprint covers `t`; 0 = none.
namespace { constexpr int kNoHeightFt = -0x7fffffff; }   // occupant_at: ignore vertical spans
CombatantId Combat::occupant_at(Vec2i t, CombatantId except) const { return occupant_at(t, except, kNoHeightFt); }
CombatantId Combat::occupant_at(Vec2i t, CombatantId except, int mover_abs_ft) const {
    const int mover_fp = mover_abs_ft != kNoHeightFt ? footprint_size(except) : 1;
    bool mover_swarm = false;
    for (const auto& s : slots_) if (s.id == except) mover_swarm = s.archetype->swarm;
    if (mover_swarm) return 0;
    for (const auto& s : slots_) {
        const auto* c = rules5e::find(state_, s.id);
        if (s.id == except || !c || c->dead || s.archetype->swarm) continue;
        const Spot sp = spot(s.id);
        if (t.x >= sp.p.x && t.x < sp.p.x + sp.n && t.y >= sp.p.y && t.y < sp.p.y + sp.n) {
            // altitude PR2: a creature whose vertical span does not overlap the mover's is not in its way.
            if (mover_abs_ft != kNoHeightFt && !spans_overlap(mover_abs_ft, mover_fp, abs_height_ft(s.id), sp.n)) continue;
            return s.id;
        }
    }
    return 0;
}

// Does `id`'s footprint at `anchor` (side n) share a tile with another living creature's space?
bool Combat::overlaps_other(CombatantId id, Vec2i anchor, int n) const {
    // altitude PR2: at the height the mover would have there (an airborne flyer keeps its absolute height).
    const auto* c = rules5e::find(state_, id);
    const int abs_ft = airborne(id) ? abs_height_ft(id)
                                    : grid_.footprint_ground_ft(anchor, n) + (burrowed(id) && c ? int(c->alt_ft) : 0);
    for (int dy = 0; dy < n; ++dy)
        for (int dx = 0; dx < n; ++dx)
            if (occupant_at({anchor.x + dx, anchor.y + dy}, id, abs_ft)) return true;
    return false;
}

// One 5 ft step of the current mover (Step command), checked without side effects. Every tile the footprint newly enters
// must be walkable; an occupied one only when may_pass (and it is difficult terrain: 10 ft). The full footprint is tried
// first; a Large+ creature that does not fit squeezes into a space one size smaller (side n - 1: every foot costs one extra
// foot). A squeezing creature stops squeezing on the first step where its full footprint fits.
std::optional<std::string> Combat::plan_step(CombatantId mover, Vec2i to, StepPlan& plan, bool allow_squeeze) const {
    const Vec2i from = pos(mover);
    if (chebyshev(from, to) != 1) return std::string("step must be to an adjacent tile");
    const int cur = footprint_size(mover), full = natural_footprint(mover);
    const auto* me = rules5e::find(state_, mover);
    const bool squeezing = me && me->squeezing;
    const MoveCaps caps = caps_of(mover);
    const bool air = airborne(mover);   // altitude PR2: flies over low obstacles and creatures below / above it
    const int air_abs = air ? abs_height_ft(mover) : -1;
    const bool under = !air && burrowed(mover);   // tunnels under walkable ground, below surface creatures
    const int depth = under ? int(me->alt_ft) : 0;
    auto fits = [&](int n, bool& through) -> std::optional<std::string> {
        through = false;
        const int abs_ft = air ? air_abs : grid_.footprint_ground_ft(to, n) + depth;
        for (int dy = 0; dy < n; ++dy)
            for (int dx = 0; dx < n; ++dx) {
                const Vec2i t{to.x + dx, to.y + dy};
                if (t.x >= from.x && t.x < from.x + cur && t.y >= from.y && t.y < from.y + cur) continue;
                if (under ? !(grid_.walkable(t) && !grid_.water(t))
                          : air ? !grid_.passable_over(t, caps, air_abs) : !grid_.enterable(t, caps))
                    return std::string("tile not walkable");   // feat/move-modes
                if (const CombatantId o = occupant_at(t, mover, abs_ft)) {
                    if (!may_pass(mover, o)) return std::string("tile occupied");
                    through = true;
                }
            }
        return std::nullopt;
    };
    bool through = false;
    auto err = fits(full, through);
    plan.n = full;
    plan.squeeze = squeezing ? 2 : 0;
    if (err && full >= 2 && (allow_squeeze || squeezing)) {
        bool t2 = false;
        if (!fits(full - 1, t2)) {
            err.reset();
            through = t2;
            plan.n = full - 1;
            plan.squeeze = squeezing ? 0 : 1;
        }
    }
    if (err) return err;
    int cost = step_cost_ft(mover, from, to, plan.n, through || overlaps_other(mover, to, plan.n));
    if (plan.n < full) cost *= 2;                                                 // squeezing: +1 ft per ft
    plan.cost = cost;
    plan.ends_in_other = overlaps_other(mover, to, plan.n);
    return std::nullopt;
}

std::vector<Vec2i> Combat::footprint_tiles_at(CombatantId id, Vec2i anchor) const {
    const int n = footprint_size(id);
    std::vector<Vec2i> out;
    out.reserve(std::size_t(n * n));
    for (int dy = 0; dy < n; ++dy)
        for (int dx = 0; dx < n; ++dx) out.push_back({anchor.x + dx, anchor.y + dy});
    return out;
}

int Combat::distance_tiles(CombatantId a, CombatantId b) const {
    const Spot sa = spot(a), sb = spot(b);
    return rect_distance(sa.p, sa.n, sb.p, sb.n);
}


rules5e::Cover Combat::cover_between(Vec2i origin, int origin_n, Vec2i target, int target_n, CombatantId except_a,
                                      CombatantId except_b) const {
    using rules5e::Cover;
    if (origin_n < 1) origin_n = 1;
    if (target_n < 1) target_n = 1;
    const Spot os{origin, origin_n}, ts{target, target_n};
    // Creature tiles that give half cover (living, present, neither except_a nor except_b), sorted for lookup.
    std::vector<std::pair<int, int>> crowd;
    for (const auto& sl : slots_) {
        if (sl.gone || sl.id == except_a || sl.id == except_b) continue;
        const auto* c = rules5e::find(state_, sl.id);
        if (!c || c->dead) continue;
        const Spot s = spot(sl.id);
        for (int y = 0; y < s.n; ++y)
            for (int x = 0; x < s.n; ++x) crowd.push_back({s.p.x + x, s.p.y + y});
    }
    std::sort(crowd.begin(), crowd.end());
    auto inside = [](Spot s, Vec2i t) {
        return t.x >= s.p.x && t.x < s.p.x + s.n && t.y >= s.p.y && t.y < s.p.y + s.n;
    };
    auto wall = [&](Vec2i t) { return !inside(ts, t) && !inside(os, t) && grid_.blocks_sight(t); };
    auto body = [&](Vec2i t) {
        return wall(t) || (!inside(ts, t) && !inside(os, t) &&
                           std::binary_search(crowd.begin(), crowd.end(), std::pair<int, int>{t.x, t.y}));
    };
    const int lines = (ts.n + 1) * (ts.n + 1);
    Cover best = Cover::Total;
    for (int oy = 0; oy <= os.n && best != Cover::None; ++oy)
        for (int ox = 0; ox <= os.n && best != Cover::None; ++ox) {
            const Vec2i o{os.p.x + ox, os.p.y + oy};
            int walls = 0, any = 0;
            for (int ty = 0; ty <= ts.n; ++ty)
                for (int tx = 0; tx <= ts.n; ++tx) {
                    const Vec2i t{ts.p.x + tx, ts.p.y + ty};
                    if (segment_blocked(o, t, wall)) {
                        ++walls;
                        ++any;
                    } else if (segment_blocked(o, t, body)) {
                        ++any;
                    }
                }
            const Cover c = walls == lines                       ? Cover::Total
                            : 4 * walls >= 3 * lines             ? Cover::ThreeQuarters
                            : any > 0                            ? Cover::Half
                                                                 : Cover::None;
            if (uint8_t(c) < uint8_t(best)) best = c;
        }
    return best;
}

rules5e::SightFacts Combat::sight_facts_of(CombatantId viewer, CombatantId subject) const {
    rules5e::SightFacts f;
    if (!rules5e::find(state_, viewer) || !rules5e::find(state_, subject)) {
        f.line_of_sight = false;
        return f;
    }
    const Vec2i vp = pos(viewer), sp = pos(subject);
    const int dist = distance_tiles(viewer, subject);
    f.distance_ft = uint16_t(std::min(dist * kFeetPerTile, 65535));
    f.line_of_sight = line_of_sight(viewer, subject);
    if (const auto* t = rules5e::find(state_, subject))
        f.subject_invisible = t->has(rules5e::Condition::Invisible);
    f.subject_grounded = true;   // no airborne altitude model yet; tremorsense treats everyone as grounded
    f.audible = true;            // Silence deafens via rules (kZoneSilence); hearing is not a light fact

    // LightSample -> TileLight. sim::Light is Dark=0..Bright=2; rules5e::Light is Bright=0..Dark=2.
    auto to_tile = [](LightSample s) {
        rules5e::TileLight t;
        t.light = s.level == Light::Bright   ? rules5e::Light::Bright
                  : s.level == Light::Dim    ? rules5e::Light::Dim
                                             : rules5e::Light::Dark;
        t.magical_darkness = (s.flags & light_flags::kMagicalDark) != 0;
        t.sunlight = (s.flags & light_flags::kSunlight) != 0;
        t.obscured = (s.flags & light_flags::kObscuredHeavy) != 0   ? rules5e::Obscured::Heavy
                     : (s.flags & light_flags::kObscuredLight) != 0 ? rules5e::Obscured::Light
                                                                   : rules5e::Obscured::None;
        return t;
    };
    auto obscured_of = [](LightSample s) {
        return (s.flags & light_flags::kObscuredHeavy) != 0   ? rules5e::Obscured::Heavy
               : (s.flags & light_flags::kObscuredLight) != 0 ? rules5e::Obscured::Light
                                                             : rules5e::Obscured::None;
    };
    // Default without a driver: Bright / clear (legacy "everyone can see" lighting).
    LightSample bright{};
    bright.level = Light::Bright;
    auto sample_at = [&](Vec2i tile) -> LightSample {
        const LightDriver* d = light_.d;
        if (!d) return bright;
        const auto frame = d->latest();
        if (!frame) return bright;
        const int cs = world::kChunkSize;
        const int cx = tile.x >= 0 ? tile.x / cs : -((-tile.x + cs - 1) / cs);
        const int cy = tile.y >= 0 ? tile.y / cs : -((-tile.y + cs - 1) / cs);
        for (const auto& ch : frame->chunks)
            if (ch.coord.x == cx && ch.coord.y == cy) {
                const int lx = tile.x - cx * cs, ly = tile.y - cy * cs;
                return ch.samples[std::size_t(ly * cs + lx)];
            }
        return bright;
    };

    f.viewer = to_tile(sample_at(vp));
    f.subject = to_tile(sample_at(sp));

    // `between`: max zone obscurement on mid-tiles of the centre-to-centre supercover (same walk as Grid::line_of_sight).
    rules5e::Obscured between = rules5e::Obscured::None;
    if (vp != sp) {
        const int dx = vp.x < sp.x ? sp.x - vp.x : vp.x - sp.x, dy = vp.y < sp.y ? sp.y - vp.y : vp.y - sp.y;
        const int sx = sp.x > vp.x ? 1 : -1, sy = sp.y > vp.y ? 1 : -1;
        int x = vp.x, y = vp.y, ix = 0, iy = 0;
        while (ix < dx || iy < dy) {
            const int64_t l = int64_t(1 + 2 * ix) * dy, r = int64_t(1 + 2 * iy) * dx;
            if (l == r) {
                x += sx;
                y += sy;
                ++ix;
                ++iy;
            } else if (l < r) {
                x += sx;
                ++ix;
            } else {
                y += sy;
                ++iy;
            }
            if (x == sp.x && y == sp.y) break;   // endpoint is subject.obscured, not between
            const rules5e::Obscured o = obscured_of(sample_at({x, y}));
            if (uint8_t(o) > uint8_t(between)) between = o;
        }
    }
    f.between = between;
    return f;
}

rules5e::Cover Combat::cover_of(const rules5e::CoverQuery& q) const {
    using rules5e::Cover;
    if (!rules5e::find(state_, q.target) || q.source == q.target) return Cover::None;
    if (!q.has_origin && !rules5e::find(state_, q.source)) return Cover::None;
    const Spot ts = spot(q.target);
    const Spot os{q.has_origin ? Vec2i{q.origin_x, q.origin_y} : pos(q.source),
                  q.has_origin ? 1 : footprint_size(q.source)};
    return cover_between(os.p, os.n, ts.p, ts.n, q.source, q.target);
}

bool Combat::los_spots(Spot a, Spot b) const {
    if (a.n == 1 && b.n == 1) return grid_.line_of_sight(a.p, b.p);   // v0 test
    for (int ay = 0; ay < a.n; ++ay)
        for (int ax = 0; ax < a.n; ++ax)
            for (int by = 0; by < b.n; ++by)
                for (int bx = 0; bx < b.n; ++bx)
                    if (grid_.line_of_sight({a.p.x + ax, a.p.y + ay}, {b.p.x + bx, b.p.y + by})) return true;
    return false;
}

bool Combat::ally_near(CombatantId attacker, Spot target) const {
    const auto* me = rules5e::find(state_, attacker);
    if (!me) return false;
    for (const auto& sl : slots_) {
        const auto* c = rules5e::find(state_, sl.id);
        if (sl.id == attacker || !c || c->dead || c->team != me->team || !rules5e::is_conscious(*c) ||
            rules5e::is_incapacitated(*c))
            continue;
        const Spot as = spot(sl.id);
        if (rect_distance(as.p, as.n, target.p, target.n) <= 1) return true;
    }
    return false;
}

bool Combat::ally_near_target(CombatantId attacker, CombatantId target) const {
    return ally_near(attacker, spot(target));
}

bool Combat::line_of_sight(CombatantId a, CombatantId b) const { return los_spots(spot(a), spot(b)); }

std::vector<CombatantId> Combat::gaze_seen(CombatantId viewer) const {
    std::vector<CombatantId> out;
    const auto* v = rules5e::find(state_, viewer);
    if (!v || v->has(rules5e::Condition::Blinded)) return out;
    for (const auto& g : state_.combatants) {
        if (g.id == viewer || g.dead || g.gazes.empty() || g.has(rules5e::Condition::Invisible)) continue;
        int range_ft = 0;
        for (const auto& gz : g.gazes) range_ft = std::max(range_ft, int(gz.range_ft));
        if (distance_tiles(viewer, g.id) * kFeetPerTile > range_ft || !line_of_sight(viewer, g.id)) continue;
        out.push_back(g.id);
    }
    return out;
}

std::vector<Vec2i> Combat::occupied_except(CombatantId id) const {
    std::vector<Vec2i> out;
    bool mover_swarm = false;
    for (const auto& s : slots_) if (s.id == id) mover_swarm = s.archetype->swarm;
    for (const auto& s : slots_) {
        const auto* c = rules5e::find(state_, s.id);
        if (s.id == id || !c || c->dead) continue;
        // EC swarm (SRD Swarm): a swarm can occupy another creature's space and vice versa, so a swarm mover sees no other
        // creature's tiles and a swarm's tiles block no one. Without swarms this is the old list.
        if (s.archetype->swarm || mover_swarm) continue;
        if (footprint_size(s.id) == 1) { out.push_back(pos(s.id)); continue; }
        for (Vec2i t : footprint_tiles(s.id)) out.push_back(t);
    }
    return out;
}

void Combat::apply_rules_events(const std::vector<rules5e::Event>& evs) {
    for (const auto& e : evs) {
        if (e.kind == rules5e::EventKind::TurnStarted) { ++turns_started_; falls_.clear(); }   // falls(): this turn only
        SimEvent se;
        se.kind = SimEvent::Kind::Rules;
        se.rules = e;
        events_.push_back(se);
    }
    sync_ecs();
}

void Combat::sync_ecs() {
    for (const auto& s : slots_)
        if (const auto* c = rules5e::find(state_, s.id); c && !s.gone)   // R-G12: a gone summon's entity is destroyed
            if (Health* h = reg_->get<Health>(s.entity)) { h->hp = c->hp; h->hp_max = c->hp_max; }
}

int Combat::melee_reach_tiles(CombatantId id) const {
    const auto* c = rules5e::find(state_, id);
    int best = 0;
    if (c)
        for (const auto& at : c->attacks)
            if (!at.ranged) best = std::max(best, reach_tiles(at.reach_or_range_ft));
    return best;
}

bool Combat::leaves_reach(CombatantId mover, Vec2i from, Vec2i to) const {
    // Facts only; rules5e decides eligibility (reaction used, incapacitated, policy, Disengage).
    for (const auto& s : slots_) {
        if (s.id == mover || !hostile(s.id, mover)) continue;
        const auto* c = rules5e::find(state_, s.id);
        if (!c || c->dead) continue;
        const int reach = melee_reach_tiles(s.id);
        if (reach == 0) continue;
        // Nearest-tile distances between the footprints: the mover's before and after the step.
        const Spot rs = spot(s.id), f = spot_at(mover, from), t = spot_at(mover, to);
        if (rect_distance(f.p, f.n, rs.p, rs.n) <= reach && rect_distance(t.p, t.n, rs.p, rs.n) > reach) return true;
    }
    return false;
}

void Combat::install_hooks() {
    // Re-bound before every rules call: Combat is movable, so hooks must not outlive `this`.
    // No decide_reaction: rules5e returns NeedsDecision and sim pauses (settle() is the adapter).
    state_.hooks.decide_reaction = nullptr;
    state_.hooks.poll_order = poll_order_;
    state_.hooks.spells = spells_;
    if (options_.equipment) {   // EQUIPMENT.md 9: only with the option; off = empty hooks = pre-equipment rules
        refresh_interactions(state_);
        state_.hooks.loadout_of = [](const rules5e::EncounterState& st, CombatantId id) -> const rules5e::LoadoutView* {
            const rules5e::Loadout* l = find_loadout(st, id);
            return l ? &l->view : nullptr;
        };
        std::shared_ptr<const ItemDefTable> items = items_;
        state_.hooks.item_def = [items](uint32_t def) -> const rules5e::ItemDef* { return items ? items->find(def) : nullptr; };
        item_events_.clear();
        state_.hooks.ground_item_def = [this](const rules5e::EncounterState& st, uint32_t instance) -> const rules5e::ItemDef* {
            for (const auto& g : st.ground_items)   // the state rules is checking (an AI lookahead copy included)
                if (g.item.instance == instance) return items_ ? items_->find(g.item.def) : nullptr;
            return nullptr;
        };
        state_.hooks.apply_equip = [this](rules5e::EncounterState& st, const rules5e::ActionRequest& req, uint8_t cost,
                                          std::vector<rules5e::Event>&) { apply_equip(st, req, cost); };
    }
    if (spells_) {
        state_.hooks.can_provide_components = [this](const rules5e::EncounterState&, const rules5e::Combatant& caster,
                                                     const rules5e::SpellDef& d) {
            return d.material_cost_cp == 0 || material_entry(caster.id, d) >= 0;
        };
        state_.hooks.spell_target_ok = [this](const rules5e::EncounterState&, const rules5e::Combatant& caster,
                                              const rules5e::SpellDef& d, const rules5e::Combatant& t) {
            return spell_in_reach(d, caster.id, t.id);
        };
        state_.hooks.targets_within = [this](const rules5e::EncounterState&, const std::vector<CombatantId>& ids, int feet) {
            for (std::size_t i = 0; i < ids.size(); ++i)
                for (std::size_t j = i + 1; j < ids.size(); ++j)
                    if (distance_tiles(ids[i], ids[j]) * kFeetPerTile > feet) return false;
            return true;
        };
        // Row 9b (area_save riders: Death Burst): the template centred on the source (its tile is used even when it is
        // dead). Sphere / cylinder around the source need no aim; cone / line / cube / point-origin templates aim at the
        // nearest other living creature (ties: lowest id), and catch nobody when there is none.
        state_.hooks.area_targets = [this](const rules5e::EncounterState& st, const rules5e::Combatant& source,
                                           const rules5e::SpellDef& d) {
            Vec2i aim = pos(source.id);
            const bool centred = d.range_ft == 0 && (d.area == AreaShape::Sphere || d.area == AreaShape::Cylinder);
            if (!centred) {
                const rules5e::Combatant* best = nullptr;
                int best_d = 0;
                for (const auto& o : st.combatants) {
                    if (o.id == source.id || o.dead) continue;
                    const int dist = distance_tiles(source.id, o.id);
                    if (!best || dist < best_d) { best = &o; best_d = dist; }
                }
                if (!best) return std::vector<CombatantId>{};
                aim = pos(best->id);
            }
            return area_targets(source.id, d, aim);
        };
        state_.hooks.can_see = [this](const rules5e::EncounterState&, const rules5e::Combatant& observer,
                                      const rules5e::Combatant& target) {
            return !observer.has(rules5e::Condition::Blinded) && line_of_sight(observer.id, target.id);
        };
    }
    // Row 13: what UseFeature spends / grants (usage.pool + cost, grant_action, heal_pool).
    // R-G9 (FORCED_MOVEMENT.md §3): forced moves and teleports resolve on sim's grid and commit at once. The shove push keeps
    // EC #104's placement in complete() (the hook only reports its 5 ft), and rules flags it (shove_forced_flag).
    // 7.2t cover: pure geometry over the current positions (replay recomputes it from the replayed board).
    state_.hooks.cover = [this](const rules5e::EncounterState&, const rules5e::CoverQuery& q) { return cover_of(q); };
    // 7.2x vision: per-pair SightFacts from light samples + LOS + zones + invisibility (derive_sight in rules).
    state_.hooks.sight_facts = [this](const rules5e::EncounterState&, const rules5e::Combatant& viewer,
                                      const rules5e::Combatant& subject) {
        rules5e::SightFacts f = sight_facts_of(viewer.id, subject.id);
        // Prefer the combatant rules passed (start-of-step during a step) for Invisible; outlined is cleared in rules.
        f.subject_invisible = subject.has(rules5e::Condition::Invisible);
        return f;
    };
    state_.hooks.forced_move = [this](const rules5e::EncounterState&, const rules5e::ForcedMoveQuery& q) { return resolve_forced(q); };
    state_.hooks.forced_move_order = [this](const rules5e::EncounterState&, std::span<const rules5e::ForcedMoveQuery> qs) {
        std::vector<std::size_t> idx(qs.size());
        for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::vector<int> dist(qs.size());
        for (std::size_t i = 0; i < qs.size(); ++i) dist[i] = distance_tiles(qs[i].source, qs[i].mover);
        std::stable_sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y) {
            if (dist[x] != dist[y]) return dist[x] > dist[y];   // farthest from the origin first
            return qs[x].mover < qs[y].mover;                   // ties: lower id
        });
        return idx;
    };
    // R-G11: zone membership from sim's positions (Appear / StartTurn / EndTurn rows, completely_within rows).
    state_.hooks.zone_contains = [this](const rules5e::EncounterState& st, uint32_t zone_id, CombatantId id, bool completely) {
        const rules5e::ActiveZone* z = rules5e::find_zone(st, zone_id);
        return z && rules5e::find(st, id) && zone_holds(*z, id, spot(id), completely);
    };
    // R-G12 summons (7.2r, §3.3 / D1): the creature of option `idx` of a Summon spell (content only, pure), and the CR-band
    // count table. A spell without sim summon options summons nothing.
    state_.hooks.build_summon = [this](const rules5e::EncounterState&, const rules5e::Combatant&, const rules5e::SpellDef& def, uint8_t,
                                       uint8_t idx) -> std::optional<rules5e::Combatant> {
        const SummonOptions* o = content_ ? content_->summon_options(def.id) : nullptr;
        if (!o || idx >= o->archetypes.size()) return std::nullopt;
        const Archetype* a = content_->find(o->archetypes[idx]);
        if (!a) return std::nullopt;
        return a->base;
    };
    state_.hooks.summon_choice_ok = [this](const rules5e::EncounterState&, const rules5e::Combatant&, const rules5e::SpellDef& def,
                                           uint8_t slot_level, const std::vector<uint8_t>& ar) {
        const SummonOptions* o = content_ ? content_->summon_options(def.id) : nullptr;
        if (!o) return false;
        for (uint8_t k : ar) if (k >= o->archetypes.size() || !content_->find(o->archetypes[k])) return false;
        if (o->table == SummonOptions::Table::Any) return true;
        const std::size_t mult = slot_level >= 9 ? 4 : slot_level >= 7 ? 3 : slot_level >= 5 ? 2 : 1;
        uint32_t cap = 0;   // CR cap in eighths for the count: 1 -> CR 2, 2 -> CR 1, 4 -> CR 1/2, 8 -> CR 1/4 (times mult)
        if (ar.size() == mult) cap = 16;
        else if (ar.size() == 2 * mult) cap = 8;
        else if (ar.size() == 4 * mult) cap = 4;
        else if (ar.size() == 8 * mult) cap = 2;
        else return false;
        for (uint8_t k : ar) if (content_->find(o->archetypes[k])->cr_x8 < 0 || uint32_t(content_->find(o->archetypes[k])->cr_x8) > cap) return false;
        return true;
    };
    state_.hooks.shove_forced_flag = true;   // rules flags the shove push (c = 1); sim no longer flags its copy (same events)
    // R-G12 (7.2q): transform forms. Live play and replay install the same closure; it reads content, its arguments and the
    // (replayed) board only.
    state_.hooks.build_form = [this](const rules5e::EncounterState&, const rules5e::Combatant& who, rules5e::FormKind kind,
                                     uint32_t source, uint8_t index) { return build_form(who, kind, source, index); };
    state_.hooks.feature_spec = [this](const rules5e::EncounterState&, const rules5e::Combatant& actor, uint32_t feature_id)
        -> std::optional<rules5e::FeatureSpec> {
        if (const Archetype* arch = archetype(actor.id))
            if (auto it = arch->feature_specs.find(feature_id); it != arch->feature_specs.end()) return it->second;
        return std::nullopt;
    };
    // Row 12: apply_modifier features hand their compiled rows to rules when used (UseFeature).
    state_.hooks.feature_modifiers = [this](const rules5e::EncounterState&, const rules5e::Combatant& actor, uint32_t feature_id) {
        std::vector<rules5e::ModifierSpec> rows;
        if (const Archetype* arch = archetype(actor.id))
            if (auto it = arch->feature_modifiers.find(feature_id); it != arch->feature_modifiers.end()) rows = it->second;
        return rows;
    };
    state_.hooks.in_range = [this](const rules5e::EncounterState&, const rules5e::ReactionWindow& w,
                                   const rules5e::Combatant& r) {
        if (w.trigger == ReactionTrigger::BeingHit) return r.id == w.target;   // Shield: the creature hit
        if (w.trigger == ReactionTrigger::Damaged) return r.id == w.target;       // reduce_damage / retaliate / riposte: the creature hurt
        if (w.trigger == ReactionTrigger::AllyAttacked)                          // Protection: within 5 ft of the ally attacked
            return r.id != w.target && distance_tiles(r.id, w.target) * kFeetPerTile <= 5;   // footprint-aware nearest-tile distance
        if (w.trigger == ReactionTrigger::Damaged) return r.id == w.target;       // reduce_damage / retaliate / riposte: the creature hurt
        if (w.trigger == ReactionTrigger::AllyAttacked)                          // Protection: within 5 ft of the ally attacked
            return r.id != w.target && distance_tiles(r.id, w.target) * kFeetPerTile <= 5;   // footprint-aware nearest-tile distance
        if (w.trigger == ReactionTrigger::AttackRolled)   // R-G7 Cutting Words: within 60 ft of the attacker and able to see it
            return r.id != w.trigger_actor && distance_tiles(r.id, w.trigger_actor) * kFeetPerTile <= 60 &&
                   !r.has(rules5e::Condition::Blinded) && line_of_sight(r.id, w.trigger_actor);   // footprint-aware nearest-tile
        if (w.trigger == ReactionTrigger::SpellCast) {   // Counterspell: within the counter spell's range, in sight
            if (!spells_) return false;
            for (uint32_t id : r.spells_known) {
                const rules5e::SpellDef* d = spells_->find(id);
                if (d && d->kind == rules5e::SpellEffectKind::Counter &&
                    distance_tiles(r.id, w.trigger_actor) * kFeetPerTile <= int(d->range_ft) &&
                    line_of_sight(r.id, w.trigger_actor))
                    return true;
            }
            return false;
        }
        if (w.trigger != ReactionTrigger::LeavingReach) return false;
        const auto to = moving_to(w.trigger_actor);
        if (!to) return false;
        const int reach = melee_reach_tiles(r.id);
        if (reach == 0) return false;
        const Spot rs = spot(r.id), f = spot(w.trigger_actor), t = spot_at(w.trigger_actor, *to);
        return rect_distance(f.p, f.n, rs.p, rs.n) <= reach && rect_distance(t.p, t.n, rs.p, rs.n) > reach;
    };
    // A readied ranged attack's facts are taken when it fires, not when it was readied.
    state_.hooks.refresh_ready = [this](const rules5e::EncounterState&, const rules5e::ReadiedAction& ra,
                                        const rules5e::Event& e) -> std::optional<ActionRequest> {
        if (ra.response.kind == ActionKind::Move) {   // R-G11: the readied Move's zone membership, taken when it fires
            if (state_.zones.empty()) return std::nullopt;
            const auto it = readied_dest_.find(ra.actor);
            if (it == readied_dest_.end()) return std::nullopt;
            ActionRequest q = ra.response;
            const Spot from = spot(ra.actor), to = spot_at(ra.actor, it->second);
            zone_lists(ra.actor, from, to, q);
            q.zone_changes = zone_changes_for(ra.actor, from, to);
            return q;
        }
        if (ra.response.kind != ActionKind::Attack) return std::nullopt;
        const auto* a = rules5e::find(state_, ra.actor);
        if (!a || ra.response.attack_index >= a->attacks.size()) return std::nullopt;
        const Spot rp = spot(ra.actor);
        const Spot tp = ra.response.target == e.actor ? spot_at(e.actor, moving_to(e.actor).value_or(pos(e.actor)))
                                                      : spot(ra.response.target);
        ActionRequest q = ra.response;
        q.ally_near_target = ally_near(ra.actor, tp);   // facts are taken when the readied attack fires
        if (!a->attacks[ra.response.attack_index].ranged) return q;   // melee: only the ally fact is refreshed
        if (!ranged_attack_facts(ra.actor, ra.response.attack_index, rp, tp, q, nullptr)) return std::nullopt;
        q.target_within_5ft = rect_distance(rp.p, rp.n, tp.p, tp.n) <= 1;
        return q;
    };
    state_.hooks.ready_trigger = [this](const rules5e::EncounterState&, const rules5e::ReadiedAction& ra,
                                        const rules5e::Event& e) {
        if (e.kind != rules5e::EventKind::Moved || e.actor == ra.actor) return false;
        const auto kind = ReadyTriggerKind(ra.trigger_id >> 8);
        const int tiles = int(ra.trigger_id & 0xFF);
        const auto* mover = rules5e::find(state_, e.actor);
        if (!mover || mover->dead) return false;
        if (kind == ReadyTriggerKind::TargetWithin && e.actor != ra.response.target) return false;
        if (kind == ReadyTriggerKind::HostileWithin && !hostile(e.actor, ra.actor)) return false;
        const Vec2i mp = moving_to(e.actor).value_or(pos(e.actor));
        const Spot ms = spot_at(e.actor, mp), rs = spot(ra.actor);
        if (rect_distance(ms.p, ms.n, rs.p, rs.n) > tiles) return false;
        if (ra.response.kind == ActionKind::Attack) {
            const auto* a = rules5e::find(state_, ra.actor);
            const auto* t = rules5e::find(state_, ra.response.target);
            if (!a || !t || t->dead || ra.response.attack_index >= a->attacks.size()) return false;
            const Spot ts = ra.response.target == e.actor ? ms : spot(ra.response.target);
            const int td = rect_distance(rs.p, rs.n, ts.p, ts.n);
            if (a->attacks[ra.response.attack_index].ranged) {
                const Archetype* arch = attack_archetype(ra.actor);
                const int lt = arch ? reach_tiles(arch->long_range_ft[ra.response.attack_index]) : 0;
                return td <= lt && los_spots(rs, ts);
            }
            return td <= reach_tiles(a->attacks[ra.response.attack_index].reach_or_range_ft);
        }
        if (ra.response.kind == ActionKind::Move) {
            auto it = readied_dest_.find(ra.actor);
            if (it == readied_dest_.end()) return false;
            if (it->second == mp) return false;
            const int fp = footprint_size(ra.actor);
            for (const auto& sl : slots_) {
                const auto* c = rules5e::find(state_, sl.id);
                if (sl.id == ra.actor || sl.id == e.actor || !c || c->dead) continue;
                const Spot o = spot(sl.id);
                if (rect_distance(it->second, fp, o.p, o.n) == 0) return false;   // 1x1 vs 1x1: same tile
            }
            return true;
        }
        return false;
    };
}

bool Combat::move_tile(CombatantId mover, Vec2i to, const std::vector<rules5e::Event>& evs) {
    for (const auto& e : evs)
        if (e.kind == rules5e::EventKind::MoveInterrupted && e.actor == mover) return false;
    if (TilePos* p = reg_->get<TilePos>(entity(mover))) { p->x = to.x; p->y = to.y; }
    SimEvent se;
    se.kind = SimEvent::Kind::TileMoved;
    se.actor = mover;
    se.tile = to;
    events_.push_back(se);
    return true;
}

static const ActiveFeature* find_active(const Archetype* a, uint32_t fid) {
    if (!a) return nullptr;
    for (const auto& f : a->active)
        if (f.feature_id == fid) return &f;
    return nullptr;
}

uint8_t Combat::feature_uses_left(CombatantId id, uint32_t fid) const {
    const ActiveFeature* f = find_active(archetype(id), fid);
    if (!f) return 0;
    return rules5e::feature_uses_left(state_, id, fid);
}

// The hook table is a derived binding to `this` (not hashed, not logged; submit re-installs it before every rules call), so
// re-binding it from a const query changes nothing observable. It must be bound here: before the first submit it is empty, and
// after a move of the Combat it points at the old object.
std::vector<CombatantId> Combat::ward_alternates(CombatantId actor, CombatantId warded, uint32_t spell_id, uint8_t attack_index) const {
    const_cast<Combat*>(this)->install_hooks();
    return rules5e::ward_alternate_targets(state_, actor, warded, spell_id, attack_index);
}
rules5e::HealSplitCheck Combat::heal_split_check(CombatantId user, const rules5e::FeatureSpec& spec,
                                                 const std::vector<rules5e::HealShare>& split) const {
    const auto* u = rules5e::find(state_, user);
    if (!u) return {0, rules5e::HealSplitReason::BadTarget};
    const_cast<Combat*>(this)->install_hooks();
    return rules5e::check_heal_split(state_, *u, spec, split);
}
bool Combat::can_use_feature(CombatantId id, uint32_t fid) const {
    const ActiveFeature* f = find_active(archetype(id), fid);
    if (!f || state_.ended || state_.pending || rules5e::current_actor(state_) != id) return false;
    if (feature_uses_left(id, fid) == 0) return false;
    if (const Archetype* ar = archetype(id)) {   // R-G12 wild_shape: its uses are a resources[] pool (form features only)
        const auto sp = ar->feature_specs.find(fid);
        if (sp != ar->feature_specs.end() && sp->second.form != 0 && sp->second.pool != 0) {
            const rules5e::ResourcePool* p = rules5e::find_pool(state_, id, sp->second.pool);
            if (!p || p->cur < sp->second.pool_cost) return false;
        }
    }
    if (const auto* k = rules5e::find(state_, id))   // R-G12 (D7): a feature the form cannot physically use
        if (std::find(k->form_blocked_features.begin(), k->form_blocked_features.end(), fid) != k->form_blocked_features.end()) return false;
    if (f->cost == FeatureCost::Free) return true;
    return f->cost == FeatureCost::BonusAction ? state_.budget.bonus_action : state_.budget.action || state_.budget.extra_actions > 0;
}

std::optional<Vec2i> Combat::moving_to(CombatantId id) const {
    if (step_to_ && id == rules5e::current_actor(state_)) return step_to_;
    // A readied Move resolving inside another creature's request.
    if (id != rules5e::current_actor(state_)) return readied_destination(id);
    return std::nullopt;
}

// Applies a completed request: rules events, then sim events (tiles) in the order of their
// rules Moved events, then sim bookkeeping (readied destinations).
const std::vector<EquipmentEntry>& Combat::equipment(CombatantId id) const {
    static const std::vector<EquipmentEntry> kNone;
    auto it = equipment_.find(id);
    return it == equipment_.end() ? kNone : it->second;
}

// CONTENT_CONVENTIONS §7 rule 2: the first entry (array order) whose id equals components.m.item exactly and whose
// value_gp * 100 >= cost_cp. No item key: nothing provides it (rule 3).
int Combat::material_entry(CombatantId caster, const rules5e::SpellDef& d) const {
    if (d.material_item.empty()) return -1;
    const auto& eq = equipment(caster);
    for (std::size_t i = 0; i < eq.size(); ++i)
        if (eq[i].id == d.material_item && uint64_t(eq[i].value_gp) * 100u >= d.material_cost_cp) return int(i);
    return -1;
}

void Combat::complete(const rules5e::LoggedStep& ls, const std::vector<rules5e::Event>& evs, std::optional<Vec2i> to,
                      std::optional<std::size_t> teardown_begin) {
    const auto& q = ls.request;
    // R-G12 (SUMMONS_TRANSFORMS.md §3.5, v10 gate): in a v10 stream the step that ends the encounter places its sim events before
    // the teardown block and EncounterEnded: rules events [0, split) now, the rest after the sim events. Every older stream
    // (no R-G12 construct) keeps split = size, today's order byte for byte.
    std::size_t split = evs.size();
    if (!evs.empty() && evs.back().kind == rules5e::EventKind::EncounterEnded) {
        bool v10 = event_stream_version(events_) >= 10;
        for (const auto& e : evs) v10 = v10 || uint8_t(e.kind) > uint8_t(kLastRulesEventKindV9);
        if (v10) split = teardown_begin ? std::min(*teardown_begin, evs.size() - 1) : evs.size() - 1;
    }
    pending_removed_.clear();
    struct ClearPlaced { std::vector<std::pair<CombatantId, Vec2i>>& v; ~ClearPlaced() { v.clear(); } } clear_placed{forced_placed_};
    // Priced material consumed by a completed cast (rule 4): the entry that satisfied it, one per cast. Rules checked the
    // components for this cast unless it was a sustain or an innate spell not cast as listed; replay runs the same code.
    if (q.kind == ActionKind::CastSpell && !q.sustain && spells_) {
        const rules5e::SpellDef* def = spells_->find(q.spell_id);
        bool checked = def && def->material_cost_cp > 0 && def->material_consumed;
        if (checked && q.innate)
            if (const auto* caster = rules5e::find(state_, q.actor))
                for (const auto& is : caster->innate)
                    if (is.spell_id == q.spell_id) checked = is.as_listed;
        const int idx = checked ? material_entry(q.actor, *def) : -1;
        if (idx >= 0) {
            auto& eq = equipment_[q.actor];
            materials_used_.push_back({q.actor, q.spell_id, uint32_t(idx), eq[std::size_t(idx)].value_gp});
            eq.erase(eq.begin() + idx);
        }
    }
    // v8: the 5 ft shove's Moved{actor = target, target = shover} is forced movement; sim flags it (rules emit c = 0).
    auto is_shove_push = [&q](const rules5e::Event& e) {
        return e.kind == rules5e::EventKind::Moved && q.kind == ActionKind::Attack && q.special == 3 && e.actor == q.target &&
               e.target == q.actor;
    };
    // the shove push arrives flagged c = 1 from rules (EncounterHooks::shove_forced_flag). R-G12: a Summoned spawns its slot and
    // TokenSpawned right after it; a SummonEnded marks the slot gone (TokenRemoved after the step's sim events, see below).
    for (std::size_t i = 0; i < split; ++i) push_rules_event(evs[i], &ls);
    sync_ecs();
    // R-G8 helpers: put a creature on a tile (forced movement / drag), and end grapples whose holder and held creature are no
    // longer within 5 ft after an effect moved one of them (Grappled is removed from the target; the next step prunes the entry).
    auto place = [this](CombatantId id, Vec2i t) {
        if (TilePos* p = reg_->get<TilePos>(entity(id))) { p->x = t.x; p->y = t.y; }
        SimEvent se;
        se.kind = SimEvent::Kind::TileMoved;
        se.actor = id;
        se.tile = t;
        events_.push_back(se);
    };
    auto free_for = [this](CombatantId id, Vec2i t) {
        const int fp = footprint_size(id);
        if (!grid_.footprint_walkable(t, fp)) return false;
        const auto occ = occupied_except(id);
        for (int dy = 0; dy < fp; ++dy)
            for (int dx = 0; dx < fp; ++dx)
                if (std::find(occ.begin(), occ.end(), Vec2i{t.x + dx, t.y + dy}) != occ.end()) return false;
        return true;
    };
    auto release_out_of_reach = [this](CombatantId moved) {
        std::vector<CombatantId> released;
        for (const auto& sc : state_.sourced) {
            if (sc.condition != rules5e::Condition::Grappled || (sc.target != moved && sc.source != moved)) continue;
            if (distance_tiles(sc.source, sc.target) <= 1) continue;
            bool held_elsewhere = false;   // another holder still within 5 ft keeps the condition
            for (const auto& o : state_.sourced)
                held_elsewhere = held_elsewhere || (o.condition == rules5e::Condition::Grappled && o.target == sc.target &&
                                                    o.source != sc.source && distance_tiles(o.source, o.target) <= 1);
            if (!held_elsewhere && std::find(released.begin(), released.end(), sc.target) == released.end()) released.push_back(sc.target);
        }
        for (CombatantId id : released) {
            rules5e::Combatant* held = nullptr;
            for (auto& k : state_.combatants) if (k.id == id) held = &k;
            if (!held || !(held->conditions & rules5e::bit(rules5e::Condition::Grappled))) continue;
            held->conditions &= ~rules5e::bit(rules5e::Condition::Grappled);
            SimEvent se;
            se.kind = SimEvent::Kind::Rules;
            se.rules = {rules5e::EventKind::ConditionRemoved, id, 0, int32_t(rules5e::Condition::Grappled), 0, 0, 0};
            events_.push_back(se);
        }
    };
    std::vector<CombatantId> fired;
    std::size_t placed_i = 0;   // R-G9: hook commits, in the order of their Moved events
    for (const auto& e : evs) {
        if (e.kind == rules5e::EventKind::Moved && (e.c & (kMovedForced | kMovedTeleport)) && !is_shove_push(e)) {
            while (placed_i < forced_placed_.size() && forced_placed_[placed_i].first != e.actor) ++placed_i;
            if (placed_i < forced_placed_.size()) {
                if (e.a > 0) {   // already committed by the hook: the token's TileMoved
                    SimEvent se;
                    se.kind = SimEvent::Kind::TileMoved;
                    se.actor = e.actor;
                    se.tile = forced_placed_[placed_i].second;
                    events_.push_back(se);
                }
                ++placed_i;
            }
            continue;
        }
        // R-G8 shove 5 ft away: Moved{actor = target, target = shover}; push one tile straight away from the shover if it is free.
        if (is_shove_push(e)) {
            const Vec2i sp = pos(q.actor), tp = pos(e.actor);
            const Vec2i d{(tp.x > sp.x) - (tp.x < sp.x), (tp.y > sp.y) - (tp.y < sp.y)};
            const Vec2i dest{tp.x + d.x, tp.y + d.y};
            if ((d.x != 0 || d.y != 0) && free_for(e.actor, dest)) {
                place(e.actor, dest);
                release_out_of_reach(e.actor);
            }
            continue;
        }
        if (e.kind == rules5e::EventKind::Moved && e.reaction_depth == 0 && q.kind == ActionKind::Move && e.actor == q.actor && to) {
            const Vec2i from = pos(q.actor);
            if (q.squeeze != 0 && !std::any_of(evs.begin(), evs.end(), [&](const rules5e::Event& x) {
                    return x.kind == rules5e::EventKind::MoveInterrupted && x.actor == q.actor; })) {
                SimEvent fe;   // SRD Squeezing: the footprint shrinks / grows back (side a, at the new anchor), before its TileMoved
                fe.kind = SimEvent::Kind::Footprint;
                fe.actor = q.actor;
                fe.tile = *to;
                fe.rules.a = footprint_size(q.actor);
                events_.push_back(fe);
            }
            // altitude PR2: the board before the step (step_altitude derives the new alt_ft from it; replay runs the same code).
            const int g0 = grid_.footprint_ground_ft(from, footprint_size(q.actor)), a0 = abs_height_ft(q.actor);
            const bool air0 = airborne(q.actor);
            if (q.alt_to != 0) {   // vertical move in place: no TileMoved
                if (!std::any_of(evs.begin(), evs.end(), [&](const rules5e::Event& x) {
                        return x.kind == rules5e::EventKind::MoveInterrupted && x.actor == q.actor; }))
                    step_altitude(q, g0, a0, air0);
                continue;
            }
            const bool stepped = move_tile(q.actor, *to, evs);
            if (stepped) step_altitude(q, g0, a0, air0);
            if (stepped)   // drag: creatures the mover holds come along (same step, else into the vacated tile)
                for (CombatantId held : rules5e::grappled_by(state_, q.actor)) {
                    const Vec2i hp = pos(held);
                    const Vec2i same{hp.x + to->x - from.x, hp.y + to->y - from.y};
                    std::optional<Vec2i> drag_to;
                    if (same != hp && free_for(held, same)) drag_to = same;
                    else if (distance_tiles(q.actor, held) > 1 && free_for(held, from)) drag_to = from;
                    if (drag_to) {   // v8: a Moved{actor = held, target = grappler, a = feet, c = drag} precedes its TileMoved
                        SimEvent dm;
                        dm.kind = SimEvent::Kind::Rules;
                        const int32_t ft = 5 * std::max(std::abs(drag_to->x - hp.x), std::abs(drag_to->y - hp.y));
                        dm.rules = {rules5e::EventKind::Moved, held, q.actor, ft, 0, kMovedDrag, 0};
                        events_.push_back(dm);
                        place(held, *drag_to);
                    }
                    release_out_of_reach(held);
                }
            continue;
        }
        if (e.kind == rules5e::EventKind::Moved) {
            if (e.reaction_depth > 0 && std::find(fired.begin(), fired.end(), e.actor) != fired.end())
                if (auto d = readied_destination(e.actor)) {
                    if (TilePos* p = reg_->get<TilePos>(entity(e.actor))) { p->x = d->x; p->y = d->y; }
                    SimEvent se;
                    se.kind = SimEvent::Kind::TileMoved;
                    se.actor = e.actor;
                    se.tile = *d;
                    events_.push_back(se);
                }
        }
        if (e.kind == rules5e::EventKind::ReadyTriggered) fired.push_back(e.actor);
    }
    // R-G8 monster grapple riders (on_hit kind "grapple"): a hit by the actor's own Attack with that attack grapples the target.
    if (q.kind == ActionKind::Attack && q.special == 0)
        if (const Archetype* arch = attack_archetype(q.actor))
            if (auto g = arch->grapple_on_hit.find(q.attack_index); g != arch->grapple_on_hit.end())
                for (const auto& e : evs)
                    if (e.kind == rules5e::EventKind::AttackResolved && e.reaction_depth == 0 && e.actor == q.actor && (e.c & 1)) {
                        std::vector<rules5e::Event> gev;
                        rules5e::grapple(state_, q.actor, e.target, g->second, q.attack_index, gev);   // false past max_grapples: damage only
                        apply_rules_events(gev);
                        break;
                    }
    if (q.kind == ActionKind::CastSpell) {   // v5: where each affected creature stood
        bool cast = false;
        // A depth-0 SpellCast, or ActionUsed for a non-spell action (SpellDef::is_spell == false; no SpellCast).
        for (const auto& e : evs)
            cast = cast || ((e.kind == rules5e::EventKind::SpellCast || e.kind == rules5e::EventKind::ActionUsed) &&
                            e.reaction_depth == 0 && e.actor == q.actor);
        if (cast) {
            std::vector<CombatantId> ids = q.targets;
            if (ids.empty()) ids.push_back(q.target);
            for (CombatantId id : ids) {
                SimEvent se;
                se.kind = SimEvent::Kind::SpellTarget;
                se.actor = q.actor;
                se.tile = pos(id);
                se.rules.target = id;
                se.rules.a = int32_t(q.spell_id);
                events_.push_back(se);
            }
        }
    }
    // R-G12 (7.2q): footprints of forms. A side change emits a Footprint (the v6 kind, mid-combat in v10 streams) after the
    // step's rules events; a growing revert that does not fit is first moved to the nearest free anchor (TileMoved). The
    // teardown's reverts (index >= teardown_begin) keep theirs aside: they follow their own Reverted (below).
    const bool ended_now = state_.ended && std::any_of(evs.begin(), evs.end(), [](const rules5e::Event& e) {
        return e.kind == rules5e::EventKind::EncounterEnded;
    });
    std::size_t tb = evs.size();
    if (ended_now) {
        for (std::size_t i = 0; i < evs.size(); ++i)
            if (evs[i].kind == rules5e::EventKind::EncounterEnded) { tb = i; break; }
        if (teardown_begin && *teardown_begin < tb) tb = *teardown_begin;
    }
    std::vector<std::vector<SimEvent>> teardown_sim(evs.size() - tb);
    for (std::size_t i = 0; i < evs.size(); ++i) {
        const auto& e = evs[i];
        if (e.kind != rules5e::EventKind::Transformed && e.kind != rules5e::EventKind::Reverted) continue;
        if (i >= tb) {
            form_footprint(e, teardown_sim[i - tb]);
        } else {
            std::vector<SimEvent> fe;
            form_footprint(e, fe);
            events_.insert(events_.end(), fe.begin(), fe.end());
        }
    }
    // R-G12 v10 rule-7 gate (SUMMONS_TRANSFORMS.md §3.5, SIM_EVENTS.md rule 7): in a v10 stream the step's rules events from
    // `split` on (= tb: the teardown block, then EncounterEnded) are pushed at the end of this step, behind its sim events, each
    // teardown Reverted directly followed by its own form sim events and each teardown SummonEnded by its TokenRemoved (below).
    // Pre-v10 streams have split = size and keep today's order byte for byte (the teardown form events follow the step's).
    if (split == evs.size())
        for (const auto& v : teardown_sim) events_.insert(events_.end(), v.begin(), v.end());
    // Aggressive (rules #111): remember the movement from before the bonus-action Dash; the actor's Moves this turn spend it first.
    {
        const TurnKey now = turn_key();   // R-G12: not (round, turn_index), which a summon joining before the actor shifts
        if (aggressive_ && aggressive_->stamp != now) aggressive_.reset();
        const auto* actor = rules5e::find(state_, q.actor);
        bool dashed = false;
        for (const auto& e : evs) dashed = dashed || (e.kind == rules5e::EventKind::Dashed && e.reaction_depth == 0 && e.actor == q.actor);
        if (q.kind == ActionKind::Dash && q.aggressive && dashed && actor)
            aggressive_ = AggressiveDash{now, q.actor, std::max(0, int(state_.budget.movement_ft) - rules5e::modified_speed(state_, *actor))};   // 7.2u: Dash adds modified_speed
        else if (aggressive_ && q.kind == ActionKind::Move && q.actor == aggressive_->actor)
            aggressive_->free_ft = std::max(0, aggressive_->free_ft - int(q.move_ft));
    }
    for (const auto& e : evs)
        if (e.kind == rules5e::EventKind::ReadyTriggered || e.kind == rules5e::EventKind::ReadyExpired) readied_dest_.erase(e.actor);
    if (q.kind == ActionKind::Ready && q.ready_kind == ActionKind::Move && to) readied_dest_[q.actor] = *to;
    // R-G12: TokenRemoved for the summons that ended in the step's own events (0 HP, concentration), after its sim events;
    // then the teardown block (each SummonEnded b = 5 followed at once by its TokenRemoved, then Reverted) and EncounterEnded.
    for (const auto& e : pending_removed_) events_.push_back(token_removed(e));
    pending_removed_.clear();
    for (std::size_t i = split; i < evs.size(); ++i) {
        push_rules_event(evs[i], &ls);
        if (evs[i].kind == rules5e::EventKind::SummonEnded && !pending_removed_.empty()) {
            events_.push_back(token_removed(pending_removed_.back()));
            pending_removed_.pop_back();
        }
        if (i >= tb) events_.insert(events_.end(), teardown_sim[i - tb].begin(), teardown_sim[i - tb].end());
    }
    for (const auto& e : pending_removed_) events_.push_back(token_removed(e));   // none expected (every teardown one is inline)
    pending_removed_.clear();
    if (split < evs.size()) sync_ecs();
    for (auto& sl : slots_)   // gone summons: the entity goes at the end of the step (never mid-step)
        if (sl.gone && reg_->alive(sl.entity)) { reg_->destroy(sl.entity); sl.entity = ecs::kNullEntity; }
    if (options_.equipment) equipment_after_step(ls, evs);
    step_to_.reset();
    ready_to_.reset();
}

void Combat::push_rules_event(const rules5e::Event& e, const rules5e::LoggedStep* ls) {
    if (e.kind == rules5e::EventKind::TurnStarted) { ++turns_started_; falls_.clear(); }   // falls(): this turn only
    SimEvent se;
    se.kind = SimEvent::Kind::Rules;
    se.rules = e;
    events_.push_back(se);
    if (e.kind == rules5e::EventKind::Summoned && ls) spawn_summon(e, *ls);
    if (e.kind == rules5e::EventKind::SummonEnded)
        for (auto& sl : slots_)
            if (sl.id == e.target && sl.summoned && !sl.gone) {
                sl.last = pos(sl.id);
                sl.gone = true;
                pending_removed_.push_back(e);
            }
}

SimEvent Combat::token_removed(const rules5e::Event& ended) {
    SimEvent se;   // v10: actor = the summon, tile = its last anchor, rules.a = SummonEnded.b (reason)
    se.kind = SimEvent::Kind::TokenRemoved;
    se.actor = ended.target;
    se.tile = pos(ended.target);
    se.rules.a = ended.b;
    return se;
}

void Combat::register_rows(const Slot& sl) {   // the create-time registration loops, for one (summoned) slot
    for (const auto& f : sl.archetype->active)
        if (f.uses > 0)
            rules5e::register_feature(state_, sl.id, f.feature_id, f.uses,
                                      f.recharge == Recharge::ShortRest  ? rules5e::FeatureRecharge::ShortRest
                                      : f.recharge == Recharge::LongRest ? rules5e::FeatureRecharge::LongRest
                                                                         : rules5e::FeatureRecharge::None);
    for (const auto& r : sl.archetype->resources)
        rules5e::register_pool(state_, sl.id, r.pool_id, r.max, r.start,
                               r.recharge == uint8_t(rules5e::FeatureRecharge::ShortRest)  ? rules5e::FeatureRecharge::ShortRest
                               : r.recharge == uint8_t(rules5e::FeatureRecharge::LongRest) ? rules5e::FeatureRecharge::LongRest
                                                                                          : rules5e::FeatureRecharge::None);
    for (const auto& sa : sl.archetype->save_actions) {
        if (sa.usage_kind == 3 && !rules5e::find_pool(state_, sl.id, sa.pool_id))
            rules5e::register_pool(state_, sl.id, sa.pool_id, sa.pool_max, sa.pool_start,
                                   sa.pool_recharge == Recharge::ShortRest  ? rules5e::FeatureRecharge::ShortRest
                                   : sa.pool_recharge == Recharge::LongRest ? rules5e::FeatureRecharge::LongRest
                                                                            : rules5e::FeatureRecharge::None);
        else if (sa.usage_kind == 1) rules5e::register_action_use(state_, sl.id, sa.spell_id, false);
        else if (sa.usage_kind == 2)
            rules5e::register_feature(state_, sl.id, sa.spell_id, sa.uses,
                                      sa.recharge == Recharge::ShortRest ? rules5e::FeatureRecharge::ShortRest
                                                                         : rules5e::FeatureRecharge::LongRest);
    }
    for (const auto& tr : sl.archetype->base.triggers)
        if (tr.recharge_min != 0) rules5e::register_action_use(state_, sl.id, tr.id, false);
}

void Combat::spawn_summon(const rules5e::Event& ev, const rules5e::LoggedStep& ls) {
    const auto& q = ls.request;
    const CombatantId id = ev.target;
    std::size_t k = 0;
    while (k < q.summon_ids.size() && q.summon_ids[k] != id) ++k;
    const SummonOptions* opts = content_ ? content_->summon_options(q.spell_id) : nullptr;
    if (k >= q.summon_ids.size() || k >= q.summon_archetypes.size() || k >= q.summon_tiles.size() || !opts ||
        q.summon_archetypes[k] >= opts->archetypes.size())
        return;   // unreachable: do_rules filled the tiles and build_summon resolved the archetype
    const Archetype* a = content_->find(opts->archetypes[q.summon_archetypes[k]]);
    const auto* c = rules5e::find(state_, id);
    if (!a || !c || reg_->find(id)) return;
    const Vec2i at{q.summon_tiles[k].x, q.summon_tiles[k].y};
    const ecs::Entity e = reg_->create(id);
    reg_->emplace(e, Health{c->hp, c->hp_max});
    reg_->emplace(e, Team{c->team});
    reg_->emplace(e, TilePos{at.x, at.y});
    if (a->footprint > 1) reg_->emplace(e, Footprint{uint8_t(a->footprint)});
    reg_->emplace(e, CombatantLink{id});
    Slot sl{id, e, a};
    sl.summoned = true;
    slots_.push_back(sl);
    if (!a->equipment.empty()) equipment_[id] = a->equipment;
    register_rows(slots_.back());
    SimEvent se;   // v10: right after its own Summoned; actor = id, tile = anchor, rules.a = footprint side, rules.b = summoner
    se.kind = SimEvent::Kind::TokenSpawned;
    se.actor = id;
    se.tile = at;
    se.rules.a = a->footprint;
    se.rules.b = int32_t(ev.actor);
    events_.push_back(se);
}

bool Combat::ranged_attack_facts(CombatantId actor, uint8_t ai, Spot from, Spot tp, ActionRequest& req,
                                 std::string* error) const {
    const Archetype* arch = attack_archetype(actor);
    if (!arch || ai >= arch->long_range_ft.size()) { if (error) *error = "bad attack"; return false; }
    const int d = rect_distance(from.p, from.n, tp.p, tp.n);
    if (d > reach_tiles(arch->long_range_ft[ai])) { if (error) *error = "target beyond long range"; return false; }
    if (!los_spots(from, tp)) { if (error) *error = "no line of sight"; return false; }
    req.beyond_normal_range = d > reach_tiles(arch->normal_range_ft[ai]);
    req.ranged_hostile_within_5ft = false;
    for (const auto& sl : slots_) {
        const auto* c = rules5e::find(state_, sl.id);
        if (sl.id == actor || !c || c->dead || !hostile(sl.id, actor) || !rules5e::is_conscious(*c) ||
            rules5e::is_incapacitated(*c))
            continue;
        const Spot hs = spot(sl.id);
        if (rect_distance(from.p, from.n, hs.p, hs.n) <= 1) req.ranged_hostile_within_5ft = true;
    }
    return true;
}

bool Combat::ranged_disadvantage(CombatantId actor, uint8_t ai, CombatantId target) const {
    ActionRequest q{};
    if (!ranged_attack_facts(actor, ai, spot(actor), spot(target), q, nullptr)) return false;
    return q.beyond_normal_range || q.ranged_hostile_within_5ft;
}

bool Combat::can_shoot(CombatantId actor, uint8_t ai, CombatantId target) const {
    const auto* a = rules5e::find(state_, actor);
    if (!a || ai >= a->attacks.size() || !a->attacks[ai].ranged || !rules5e::find(state_, target)) return false;
    ActionRequest scratch{};
    return ranged_attack_facts(actor, ai, spot(actor), spot(target), scratch, nullptr);
}

SubmitResult Combat::finish(rules5e::StepResult sr) {
    SubmitResult r;
    r.status = sr.status;
    if (sr.status == StepStatus::Rejected) {
        r.error = std::string(sr.error);
        if (!state_.pending) { step_to_.reset(); ready_to_.reset(); derived_area_ = false; }   // nothing paused
        return r;
    }
    r.ok = true;
    if (sr.status == StepStatus::NeedsDecision) {   // events are provisional; state is pre-request
        r.needs = std::move(sr.needs);
        return r;
    }
    const rules5e::LoggedStep& ls = *sr.logged;
    std::optional<Vec2i> to;
    if (ls.request.kind == ActionKind::Move) to = step_to_;
    if (ls.request.kind == ActionKind::Ready) to = ready_to_;
    if (ls.request.kind == ActionKind::CastSpell && derived_area_) to = aim_to_;   // the aim tile (log_hash folds it)
    derived_area_ = false;
    complete(ls, sr.events, to, sr.teardown_begin);
    log_.push_back({ls, to});
    r.reactions = std::move(sr.reactions);
    if (light_.d) light_.d->step(*this);   // R-G13: the committed state's light frame
    return r;
}

void Combat::attach_light_driver(LightDriver* d) {
    light_.d = d;
    if (d) d->step(*this);
}

SubmitResult Combat::submit(const Command& cmd) {
    SubmitResult r;
    if (state_.pending) { r.error = "a reaction window is waiting for a decision"; r.needs = pending(); return r; }
    if (state_.ended) { r.error = "encounter ended"; return r; }
    if (light_.d && light_.d->halted()) {   // R-G13 §7: never step on a partial halo; re-check once (the streamer may have loaded)
        light_.d->step(*this);
        if (light_.d->halted()) { r.error = light_.d->failed().empty() ? "light halo pending" : "light halo failed"; return r; }
    }
    install_hooks();
    return cmd.kind == CommandKind::Step ? do_step(cmd.to, cmd.request.squeeze == 1, cmd.request.alt_to) : do_rules(cmd.request, cmd.to, cmd.aim);
}

SubmitResult Combat::decide(const ReactionDecision& d) {
    SubmitResult r;
    const auto nd = pending();
    if (!nd) { r.error = "no pending reaction window"; return r; }
    if (d.window_seq != nd->window_seq) { r.error = "decision for a different window"; r.needs = nd; return r; }
    install_hooks();
    if (state_.pending) set_step_context(state_.pending->request);
    forced_placed_.clear();
    return finish(rules5e::step(state_, rules5e::decision_request(*nd, d)));
}

SubmitResult Combat::settle(const ReactionDeciderFn& decider) {
    SubmitResult r;
    r.ok = true;
    r.status = StepStatus::Done;
    while (auto nd = pending()) {
        ReactionDecision d = decider ? decider(*this, *nd) : ReactionDecision{nd->window_seq, false, 0};
        d.window_seq = nd->window_seq;
        if (d.react && d.option >= nd->options.size()) d.react = false;
        r = decide(d);
        if (!r.ok) return r;
    }
    return r;
}

SubmitResult Combat::do_rules(ActionRequest req, Vec2i to, bool aim) {
    SubmitResult r;
    derived_area_ = false;
    const CombatantId actor = rules5e::current_actor(state_);
    if (req.actor != actor) { r.error = "not this combatant's turn"; return r; }
    if (req.kind == ActionKind::Move) { r.error = "use Step commands for movement (sim paths the grid)"; return r; }
    // SRD (feat/size-space): a creature can't willingly end its move in another creature's space. The turn may not end
    // while it overlaps one, unless no step out is possible (then it stays: unwilling).
    if (req.kind == ActionKind::EndTurn && overlaps_other(actor, pos(actor), footprint_size(actor))) {
        const Vec2i p = pos(actor);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                StepPlan sp;
                if ((dx || dy) && !plan_step(actor, {p.x + dx, p.y + dy}, sp) &&
                    step_mode(actor, p, {p.x + dx, p.y + dy}, sp.cost, sp.n) < 0xFE) {
                    r.error = "cannot end movement in another creature's space";
                    return r;
                }
            }
    }
    if (req.kind == ActionKind::OpportunityAttack) { r.error = "opportunity attacks come from reaction windows"; return r; }
    if (req.kind == ActionKind::ReactionDecision) { r.error = "use decide() for reaction decisions"; return r; }
    if (req.kind == ActionKind::GazeResponse) {   // Petrifying Gaze (7.2p): gaze_seen is a sim fact, never caller input
        req.gaze_seen = gaze_seen(actor);
        step_to_.reset();
        return finish(rules5e::step(state_, req));
    }
    if (req.kind == ActionKind::UseFeature) {   // parameters come from content, not the caller
        const ActiveFeature* f = find_active(archetype(actor), req.feature_id);
        if (!f) { r.error = "no such active feature"; return r; }
        if (feature_uses_left(actor, req.feature_id) == 0) { r.error = "feature has no uses left"; return r; }
        req.as_bonus_action = f->cost == FeatureCost::BonusAction;   // rules read FeatureSpec::cost when a spec exists
        req.heal_dice = f->heal;   // zero unless the hook set a heal
    }
    if (req.kind == ActionKind::Attack && req.special != 0 && req.special != 6) {   // 6 (attack a web) takes the weapon facts below   // R-G8 grapple / shove / escape: a creature within 5 ft
        if (req.special != 4 || req.target != 0) {
            if (!rules5e::find(state_, req.target)) { r.error = "bad target"; return r; }
            req.target_within_5ft = distance_tiles(actor, req.target) <= 1;
            if (!req.target_within_5ft && req.special != 4) { r.error = "target not within 5 ft"; return r; }
        }
    } else if (req.kind == ActionKind::Attack) {
        const auto* a = rules5e::find(state_, actor);
        if (!a || req.attack_index >= a->attacks.size() || !rules5e::find(state_, req.target)) {
            r.error = "bad attack";
            return r;
        }
        const int d = distance_tiles(actor, req.target);
        if (a->attacks[req.attack_index].ranged) {
            if (!ranged_attack_facts(actor, req.attack_index, spot(actor), spot(req.target), req, &r.error)) return r;
        } else if (d > reach_tiles(a->attacks[req.attack_index].reach_or_range_ft)) {
            r.error = "target out of reach";
            return r;
        }
        req.target_within_5ft = d <= 1;
        req.ally_near_target = ally_near(actor, spot(req.target));
    }
    if (req.kind == ActionKind::CastSpell) {
        if (!spells_ || !spells_->find(req.spell_id)) { r.error = "unknown spell"; return r; }
        // EC spider follow-up: a non-spell ranged attack_effect (Web) gets the ranged attack facts (beyond its normal range,
        // a hostile within 5 ft); spells are unchanged (rules ignores the facts for them).
        if (const auto* adef = spells_->find(req.spell_id); adef->kind == rules5e::SpellEffectKind::SpellAttack && !adef->is_spell &&
                                                           !adef->melee && rules5e::find(state_, req.target)) {
            const Spot from = spot(actor);
            req.beyond_normal_range = distance_tiles(actor, req.target) > reach_tiles(adef->range_ft);
            req.ranged_hostile_within_5ft = false;
            for (const auto& sl : slots_) {
                const auto* c = rules5e::find(state_, sl.id);
                if (sl.id == actor || !c || c->dead || !hostile(sl.id, actor) || !rules5e::is_conscious(*c) || rules5e::is_incapacitated(*c)) continue;
                const Spot hs = spot(sl.id);
                if (rect_distance(from.p, from.n, hs.p, hs.n) <= 1) req.ranged_hostile_within_5ft = true;
            }
        }
        // R-G12 summons (§3.0 / §3.1): ids come from rules' counter; sim refuses an exhausted id space or ids another system
        // already took in the registry, then validates caller tiles or fills them deterministically (logged, 0xBC).
        if (const auto* sdef = spells_->find(req.spell_id); sdef->kind == rules5e::SpellEffectKind::Summon && !req.summon_archetypes.empty()) {
            if (summon_ids_exhausted_) { r.error = "summon id space exhausted"; return r; }
            CombatantId top = 0;
            for (const auto& k : state_.combatants) top = std::max(top, k.id);
            const uint64_t base = std::max<uint64_t>(state_.next_summon_id, uint64_t(top) + 1);
            if (base + req.summon_archetypes.size() - 1 > UINT32_MAX) { r.error = "summon id space exhausted"; return r; }
            for (std::size_t k = 0; k < req.summon_archetypes.size(); ++k)
                if (reg_->find(CombatantId(base + k))) { r.error = "summon id already taken in the registry"; return r; }
            const SummonOptions* o = content_ ? content_->summon_options(sdef->id) : nullptr;
            bool known = o != nullptr;
            for (uint8_t k : req.summon_archetypes) known = known && k < o->archetypes.size() && content_->find(o->archetypes[k]);
            if (known) {   // otherwise rules refuses (summon not available)
                std::string err;
                const auto tiles = summon_tiles_for(actor, *sdef, req.summon_archetypes, &err);
                if (req.summon_tiles.empty()) {
                    if (tiles.empty()) { r.error = err; return r; }
                    for (Vec2i t : tiles) req.summon_tiles.push_back({t.x, t.y});
                } else if (req.summon_tiles.size() == req.summon_archetypes.size()) {
                    std::vector<Spot> placed;
                    for (std::size_t k = 0; k < req.summon_tiles.size(); ++k) {
                        const Spot sp{{req.summon_tiles[k].x, req.summon_tiles[k].y}, content_->find(o->archetypes[req.summon_archetypes[k]])->footprint};
                        if (!summon_spot_ok(actor, *sdef, sp, placed)) { r.error = "summon_tiles: illegal tile"; return r; }
                        placed.push_back(sp);
                    }
                }
            }
        }
        // R-G9 teleport (Misty Step): the caller's destination, re-validated here; sim fills teleport_feet. The request's
        // teleport_feet is sim's to set (a caller value is ignored), so a logged request always carries the derived distance.
        if (const auto* tdef = spells_->find(req.spell_id); tdef->kind == rules5e::SpellEffectKind::Teleport && !req.sustain) {
            if (!req.has_teleport_dest) { r.error = "teleport: needs a destination (has_teleport_dest)"; return r; }
            const Vec2i dest{req.teleport_x, req.teleport_y};
            if (!teleport_dest_ok(actor, *tdef, dest)) { r.error = "teleport: illegal destination"; return r; }
            const Vec2i from = pos(actor);
            req.teleport_feet = uint16_t(kFeetPerTile * std::max(std::abs(dest.x - from.x), std::abs(dest.y - from.y)));
        }
        // R-G11 zones: sim owns the anchor tile (re-validated against legal_zone_tiles), the zone move's distance and the
        // membership changes it causes; membership fields are sim-derived, so caller values are overwritten.
        req.zones_entered.clear(); req.zones_left.clear(); req.zones_within.clear(); req.zone_changes.clear();
        req.zone_move_ft = 0;
        if (const auto* zdef = spells_->find(req.spell_id); zdef->kind == rules5e::SpellEffectKind::Zone) {
            if (req.has_zone_end || req.zone_ring || req.zone_side != 0) { r.error = "zone: walls and rings are not supported by sim yet"; return r; }
            if (req.zone_move != 0) {
                const rules5e::ActiveZone* z = rules5e::find_zone(state_, req.zone_move);
                if (!z || !z->has_anchor_tile) { r.error = "zone_move: no such point zone"; return r; }
                if (req.has_zone_dest) {
                    const Vec2i dest{req.zone_x, req.zone_y};
                    const auto ok = legal_zone_tiles(actor, *zdef, req.zone_move);
                    if (std::find(ok.begin(), ok.end(), dest) == ok.end()) { r.error = "zone_move: illegal destination"; return r; }
                    req.zone_move_ft = uint16_t(kFeetPerTile * std::max(std::abs(dest.x - z->anchor_x), std::abs(dest.y - z->anchor_y)));
                    req.zone_changes = zone_changes_for(actor, spot(actor), spot(actor), req.zone_move, &dest);
                }
            } else if (zdef->zone.anchor == rules5e::ZoneAnchor::Point && !req.sustain) {
                if (!req.has_zone_dest) { r.error = "zone: a point zone needs a destination (has_zone_dest)"; return r; }
                const auto ok = legal_zone_tiles(actor, *zdef);
                if (std::find(ok.begin(), ok.end(), Vec2i{req.zone_x, req.zone_y}) == ok.end()) { r.error = "zone: illegal destination"; return r; }
            }
        }
        if (req.sustain && (aim || !req.targets.empty())) {   // R-G15 Heat Metal again: one creature, the request's target
            r.error = "sustain takes a single target and no aim";
            return r;
        }
        if (aim) {   // area template: derive the targets from the aim tile (opt-in; an explicit list wins)
            const auto* def = spells_->find(req.spell_id);
            if (def->area == rules5e::AreaShape::None || !req.targets.empty()) { r.error = "aim needs an area spell and no explicit targets"; return r; }
            std::string err;
            rules5e::SpellDef ranged = *def;   // R-G14 Distant Spell: the aim may reach the doubled range (rules #109 formula)
            if ((req.metamagic & rules5e::kMetaDistant) && def->range_ft >= 5)
                ranged.range_ft = uint16_t(def->range_ft == 5 ? 30 : std::min<int>(def->range_ft * 2, 65535));
            req.targets = area_targets(actor, ranged, to, &err);
            if (req.targets.empty()) { r.error = err; return r; }
            req.target = req.targets[0];
            derived_area_ = true;
            aim_to_ = to;
            // 7.2t: an aimed point of origin (not self-centred, not a cone / line from the caster) is the cover origin
            if (def->range_ft != 0 && def->area != rules5e::AreaShape::Cone && def->area != rules5e::AreaShape::Line) {
                req.has_area_origin = true; req.area_x = to.x; req.area_y = to.y;
            }
        }
        // R-G8 Charmed: no harmful spell (attack or save) may include the recorded charmer.
        if (const CombatantId charmer = rules5e::condition_source(state_, actor, rules5e::Condition::Charmed)) {
            const auto* def = spells_->find(req.spell_id);
            // R-G15: the HP pool (Sleep), delayed and sustained damage (Acid Arrow, Heat Metal and sustaining it) are harmful too.
            const auto k = def->kind;
            const bool harmful = k == rules5e::SpellEffectKind::SpellAttack || k == rules5e::SpellEffectKind::Save ||
                                 k == rules5e::SpellEffectKind::HpPool || k == rules5e::SpellEffectKind::DelayedDamage ||
                                 k == rules5e::SpellEffectKind::SustainedDamage;
            bool hits = req.target == charmer;
            for (CombatantId id : req.targets) hits = hits || id == charmer;
            if (harmful && hits) { r.error = "charmed: cannot target the charmer with a harmful spell"; derived_area_ = false; return r; }
        }
        const CombatantId first = !req.targets.empty() ? req.targets[0] : req.target;
        if (!rules5e::find(state_, first)) { r.error = "bad target"; return r; }
        // R-G15 Revivify: a dead creature does not block its tiles, so another may stand there now; it cannot come back
        // into an occupied space (rules check death and the time window).
        if (!req.sustain && spells_->find(req.spell_id)->kind == rules5e::SpellEffectKind::Revive) {
            const auto taken = occupied_except(first);
            for (Vec2i t : footprint_tiles(first))
                if (std::find(taken.begin(), taken.end(), t) != taken.end()) { r.error = "revive target's space is occupied"; return r; }
        }
        req.target_within_5ft = distance_tiles(actor, first) <= 1;   // spell attacks: crit vs Paralyzed/Unconscious
    }
    ready_to_.reset();
    if (req.kind == ActionKind::Ready) {
        const auto kind = ReadyTriggerKind(req.trigger_id >> 8);
        const int tiles = int(req.trigger_id & 0xFF);
        if ((kind != ReadyTriggerKind::TargetWithin && kind != ReadyTriggerKind::HostileWithin) || tiles < 1 ||
            (req.trigger_id >> 16) != 0) {
            r.error = "bad ready trigger";
            return r;
        }
        const auto* a = rules5e::find(state_, actor);
        if (req.ready_kind == ActionKind::Attack) {
            const auto* t = rules5e::find(state_, req.target);
            if (!t || req.attack_index >= a->attacks.size() || !hostile(actor, req.target)) { r.error = "bad readied attack"; return r; }
            if (kind == ReadyTriggerKind::HostileWithin) { r.error = "a readied attack needs a TargetWithin trigger"; return r; }
            const auto& at = a->attacks[req.attack_index];
            const Archetype* arch = attack_archetype(actor);
            if (at.ranged) {
                if (tiles > reach_tiles(arch->long_range_ft[req.attack_index])) { r.error = "trigger beyond long range"; return r; }
                req.beyond_normal_range = tiles > reach_tiles(arch->normal_range_ft[req.attack_index]);
                req.ranged_hostile_within_5ft = false;   // re-evaluated when it fires (hooks.refresh_ready)
                req.target_within_5ft = tiles <= 1;
            } else {
                if (tiles > reach_tiles(at.reach_or_range_ft)) { r.error = "trigger beyond reach"; return r; }
                req.target_within_5ft = tiles <= 1;
            }
        } else if (req.ready_kind == ActionKind::Move) {
            const Vec2i from = pos(actor);
            const int fp = footprint_size(actor);
            if (!grid_.footprint_walkable(to, fp) || to == from) { r.error = "bad readied move destination"; return r; }
            const auto path = path_for(actor, from, {to}, fp);
            if (path.empty()) { r.error = "readied move destination unreachable"; return r; }
            int cost = 0;
            Vec2i prev = from;
            for (Vec2i p : path) { cost += step_cost_ft(actor, prev, p); prev = p; }
            if (cost > rules5e::modified_speed(state_, *a)) { r.error = "readied move longer than speed"; return r; }   // 7.2u: effective speed
            req.move_ft = int16_t(cost);
            req.move_tiles = uint8_t(std::min<std::size_t>(path.size(), 255));   // path length (rules counts it only when it fires on the readier's own turn)
            req.leaves_reach = true;   // rules polls; sim's in_range decides who is actually left
            ready_to_ = to;
        } else {
            r.error = "only Attack or Move can be readied";
            return r;
        }
    }
    // EQUIPMENT.md 4.2: PickUp / HandOver reach is the sim's fact (the caller's value is replaced; logged with the request).
    if (req.kind == ActionKind::UseObject && options_.equipment &&
        (req.equip_op == uint8_t(rules5e::EquipOp::PickUp) || req.equip_op == uint8_t(rules5e::EquipOp::HandOver))) {
        req.equip_in_reach = false;
        if (req.equip_op == uint8_t(rules5e::EquipOp::PickUp)) {
            // Not a ground item: reach is moot; rules refuses it as "equip: unknown ground item" (ground_item_def).
            req.equip_in_reach = true;
            for (const auto& g : state_.ground_items)
                if (g.item.instance == req.equip_item) req.equip_in_reach = within_5ft(actor, {g.x, g.y});
        } else if (rules5e::find(state_, req.equip_to) && req.equip_to != actor) {
            const Spot rs = spot(req.equip_to);
            const Spot as = spot(actor);
            req.equip_in_reach = rect_distance(as.p, as.n, rs.p, rs.n) <= 1;
        }
    }
    step_to_.reset();
    set_step_context(req);
    forced_placed_.clear();
    return finish(rules5e::step(state_, req));
}

SubmitResult Combat::do_step(Vec2i to, bool allow_squeeze, int16_t alt_to) {
    SubmitResult r;
    const CombatantId mover = rules5e::current_actor(state_);
    if (state_.budget.gaze_pending) { r.error = "gaze pending: answer with GazeResponse first"; return r; }
    const Vec2i from = pos(mover);
    if (alt_to != 0) {   // altitude PR2: vertical movement in place (5 ft per 5 ft from the shared budget)
        if (to != from) { r.error = "alt_to: a vertical move is a Step to the mover's own tile"; return r; }
        if (alt_to % kFeetPerTile != 0) { r.error = "alt_to: multiple of 5 ft"; return r; }
        const auto* me = rules5e::find(state_, mover);
        if (!me) { r.error = "no mover"; return r; }
        const MoveCaps caps = caps_of(mover);
        const bool prone = me->has(rules5e::Condition::Prone);
        const int cur = me->alt_ft;
        int target = cur + alt_to;
        uint8_t mode = 0;
        int mult = 1;
        if (target > 0 || (target >= 0 && cur > 0)) {   // in the air (or on a wall): flight, else climbing terrain
            if (caps.fly && !prone) mode = 1;
            else if (grid_.climbable(from)) { mode = caps.climb ? 3 : 0; mult = caps.climb ? 1 : 2; }
            else { r.error = prone && caps.fly ? "cannot fly while prone" : "alt_to: needs flight or climbable terrain"; return r; }
        } else if (caps.burrow && !grid_.water(from)) {   // feat/rules-burrow-speed: down into (or up out of) the ground
            if (!grid_.walkable(from)) { r.error = "alt_to: cannot burrow here"; return r; }
            mode = 4;   // altitude PR2: the burrow speed's allowance (move_allowance refuses a mover without one)
        } else {   // below the surface: water only, down to its depth
            const int depth = grid_.water_depth_ft(from);
            if (!grid_.water(from) || depth == 0) { r.error = "alt_to: cannot go below the ground"; return r; }
            target = std::max(target, -depth);
            mode = caps.swim ? 2 : 0;
            mult = caps.swim ? 1 : 2;
        }
        if (target == cur) { r.error = "alt_to: no altitude change"; return r; }
        const int cost = (target > cur ? target - cur : cur - target) * mult;
        const int allow = rules5e::move_allowance(state_, *me, mode);
        if (allow < 0 || cost > allow) { r.error = "not enough movement"; return r; }
        ActionRequest mv{mover, ActionKind::Move};
        mv.move_ft = int16_t(cost);
        mv.move_tiles = 0;
        mv.move_mode = mode;
        mv.alt_to = int16_t(target - cur);
        step_to_ = from;
        set_step_context(mv);
        forced_placed_.clear();
        return finish(rules5e::step(state_, mv));
    }
    StepPlan plan;
    if (auto e = plan_step(mover, to, plan, allow_squeeze)) { r.error = *e; return r; }
    const int fp = plan.n;
    // R-G8 Frightened: no step that ends closer to the recorded source of the fear.
    if (const CombatantId src = rules5e::condition_source(state_, mover, rules5e::Condition::Frightened)) {
        const Spot ss = spot(src);
        if (rect_distance(to, fp, ss.p, ss.n) < rect_distance(from, fp, ss.p, ss.n)) { r.error = "frightened: cannot move closer to the source"; return r; }
    }
    const int cost = plan.cost;   // R-G11 zone overlays, another creature's space, squeezing, terrain (feat/move-modes)
    // feat/move-modes: the speed that fits the terrain (SRD "using different speeds"); the rules check the allowance.
    const uint8_t mode = step_mode(mover, from, to, cost, fp);
    if (mode == 0xFF) { r.error = "not enough movement"; return r; }
    if (mode == 0xFE) { r.error = "cannot fly while prone"; return r; }
    // SRD: a creature can't willingly end its move in another creature's space: a step into one needs movement left (in
    // the step's speed) for another 5 ft step (actions other than Step are refused while it overlaps, see do_rules).
    if (plan.ends_in_other && rules5e::move_allowance(state_, *rules5e::find(state_, mover), mode) - cost < 5) {
        r.error = "cannot end movement in another creature's space";
        return r;
    }
    if (!aggressive_step_ok(mover, to, cost, fp)) { r.error = "aggressive: the Dash's movement must go toward a hostile it can see"; return r; }
    ActionRequest mv{mover, ActionKind::Move};
    mv.move_ft = int16_t(cost);
    mv.move_tiles = 1;
    mv.move_mode = mode;
    mv.leaves_reach = leaves_reach(mover, from, to);
    mv.squeeze = plan.squeeze;
    if (!state_.zones.empty()) {   // R-G11: membership facts, sim-derived (Q2 / Q8)
        zone_lists(mover, spot(mover), Spot{to, fp}, mv);
        mv.zone_changes = zone_changes_for(mover, spot(mover), Spot{to, fp});
    }
    step_to_ = to;
    set_step_context(mv);
    forced_placed_.clear();
    return finish(rules5e::step(state_, mv));
}

// R-G12 summons (§3.1): may a summon footprint `sp` appear for `caster`'s `def`: in bounds and walkable, not on a living
// creature nor on a footprint placed earlier in the same request, within the spell's range (footprint to footprint; range 0
// counts as 5 ft) and in the caster's line of sight.
bool Combat::summon_spot_ok(CombatantId caster, const rules5e::SpellDef& def, Spot sp, const std::vector<Spot>& placed) const {
    if (!grid_.footprint_walkable(sp.p, sp.n)) return false;
    const auto occ = occupied_except(0);
    for (int dy = 0; dy < sp.n; ++dy)
        for (int dx = 0; dx < sp.n; ++dx)
            if (std::find(occ.begin(), occ.end(), Vec2i{sp.p.x + dx, sp.p.y + dy}) != occ.end()) return false;
    for (const Spot& o : placed) if (rect_distance(sp.p, sp.n, o.p, o.n) == 0) return false;
    const Spot cs = spot(caster);
    if (rect_distance(sp.p, sp.n, cs.p, cs.n) > std::max(1, int(def.range_ft) / kFeetPerTile)) return false;
    return los_spots(cs, sp);
}

std::vector<Vec2i> Combat::summon_tiles_for(CombatantId caster, const rules5e::SpellDef& def, const std::vector<uint8_t>& ar,
                                            std::string* error) const {
    auto fail = [&](const char* m) { if (error) *error = m; return std::vector<Vec2i>{}; };
    const SummonOptions* o = content_ ? content_->summon_options(def.id) : nullptr;
    if (!o || !rules5e::find(state_, caster)) return fail("summon not available");
    const Spot cs = spot(caster);
    const int r = std::max(1, int(def.range_ft) / kFeetPerTile);
    std::vector<Spot> placed;
    std::vector<Vec2i> out;
    for (uint8_t k : ar) {   // id order = request order; nearest to the caster, then row-major (y, then x)
        if (k >= o->archetypes.size() || !content_->find(o->archetypes[k])) return fail("summon not available");
        const int n = content_->find(o->archetypes[k])->footprint;
        std::optional<Spot> best;
        int best_d = 0;
        for (int y = cs.p.y - r - n; y <= cs.p.y + cs.n + r; ++y)
            for (int x = cs.p.x - r - n; x <= cs.p.x + cs.n + r; ++x) {
                const Spot sp{{x, y}, n};
                if (!summon_spot_ok(caster, def, sp, placed)) continue;
                const int d = rect_distance(sp.p, sp.n, cs.p, cs.n);
                if (!best || d < best_d) { best = sp; best_d = d; }
            }
        if (!best) return fail("no unoccupied space");
        placed.push_back(*best);
        out.push_back(best->p);
    }
    return out;
}

bool Combat::aggressive_step_ok(CombatantId mover, Vec2i to) const {
    // Priced like the Step itself (plan_step -> step_cost_ft): move modes, zones, another creature's space, squeezing.
    StepPlan plan;
    if (!plan_step(mover, to, plan)) return aggressive_step_ok(mover, to, plan.cost, plan.n);
    const int fp = footprint_size(mover);
    return aggressive_step_ok(mover, to, step_cost_ft(mover, pos(mover), to, fp, overlaps_other(mover, to, fp)), fp);
}
bool Combat::aggressive_step_ok(CombatantId mover, Vec2i to, int cost, int fp) const {
    const TurnKey now = turn_key();
    if (!aggressive_ || aggressive_->stamp != now || aggressive_->actor != mover) return true;
    const Vec2i from = pos(mover);
    if (cost <= aggressive_->free_ft) return true;   // the creature's own movement
    const auto* m = rules5e::find(state_, mover);
    if (!m || m->has(rules5e::Condition::Blinded)) return false;
    for (const auto& sl : slots_) {
        const auto* k = rules5e::find(state_, sl.id);
        if (sl.id == mover || !k || k->dead || !hostile(mover, sl.id) || !line_of_sight(mover, sl.id)) continue;
        const Spot ks = spot(sl.id);
        if (rect_distance(to, fp, ks.p, ks.n) < rect_distance(from, fp, ks.p, ks.n)) return true;
    }
    return false;
}

uint64_t Combat::hash() const {
    CanonicalHasher h;
    h.u64(seed_);
    h.u32(uint32_t(events_.size()));
    for (const auto& e : events_) {
        h.byte(uint8_t(e.kind));
        h.byte(uint8_t(e.rules.kind));
        h.u32(e.rules.actor);
        h.u32(e.rules.target);
        h.i32(e.rules.a);
        h.i32(e.rules.b);
        h.i32(e.rules.c);
        h.byte(e.rules.reaction_depth);
        h.u32(e.actor);
        h.i32(e.tile.x);
        h.i32(e.tile.y);
    }
    for (std::size_t si = 0; si < created_count_ && si < slots_.size(); ++si) {   // R-G12: create-time slots only (summons: 0xAF)
        const Slot& s = slots_[si];
        h.u32(s.id);
        const Vec2i p = pos(s.id);
        h.i32(p.x);
        h.i32(p.y);
        const auto* c = rules5e::find(state_, s.id);
        h.i16(c ? c->hp : int16_t(0));
        h.byte(c && c->dead ? 1 : 0);
    }
    // Feature uses (rules state). Folded only when any are registered, so feature-less combats
    // keep their hashes.
    if (!state_.features.empty()) h.u64(rules5e::feature_uses_digest(state_));
    // v7 sections: each folded only when non-empty, under its own tag byte (so "pools only" and
    // "action uses only" differ). Rows are registered at setup only for content that uses them, so every
    // existing combat hashes as before. 0xA2 (modifiers) is reserved for a later row.
    if (!state_.action_uses.empty()) { h.byte(0xA1); h.u64(rules5e::action_uses_digest(state_)); }
    if (!state_.pools.empty()) { h.byte(0xA3); h.u64(rules5e::pools_digest(state_)); }
    if (rules5e::has_innate_state(state_)) { h.byte(0xA4); h.u64(rules5e::innate_digest(state_)); }
    // 0xA2: timed modifiers (row 12), only when one is on the board (or was applied); never for old content.
    if (rules5e::has_modifier_state(state_)) { h.byte(0xA2); h.u64(rules5e::modifiers_digest(state_)); }
    // 0xA5: timed lasting effects (condition durations). 0xA2 (modifiers) stays reserved for row 12; 0xA4 (innate) is Engine Core's.
    if (rules5e::has_timed_effects(state_)) { h.byte(0xA5); h.u64(rules5e::timed_effects_digest(state_)); }
    // 0xBF (EC spider follow-up): destructible restraints (the web) with HP left: target, spell, object_hp, in effect order.
    // Only when one is on the board, so every existing hash is unchanged.
    {
        bool any = false;
        for (const auto& e : state_.effects) any = any || e.object_hp > 0;
        if (any) {
            h.byte(0xBF);
            for (const auto& e : state_.effects)
                if (e.object_hp > 0) { h.u32(e.target); h.u32(e.spell_id); h.i16(e.object_hp); }
        }
    }
    // 0xA6: event triggers' once-per-turn / round bookkeeping (row 9a), only when some combatant has triggers.
    if (rules5e::has_trigger_state(state_)) { h.byte(0xA6); h.u64(rules5e::triggers_digest(state_)); }
    // 0xA7 (R-G8, Director): sourced conditions, only the entries that carry Grappled (spell-applied Charmed / Frightened entries
    // are left out so no golden moves; widen only with documented golden regeneration once a rule reads their source).
    if (rules5e::has_sourced_conditions(state_)) {
        bool any = false;
        for (const auto& sc : state_.sourced) any = any || sc.condition == rules5e::Condition::Grappled;
        if (any) {
            h.byte(0xA7);
            for (const auto& sc : state_.sourced) {
                if (sc.condition != rules5e::Condition::Grappled) continue;
                h.u32(sc.target);
                h.u32(sc.source);
                h.i16(sc.escape_dc);
            }
        }
        // 0xDB (Druid gap G, monster grapple rider): the appended SourcedCondition fields, only for Grappled entries a rider
        // with restrains / exclusive / max_grapples made (via_attack != 0xFF); plain grapples leave every hash unchanged.
        bool rider = false;
        for (const auto& sc : state_.sourced) rider = rider || (sc.condition == rules5e::Condition::Grappled && sc.via_attack != 0xFF);
        if (rider) {
            h.byte(0xDB);
            for (const auto& sc : state_.sourced) {
                if (sc.condition != rules5e::Condition::Grappled || sc.via_attack == 0xFF) continue;
                h.u32(sc.target);
                h.u32(sc.source);
                h.byte(sc.via_attack);
                h.byte(uint8_t((sc.restrains ? 1 : 0) | (sc.exclusive ? 2 : 0) | (sc.owns_restrained ? 4 : 0)));
            }
        }
    }
    // 0xAA (small leftovers, rules #111): Life Drain max-HP reductions, Haste lethargy, active Reckless Attack and escalating
    // (Petrifying Gaze) effects, only when some is present; content that uses none hashes as before.
    if (rules5e::has_leftover_state(state_)) { h.byte(0xAA); h.u64(rules5e::leftover_digest(state_)); }
    // 0xAD (priced materials, CONTENT_CONVENTIONS §7): consumed materials in cast order (caster, spell, index of the entry in
    // the caster's equipment at that moment, its value_gp), only once something was consumed; the remaining equipment is
    // derived from content plus this list, and no combat consumed anything before, so every golden hashes as before.
    if (!materials_used_.empty()) {
        h.byte(0xAD);
        h.u32(uint32_t(materials_used_.size()));
        for (const auto& u : materials_used_) { h.u32(u.caster); h.u32(u.spell_id); h.u32(u.entry); h.u32(u.value_gp); }
    }
    // 0xAC (R-G15, rules #105 / #106): the state of the new spell kinds that 0xA5 does not cover (damage dice, type, sustain
    // stamp, ends-on-damage, slot level), only for effects that carry it: Sleep / Color Spray-style `ends_on_damage`, Acid
    // Arrow's delayed and Heat Metal's sustained damage (`damage_when != 0`). No such effect exists in older content.
    {
        uint32_t n = 0;
        for (const auto& e : state_.effects) n += (e.damage_when != 0 || e.ends_on_damage) ? 1u : 0u;
        if (n != 0) {
            h.byte(0xAC);
            h.u32(n);
            for (const auto& e : state_.effects) {
                if (e.damage_when == 0 && !e.ends_on_damage) continue;
                h.u32(e.spell_id);
                h.u32(e.caster);
                h.u32(e.target);
                h.byte(uint8_t((e.no_condition ? 1 : 0) | (e.ends_on_damage ? 2 : 0) | (e.magical ? 4 : 0)));
                h.byte(e.damage_when);
                h.byte(e.damage.count);
                h.byte(e.damage.sides);
                h.i16(e.damage.bonus);
                h.byte(uint8_t(e.damage_type));
                h.u32(e.cast_stamp);
                h.byte(e.slot_level);
                if (e.damage_when == 3 && e.container_damage != 0) h.u16(e.container_damage);   // 7.2w Swallow: damage taken this turn (regurgitation), only when non-zero
            }
        }
    }
    // 0xA8 (R-G16): surprised creatures, only while one is still surprised (round 1 of an ambush); never for other combats.
    if (rules5e::has_surprised(state_)) { h.byte(0xA8); h.u64(rules5e::surprised_digest(state_)); }
    // 0xAB (R-G11): zones, only once a zone exists or has existed (has_zone_state); no older combat has one.
    if (rules5e::has_zone_state(state_)) { h.byte(0xAB); h.u64(rules5e::zones_digest(state_)); }
    // 0xAE (Petrifying Gaze, 7.2p): ids of creatures with their eyes averted, only while some is; never for gazeless combats.
    if (rules5e::has_gaze_state(state_)) { h.byte(0xAE); h.u64(rules5e::gaze_digest(state_)); }
    // 0xC5 (vision, 7.2x): who is hidden from whom (rules' hidden_digest), only while someone is hidden; no older combat is.
    if (rules5e::has_hidden_state(state_)) { h.byte(0xC5); h.u64(rules5e::hidden_digest(state_)); }
    {   // SRD Squeezing (feat/size-space): folded only when some creature squeezes
        bool any = false;
        for (const auto& c : state_.combatants) any = any || c.squeezing;
        if (any) {
            h.byte(0xC1);
            for (const auto& c : state_.combatants) if (c.squeezing) h.u64(c.id);
        }
    }
    {   // 0xD6 (altitude PR1): {id u64, alt_ft i16} per creature off the ground, only when some creature is; altitude-0 combats unchanged
        bool any = false;
        for (const auto& c : state_.combatants) any = any || c.alt_ft != 0;
        if (any) {
            h.byte(0xD6);
            for (const auto& c : state_.combatants) {
                if (c.alt_ft != 0) { h.u64(c.id); h.u16(uint16_t(c.alt_ft)); }
            }
        }
    }
    // 0xAF (R-G12, SUMMONS_TRANSFORMS.md §3.2): rules' summons_digest, then sim's part: summon slot count, then per summon slot
    // (creation order) id, anchor x / y, footprint side, gone, team. Only while has_summon_state (something was summoned or
    // transformed); no older combat does either.
    if (rules5e::has_summon_state(state_)) {
        h.byte(0xAF);
        h.u64(rules5e::summons_digest(state_));
        h.u32(uint32_t(slots_.size() - created_count_));
        for (std::size_t si = created_count_; si < slots_.size(); ++si) {
            const Slot& s = slots_[si];
            const Vec2i p = pos(s.id);
            const auto* c = rules5e::find(state_, s.id);
            h.u32(s.id); h.i32(p.x); h.i32(p.y);
            h.byte(uint8_t(s.archetype->footprint)); h.byte(s.gone ? 1 : 0); h.byte(c ? c->team : 0);
        }
    }
    // 0xD0 (EQUIPMENT.md 8, Engine Core): loadout_digest, only when a loadout exists (CombatOptions::equipment and a creature
    // with a loaded item); never with the option off.
    if (!state_.loadouts.empty()) { h.byte(0xD0); h.u64(loadout_digest(state_)); }
    // 0xD2 (EQUIPMENT.md 4.4): ground items (count, then instance, def, count, flags, tile x / y in instance order), once a
    // ground item has existed (sticky ground_items_seen: a picked-up item still differs from one never dropped).
    if (state_.ground_items_seen) {
        h.byte(0xD2);
        h.u32(uint32_t(state_.ground_items.size()));
        for (const auto& g : state_.ground_items) {
            h.u32(g.item.instance); h.u32(g.item.def); h.u16(g.item.count); h.byte(g.item.flags);
            h.i32(g.x); h.i32(g.y);
        }
    }
    // 0xD4 (7.2zh Branding Smite): pending charge and residual brand, only while one exists.
    if (rules5e::has_smite_state(state_)) { h.byte(0xD4); h.u64(rules5e::smite_digest(state_)); }
    // 0xD5 (7.2zh Heat Metal): the heated object and the while-held disadvantage, only while one is set.
    if (rules5e::has_heat_metal_state(state_)) { h.byte(0xD5); h.u64(rules5e::heat_metal_digest(state_)); }
    // 0xDC (batch 3 mummy rot): no_regain_hp is dynamic once an attack can curse; folded (one byte per combatant, in order) only
    // while some combatant has an attack with curse_no_regain_dc, so every existing hash is unchanged.
    bool curse = false;
    for (const auto& c : state_.combatants)
        for (const auto& at : c.attacks) curse = curse || at.curse_no_regain_dc > 0;
    if (curse) {
        h.byte(0xDC);
        for (const auto& c : state_.combatants) h.byte(c.no_regain_hp ? 1 : 0);
    }
    return h.value();
}

bool Combat::apply_surprise(const std::vector<CombatantId>& ids, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (!log_.empty()) return fail("surprise must be set before the first command");
    if (pending()) return fail("a decision is pending");
    if (state_.ended) return fail("the encounter has ended");
    for (CombatantId id : ids) {
        const auto* k = rules5e::find(state_, id);
        if (!k) return fail("unknown surprised combatant");
        if (k->dead) return fail("a dead combatant cannot be surprised");
    }
    rules5e::apply_surprise(state_.combatants, ids);
    surprised_at_start_.clear();
    for (const auto& k : state_.combatants)
        if (k.surprised) surprised_at_start_.push_back(k.id);
    return true;
}

void Combat::set_end_condition(rules5e::EndConditionFn fn) {
    scenario_end_ = {};
    state_.hooks.end_condition = std::move(fn);
}

void Combat::set_scenario_end(ScenarioEnd end) {
    scenario_end_ = end;
    const auto kind = end.kind;
    const auto cap = end.round_cap;
    state_.hooks.end_condition = [kind, cap](const rules5e::EncounterState& state) -> std::optional<rules5e::EncounterOutcome> {
        std::optional<rules5e::EncounterOutcome> outcome;
        switch (kind) {
        case ScenarioEndKind::Teams: outcome = rules5e::default_end_condition(state); break;
        case ScenarioEndKind::NoPcConscious: outcome = rules5e::end_when_no_pc_conscious(state); break;
        case ScenarioEndKind::NoPcOrNoOpponent: outcome = rules5e::end_when_no_pc_or_no_opponent(state); break;
        }
        if (outcome) return outcome;
        // Round cap: when round_cap != 0 and state.round > round_cap, and the built-in policy
        // returned nothing, the outcome is EndReason::Custom.
        if (cap != 0 && state.round > cap)
            return rules5e::EncounterOutcome{rules5e::EndReason::Custom, rules5e::pc_statuses(state), {}};
        return std::nullopt;
    };
}

uint64_t Combat::log_hash() const {
    CanonicalHasher h;
    h.u32(uint32_t(log_.size()));
    for (const auto& e : log_) {
        const ActionRequest& q = e.step.request;
        h.u32(q.actor);
        h.byte(uint8_t(q.kind));
        h.u32(q.target);
        h.byte(q.attack_index);
        h.i16(q.move_ft);
        h.byte(q.as_bonus_action ? 1 : 0);
        h.byte(q.target_within_5ft ? 1 : 0);
        h.byte(q.leaves_reach ? 1 : 0);
        h.u32(q.feature_id);
        h.byte(uint8_t(q.ready_kind));
        h.u32(q.trigger_id);
        h.byte(q.ranged_hostile_within_5ft ? 1 : 0);
        h.byte(q.beyond_normal_range ? 1 : 0);
        if (q.multiattack != 0) h.byte(q.multiattack);   // folded only when used: older logs hash as before
        if (q.special != 0) { h.byte(0xB5); h.byte(q.special); }   // R-G8 grapple / shove / escape: folded only when set (0xA9 is retired)
        if (q.bonus_option != 0) { h.byte(0xB0); h.byte(q.bonus_option); }   // R-G6 bonus attack option: folded only when set
        if (q.two_handed_grip) { h.byte(0xB6); h.byte(1); }   // R-G7 versatile weapon in two hands: folded only when set
        if (q.sustain) { h.byte(0xB8); h.byte(1); }   // R-G15 Heat Metal sustained with a bonus action: folded only when set
        // 0xBA Font of Magic Flexible Casting (rules #147, VERSION_BUMP_BATCH 7.2o): folded only when flexible != 0.
        if (q.flexible != 0) { h.byte(0xBA); h.byte(q.flexible); h.byte(q.flexible_level); }
        if (q.squeeze != 0) { h.byte(0xC2); h.byte(q.squeeze); }   // SRD Squeezing on a Move (feat/size-space)
        if (q.move_mode != 0) { h.byte(0xC3); h.byte(q.move_mode); }   // feat/move-modes: the speed a Move used
        if (q.alt_to != 0) { h.byte(0xD7); h.i16(q.alt_to); }   // altitude PR2: vertical Move delta, only when non-zero
        if (q.command_word != 0) { h.byte(0xC4); h.byte(q.command_word); }   // 7.2v Command's word on a CastSpell: folded only when set
        if (q.has_guess) { h.byte(0xC6); h.i32(q.guess_x); h.i32(q.guess_y); }   // vision 7.2x: guessed tile of a hidden target
        if (q.object_use != 0) { h.byte(0xCB); h.byte(q.object_use); h.i16(q.dc); }   // 7.2y UseObject (Fast Hands): folded only when set
        if (q.mark_ability != 0 || q.move_mark) { h.byte(0xCC); h.byte(q.mark_ability); h.byte(q.move_mark ? 1 : 0); }   // 7.2zc mark: only when set
        if (q.ward_alternate != 0) { h.byte(0xCD); h.u32(q.ward_alternate); }   // 7.2zd Sanctuary ward resolution: only when set
        if (!q.heal_split.empty()) {   // 7.2zd Preserve Life split: only when set
            h.byte(0xCE); h.u32(uint32_t(q.heal_split.size()));
            for (const auto& x : q.heal_split) { h.u32(x.target); h.u32(uint32_t(uint16_t(x.amount))); }
        }
        if (q.avert_gaze) { h.byte(0xBB); h.byte(1); }
        if (q.target_willing) { h.byte(0xDD); h.byte(1); }   // batch 3 vampire spawn Bite willing target: only when set
        if (q.equip_op != 0) {   // 0xD1 (EQUIPMENT.md 4.2, Engine Core): equip request, folded only when equip_op != 0
            h.byte(0xD1); h.byte(q.equip_op); h.u32(q.equip_item); h.byte(q.equip_hand); h.u32(q.equip_to);
            h.byte(q.equip_in_reach ? 1 : 0);
            if (q.equip_op == uint8_t(rules5e::EquipOp::SetLightState)) h.byte(q.equip_state);   // only for op 10
        }   // Petrifying Gaze averting the eyes: folded only when set (gaze_seen is derived, not folded)
        // Small leftovers (rules #111): bit0 haste_action, bit1 reckless, bit2 aggressive; folded only when some bit is set.
        if (const uint8_t lo = uint8_t((q.haste_action ? 1 : 0) | (q.reckless ? 2 : 0) | (q.aggressive ? 4 : 0)); lo != 0) {
            h.byte(0xB3);
            h.byte(lo);
        }
        if (q.innate) h.byte(0xA4);                       // innate CastSpell: folded only when set, under a tag byte
        // 0xB1 (R-G14 per-cast options; also Lesser Restoration's condition_choice): folded only when some option is set.
        if (q.metamagic != 0 || !q.spared.empty() || q.heighten_target != 0 || q.damage_choice != 0 || q.condition_choice != 0) {
            h.byte(0xB1);
            h.byte(q.metamagic);
            h.u32(uint32_t(q.spared.size()));
            for (const auto sp : q.spared) h.u32(sp);
            h.u32(q.heighten_target);
            h.byte(q.damage_choice);
            h.byte(q.condition_choice);
        }
        // 0xCF (7.2ze Blindness/Deafness, `condition: chosen`): the condition the caster picked, only for such spells.
        if (q.kind == rules5e::ActionKind::CastSpell && q.condition_choice != 0)
            if (const rules5e::SpellDef* cd = spell_def(q.spell_id);
                cd && std::size_t(q.condition_choice) <= cd->condition_choices.size()) {
                h.byte(0xCF);
                h.byte(uint8_t(cd->condition_choices[std::size_t(q.condition_choice - 1)]));
            }
        // R-G9: 0xB4 forced_feet / teleport_feet, 0xB2 the teleport destination; each folded only when set.
        if (q.forced_feet != 0 || q.teleport_feet != 0) { h.byte(0xB4); h.u16(q.forced_feet); h.u16(q.teleport_feet); }
        if (q.has_teleport_dest) { h.byte(0xB2); h.i32(q.teleport_x); h.i32(q.teleport_y); }
        if (q.form_archetype != 0) { h.byte(0xBD); h.byte(q.form_archetype); }   // R-G12 form choice: folded only when set
        if (q.has_area_origin) { h.byte(0xC0); h.byte(1); h.i32(q.area_x); h.i32(q.area_y); }   // 7.2t cover: aimed area origin, only when set
        // 0xB7 (R-G11): caller-chosen zone inputs, only when set (membership lists / zone_move_ft are sim-derived, not folded).
        if (q.zone_move != 0 || q.has_zone_dest || !q.zone_excluded.empty()) {
            h.byte(0xB7);
            h.u32(q.zone_move);
            h.byte(q.has_zone_dest ? 1 : 0);
            h.i32(q.zone_x);
            h.i32(q.zone_y);
            h.u32(uint32_t(q.zone_excluded.size()));
            for (const auto zx : q.zone_excluded) h.u32(zx);
        }
        // 0xBC (R-G12 summons, SIM_EVENTS.md): summon_ids, summon_archetypes, summon_tiles, command, command_ids, from the
        // logged request (rules filled the ids), only when one is set.
        if (!q.summon_ids.empty() || !q.summon_archetypes.empty() || !q.summon_tiles.empty() || q.command != 0 || !q.command_ids.empty()) {
            h.byte(0xBC);
            h.u32(uint32_t(q.summon_ids.size()));
            for (const auto id : q.summon_ids) h.u32(id);
            h.u32(uint32_t(q.summon_archetypes.size()));
            for (const auto k : q.summon_archetypes) h.byte(k);
            h.u32(uint32_t(q.summon_tiles.size()));
            for (const auto& t : q.summon_tiles) { h.i32(t.x); h.i32(t.y); }
            h.byte(q.command);
            h.u32(uint32_t(q.command_ids.size()));
            for (const auto id : q.command_ids) h.u32(id);
        }
        h.byte(q.heal_dice.count);
        h.byte(q.heal_dice.sides);
        h.i16(q.heal_dice.bonus);
        h.byte(e.to ? 1 : 0);
        h.i32(e.to ? e.to->x : 0);
        h.i32(e.to ? e.to->y : 0);
        h.u32(uint32_t(e.step.reactions.size()));
        for (const auto& rr : e.step.reactions) {
            h.u32(rr.window_seq);
            h.byte(uint8_t(rr.trigger));
            h.u32(rr.reactor);
            h.byte(rr.decision.react ? 1 : 0);
            h.byte(rr.decision.option);
        }
    }
    return h.value();
}

// ---- R-G11 zones (VERSION_BUMP_BATCH 7.2l hand-off, ZONES_AURAS.md §3) ----
bool Combat::zone_spec_anchor_creature(const rules5e::ActiveZone& z) const {
    const rules5e::SpellDef* d = spells_ ? spells_->find(z.spell_id) : nullptr;
    return d && d->zone.anchor == rules5e::ZoneAnchor::Creature;
}

bool Combat::zone_covers(const rules5e::ActiveZone& z, Vec2i tile, const Spot* owner_at, const Vec2i* anchor_at) const {
    const rules5e::SpellDef* d = spells_ ? spells_->find(z.spell_id) : nullptr;
    if (!d || d->kind != rules5e::SpellEffectKind::Zone || z.ring) return false;
    const int sz = int(z.size_ft != 0 ? z.size_ft : d->zone.size_ft) / kFeetPerTile;
    const AreaShape shape = d->zone.shape;
    if (d->zone.anchor == rules5e::ZoneAnchor::Creature) {
        if (!rules5e::find(state_, z.owner)) return false;
        const Spot o = owner_at ? *owner_at : spot(z.owner);
        const int dist = rect_distance(o.p, o.n, tile, 1);
        if (shape == AreaShape::Sphere || shape == AreaShape::Cylinder) return dist <= sz;
        if (shape == AreaShape::Cube) return dist <= sz / 2;
        return false;
    }
    if (!z.has_anchor_tile && !anchor_at) return false;
    const Vec2i a = anchor_at ? *anchor_at : Vec2i{z.anchor_x, z.anchor_y};
    if (shape == AreaShape::Sphere || shape == AreaShape::Cylinder) return std::max(std::abs(tile.x - a.x), std::abs(tile.y - a.y)) <= sz;
    if (shape == AreaShape::Cube) {
        const int lo = -((sz - 1) / 2), hi = sz / 2;
        return tile.x >= a.x + lo && tile.x <= a.x + hi && tile.y >= a.y + lo && tile.y <= a.y + hi;
    }
    return false;   // walls (Line) are not modelled yet
}

std::vector<Vec2i> Combat::zone_tiles(const rules5e::ActiveZone& z) const {
    std::vector<Vec2i> tiles;
    const rules5e::SpellDef* def = spell_def(z.spell_id);
    if (!def || def->kind != rules5e::SpellEffectKind::Zone || z.ring) return tiles;
    const int sz = int(z.size_ft != 0 ? z.size_ft : def->zone.size_ft) / kFeetPerTile;
    int min_x = 0, max_x = -1, min_y = 0, max_y = -1;
    if (def->zone.anchor == rules5e::ZoneAnchor::Creature) {
        if (!rules5e::find(state_, z.owner)) return tiles;
        const Vec2i op = pos(z.owner);
        const int on = footprint_size(z.owner);
        const int pad = (def->zone.shape == AreaShape::Cube) ? sz / 2 : sz;
        min_x = op.x - pad;
        max_x = op.x + on - 1 + pad;
        min_y = op.y - pad;
        max_y = op.y + on - 1 + pad;
    } else {
        if (!z.has_anchor_tile) return tiles;
        const Vec2i a{z.anchor_x, z.anchor_y};
        if (def->zone.shape == AreaShape::Sphere || def->zone.shape == AreaShape::Cylinder) {
            min_x = a.x - sz;
            max_x = a.x + sz;
            min_y = a.y - sz;
            max_y = a.y + sz;
        } else if (def->zone.shape == AreaShape::Cube) {
            const int lo = -((sz - 1) / 2), hi = sz / 2;
            min_x = a.x + lo;
            max_x = a.x + hi;
            min_y = a.y + lo;
            max_y = a.y + hi;
        } else {
            return tiles;   // walls (Line) are not modelled yet
        }
    }
    for (int y = min_y; y <= max_y; ++y)
        for (int x = min_x; x <= max_x; ++x)
            if (zone_covers(z, {x, y})) tiles.push_back({x, y});
    return tiles;
}

bool Combat::zone_holds(const rules5e::ActiveZone& z, CombatantId, Spot at, bool completely, const Spot* owner_at,
                        const Vec2i* anchor_at) const {
    bool any = false, all = true;
    for (int y = 0; y < at.n; ++y)
        for (int x = 0; x < at.n; ++x) {
            const bool in = zone_covers(z, {at.p.x + x, at.p.y + y}, owner_at, anchor_at);
            any = any || in;
            all = all && in;
        }
    return completely ? all : any;
}

void Combat::zone_lists(CombatantId mover, Spot from, Spot to, rules5e::ActionRequest& q) const {
    q.zones_entered.clear(); q.zones_left.clear(); q.zones_within.clear();
    for (const auto& z : state_.zones) {   // creation order = ascending ids
        if (z.owner == mover && zone_spec_anchor_creature(z)) continue;   // its own aura moves with it
        const bool was = zone_holds(z, mover, from, false), is = zone_holds(z, mover, to, false);
        if (!was && is) q.zones_entered.push_back(z.zone_id);
        else if (was && !is) q.zones_left.push_back(z.zone_id);
        else if (was && is) q.zones_within.push_back(z.zone_id);
    }
}

std::vector<rules5e::ZoneChange> Combat::zone_changes_for(CombatantId mover, Spot from, Spot to, uint32_t moved_zone,
                                                          const Vec2i* anchor_to) const {
    std::vector<rules5e::ZoneChange> out;
    for (const auto& z : state_.zones) {
        const bool aura = z.owner == mover && zone_spec_anchor_creature(z) && (from.p != to.p);
        const bool moved = moved_zone != 0 && z.zone_id == moved_zone && anchor_to;
        if (!aura && !moved) continue;
        for (const auto& k : state_.combatants) {
            if (k.id == z.owner || k.dead) continue;
            const Spot ks = spot(k.id);
            const bool was = zone_holds(z, k.id, ks, false);
            const bool is = aura ? zone_holds(z, k.id, ks, false, &to) : zone_holds(z, k.id, ks, false, nullptr, anchor_to);
            if (was != is) out.push_back({k.id, z.zone_id, is});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const rules5e::ZoneChange& x, const rules5e::ZoneChange& y) {
        return x.zone_id != y.zone_id ? x.zone_id < y.zone_id : x.creature < y.creature;
    });
    return out;
}

std::vector<Vec2i> Combat::legal_zone_tiles(CombatantId actor, const rules5e::SpellDef& def, uint32_t zone_move) const {
    std::vector<Vec2i> out;
    if (def.kind != rules5e::SpellEffectKind::Zone || def.zone.anchor != rules5e::ZoneAnchor::Point || !rules5e::find(state_, actor)) return out;
    const Spot cs = spot(actor);
    if (zone_move != 0) {
        const rules5e::ActiveZone* z = rules5e::find_zone(state_, zone_move);
        if (!z || !z->has_anchor_tile || z->owner != actor || z->spell_id != def.id) return out;
        const int r = int(def.zone.move_ft) / kFeetPerTile;
        for (int y = z->anchor_y - r; y <= z->anchor_y + r; ++y)
            for (int x = z->anchor_x - r; x <= z->anchor_x + r; ++x)
                if (grid_.in_bounds({x, y}) && Vec2i{x, y} != Vec2i{z->anchor_x, z->anchor_y}) out.push_back({x, y});
        return out;
    }
    const int r = int(def.range_ft) / kFeetPerTile;
    for (int y = cs.p.y - r; y <= cs.p.y + cs.n - 1 + r; ++y)
        for (int x = cs.p.x - r; x <= cs.p.x + cs.n - 1 + r; ++x) {
            const Vec2i t{x, y};
            if (!grid_.in_bounds(t) || rect_distance(cs.p, cs.n, t, 1) > r) continue;
            if (!los_spots(cs, Spot{t, 1})) continue;
            out.push_back(t);
        }
    return out;
}

// ---- altitude PR2 ----
int Combat::abs_height_ft(CombatantId id) const {
    const auto* c = rules5e::find(state_, id);
    return grid_.footprint_ground_ft(pos(id), footprint_size(id)) + (c ? int(c->alt_ft) : 0);
}
bool Combat::airborne(CombatantId id) const {
    const auto* c = rules5e::find(state_, id);
    return c && c->alt_ft > 0 && c->fly_ft > 0 && !c->has(rules5e::Condition::Prone);
}
bool Combat::burrowed(CombatantId id) const {
    const auto* c = rules5e::find(state_, id);
    return c && c->alt_ft < 0 && c->burrow_ft > 0 && !grid_.water(pos(id));
}
std::vector<Vec2i> Combat::occupied_except(CombatantId id, int abs_ft) const {
    std::vector<Vec2i> out;
    bool mover_swarm = false;
    for (const auto& s : slots_) if (s.id == id) mover_swarm = s.archetype->swarm;
    const int n = footprint_size(id);
    for (const auto& s : slots_) {
        const auto* c = rules5e::find(state_, s.id);
        if (s.id == id || !c || c->dead || s.archetype->swarm || mover_swarm) continue;
        if (!spans_overlap(abs_ft, n, abs_height_ft(s.id), footprint_size(s.id))) continue;
        for (Vec2i t : footprint_tiles(s.id)) out.push_back(t);
    }
    return out;
}
std::vector<Vec2i> Combat::path_for(CombatantId id, Vec2i from, const std::vector<Vec2i>& goals, int footprint) const {
    const MoveCaps caps = caps_of(id);
    if (airborne(id)) {
        const int abs_ft = abs_height_ft(id);
        return grid_.path(from, goals, occupied_except(id, abs_ft), footprint, caps, abs_ft);
    }
    if (burrowed(id)) return grid_.path(from, goals, occupied_except(id, abs_height_ft(id)), footprint, caps, kPathBurrowed);
    return grid_.path(from, goals, occupied_except(id), footprint, caps);
}
void Combat::step_altitude(const rules5e::ActionRequest& q, int from_ground_ft, int from_abs_ft, bool was_airborne) {
    const auto* c = rules5e::find(state_, q.actor);
    if (!c) return;
    if (q.alt_to != 0) { set_altitude(q.actor, int16_t(c->alt_ft + q.alt_to), kAltMove); return; }
    if (!grid_.has_ground()) return;   // chunk@1 / chunk@2 maps are flat: alt_ft is unchanged by a step
    const int g1 = grid_.footprint_ground_ft(pos(q.actor), footprint_size(q.actor));
    if (was_airborne) { set_altitude(q.actor, int16_t(std::max(0, from_abs_ft - g1)), kAltMove); return; }
    if (c->alt_ft != 0) return;   // on a wall / in water: the step keeps its alt_ft
    const int drop = from_ground_ft - g1;
    if (drop <= kWalkableStepFt) return;
    const MoveCaps caps = caps_of(q.actor);
    if (caps.fly && !c->has(rules5e::Condition::Prone)) { set_altitude(q.actor, int16_t(drop), kAltMove); return; }
    // A fall (altitude design §3): alt_ft stays 0 at the new ground; the event says so and the distance is recorded for
    // PR4's fall damage.
    falls_.push_back({q.actor, drop});
    SimEvent e;
    e.kind = SimEvent::Kind::AltitudeChanged;
    e.actor = q.actor;
    e.tile = pos(q.actor);
    e.rules.a = 0;
    e.rules.b = kAltFall;
    events_.push_back(e);
}

MoveCaps Combat::caps_of(CombatantId id) const {
    MoveCaps m;
    if (const auto* c = rules5e::find(state_, id)) { m.fly = c->fly_ft > 0; m.swim = c->swim_ft > 0; m.climb = c->climb_ft > 0; m.burrow = c->burrow_ft > 0; }
    return m;
}

// feat/move-modes: the move mode for one step (0 walk, 1 fly, 2 swim, 3 climb, 4 burrow while burrowed), 0xFF when no fitting speed has `cost`
// feet left, 0xFE when only flight fits and the mover is prone. The terrain picks the speed: open air needs flight; climb
// terrain uses a climb speed, else flight, else walking (extra cost already priced); water likewise with a swim speed.
// On plain ground the walking speed, or flight when walking has too little left.
uint8_t Combat::step_mode(CombatantId mover, Vec2i from, Vec2i to, int cost, int fp_in) const {
    const auto* c = rules5e::find(state_, mover);
    if (!c) return 0xFF;
    const int fp = fp_in > 0 ? fp_in : footprint_size(mover);
    bool air = false, climb = false, water = false;
    for (int dy = 0; dy < fp; ++dy)
        for (int dx = 0; dx < fp; ++dx) {
            const Vec2i t{to.x + dx, to.y + dy};
            if (fp > 1 && t.x >= from.x && t.x < from.x + fp && t.y >= from.y && t.y < from.y + fp) continue;
            if (grid_.open_air(t)) air = true;
            else if (!grid_.walkable(t) && grid_.climbable(t)) climb = true;
            else if (grid_.water(t)) water = true;
        }
    std::vector<uint8_t> order;
    const bool high = grid_.has_ground() &&
                      ledge_ft(grid_.footprint_ground_ft(from, fp), grid_.footprint_ground_ft(to, fp)) > kWalkableStepFt;
    if (airborne(mover)) order = {1};   // altitude PR2: an airborne flyer keeps flying
    else if (burrowed(mover)) order = {4};   // altitude PR2: a burrowed mover tunnels on its burrow speed
    else if (air) order = {1};
    else if (climb || high) order = {3, 1, 0};   // altitude PR2: a rise of more than 5 ft is a climb
    else if (water) order = {2, 1, 0};
    else order = {0, 1};
    const bool prone = c->has(rules5e::Condition::Prone);
    bool fly_blocked = false;
    for (uint8_t m : order) {
        if (m == 1 && prone) { fly_blocked = true; continue; }
        const int allow = rules5e::move_allowance(state_, *c, m);
        if (allow < 0) continue;
        if (cost <= allow) return m;   // crawling / dragging multipliers: the rules' check (as before)
    }
    return fly_blocked && air ? 0xFE : 0xFF;
}

int Combat::step_cost_ft(CombatantId mover, Vec2i from, Vec2i to) const { return step_cost_ft(mover, from, to, footprint_size(mover)); }
int Combat::step_cost_ft(CombatantId mover, Vec2i from, Vec2i to, int fp, bool through_creature) const {
    const MoveCaps caps = caps_of(mover);
    // altitude PR2: an airborne flyer ignores difficult tiles and other creatures' space (zones still apply).
    // A burrowed mover tunnels at 5 ft per tile the same way (feat/rules-burrow-speed; zones still apply).
    const bool air = airborne(mover) || burrowed(mover);
    bool difficult = through_creature && !air, halve = false, zoned = false;
    if (!state_.zones.empty() && spells_)
        for (const auto& z : state_.zones) {
            const rules5e::SpellDef* d = spells_->find(z.spell_id);
            if (!d || !(d->zone.flags & (rules5e::kZoneDifficult | rules5e::kZoneHalveSpeed))) continue;
            if (std::binary_search(z.excluded.begin(), z.excluded.end(), mover)) continue;
            if (!zone_holds(z, mover, Spot{to, fp}, false)) continue;
            zoned = true;
            difficult = difficult || (d->zone.flags & rules5e::kZoneDifficult);
            halve = halve || (d->zone.flags & rules5e::kZoneHalveSpeed);
        }
    int cost = 0;
    if (!difficult) {
        cost = air ? kFeetPerTile : grid_.move_cost_ft(from, to, fp, caps);   // feat/move-modes: water / climb / open air
    } else {
        // Difficult terrain (a zone, another creature's space, a difficult tile) is +1 ft per ft once: it never stacks with
        // itself, but it stacks with swimming without a swim speed (+1 ft per ft), priced per newly entered tile: plain water
        // under a zone or a creature is 5 + 5 + 5 = 15 ft (SRD 5.1 Movement). Swimmers and flyers pay 10.
        for (int ty = 0; ty < fp; ++ty)
            for (int tx = 0; tx < fp; ++tx) {
                const Vec2i t{to.x + tx, to.y + ty};
                const bool was_inside = fp > 1 && t.x >= from.x && t.x < from.x + fp && t.y >= from.y && t.y < from.y + fp;
                if (was_inside) continue;
                int tile = 0;
                if (air) {
                    tile = 2 * kFeetPerTile;   // a zone's difficult terrain only
                } else if (!grid_.walkable(t)) {
                    tile = std::max(grid_.tile_cost_ft(t, caps), 2 * kFeetPerTile);
                } else {
                    const int base = grid_.step_cost_ft(t);
                    const int swim_extra = (grid_.water(t) && !caps.swim && !caps.fly) ? base : 0;   // as Grid::tile_cost_ft
                    tile = std::max(base, 2 * kFeetPerTile) + swim_extra;
                }
                cost = std::max(cost, tile);
            }
    }
    if (halve) cost *= 2;
    if (zoned) cost = std::min(cost, 20);
    // altitude PR2 (chunk@3 ground; 0 on flat maps): a step up of 5 ft or less and descending are free (grid.hpp step_cost_ft);
    // an airborne flyer pays only for ground that rises above its absolute height.
    if (grid_.has_ground()) {
        const int g1 = grid_.footprint_ground_ft(to, fp);
        if (burrowed(mover)) {}   // stays at its depth below the ground: no ledge cost
        else if (air) cost += std::max(0, g1 - abs_height_ft(mover));
        else cost += eng::sim::step_cost_ft(grid_.footprint_ground_ft(from, fp), g1, caps);
    }
    return cost;
}

// ---- R-G9 forced movement / teleport (FORCED_MOVEMENT.md §3) ----
rules5e::ForcedMoveResult Combat::resolve_forced(const rules5e::ForcedMoveQuery& q) {
    rules5e::ForcedMoveResult res;
    if (shove_actor_ != 0 && q.source == shove_actor_ && q.mover == shove_target_ && !q.teleport) {
        res.feet_moved = q.feet;   // EC #104's shove placement stays in complete(); report the 5 ft as before
        return res;
    }
    auto commit = [this](CombatantId id, Vec2i t) {
        if (TilePos* p = reg_->get<TilePos>(entity(id))) { p->x = t.x; p->y = t.y; }
        forced_placed_.push_back({id, t});
    };
    const Vec2i start = pos(q.mover);
    if (q.teleport) {
        if (!q.has_dest) { forced_placed_.push_back({q.mover, start}); return res; }
        commit(q.mover, {q.dest_x, q.dest_y});   // validated at submit
        res.feet_moved = q.feet;
        const Spot before{start, footprint_size(q.mover)}, after{Vec2i{q.dest_x, q.dest_y}, footprint_size(q.mover)};
        for (const auto& z : state_.zones) {   // R-G11: a teleport reports entered / left only
            if (z.owner == q.mover && zone_spec_anchor_creature(z)) continue;
            const bool was = zone_holds(z, q.mover, before, false), is = zone_holds(z, q.mover, after, false);
            if (!was && is) res.zones_entered.push_back(z.zone_id);
            if (was && !is) res.zones_left.push_back(z.zone_id);
        }
    } else {
        // direction: sign of (mover centre - source centre) in doubled coordinates; pull negates; overlap: +x
        const Spot ms = spot(q.mover), ss = spot(q.source);
        const int dx2 = (2 * ms.p.x + ms.n) - (2 * ss.p.x + ss.n), dy2 = (2 * ms.p.y + ms.n) - (2 * ss.p.y + ss.n);
        Vec2i d{(dx2 > 0) - (dx2 < 0), (dy2 > 0) - (dy2 < 0)};
        if (d.x == 0 && d.y == 0) d = {1, 0};
        if (q.mode == 2) d = {-d.x, -d.y};
        const int fp = ms.n;
        const auto occ = occupied_except(q.mover);
        Vec2i cur = start;
        const int steps = int(q.feet) / kFeetPerTile;
        int moved = 0;
        std::vector<Vec2i> walked{start};
        for (; moved < steps; ++moved) {
            const Vec2i nx{cur.x + d.x, cur.y + d.y};
            bool free = grid_.footprint_walkable(nx, fp);
            for (int yy = 0; yy < fp && free; ++yy)
                for (int xx = 0; xx < fp && free; ++xx)
                    free = std::find(occ.begin(), occ.end(), Vec2i{nx.x + xx, nx.y + yy}) == occ.end();
            if (!free) break;
            cur = nx;
            walked.push_back(cur);
        }
        // R-G11 (Q7): membership per tile walked; zones_within repeats a zone once per further 5 ft inside it.
        for (const auto& z : state_.zones) {
            if (z.owner == q.mover && zone_spec_anchor_creature(z)) continue;
            bool entered = false, left = false;
            for (std::size_t i = 1; i < walked.size(); ++i) {
                const bool was = zone_holds(z, q.mover, Spot{walked[i - 1], fp}, false), is = zone_holds(z, q.mover, Spot{walked[i], fp}, false);
                if (!was && is) entered = true;
                if (was && !is) left = true;
                if (was && is) res.zones_within.push_back(z.zone_id);
            }
            if (entered) res.zones_entered.push_back(z.zone_id);
            if (left) res.zones_left.push_back(z.zone_id);
        }
        res.feet_moved = uint16_t(moved * kFeetPerTile);
        res.stopped_short = moved < steps;
        if (moved > 0) commit(q.mover, cur);
        else forced_placed_.push_back({q.mover, start});
    }
    // grapples (either direction) no longer within 5 ft
    for (const auto& sc : state_.sourced) {
        if (sc.condition != rules5e::Condition::Grappled || (sc.target != q.mover && sc.source != q.mover)) continue;
        const CombatantId other = sc.target == q.mover ? sc.source : sc.target;
        if (distance_tiles(sc.source, sc.target) > 1 &&
            std::find(res.out_of_reach_of.begin(), res.out_of_reach_of.end(), other) == res.out_of_reach_of.end())
            res.out_of_reach_of.push_back(other);
    }
    return res;
}

bool Combat::teleport_dest_ok(CombatantId actor, const rules5e::SpellDef& def, Vec2i dest) const {
    if (def.kind != rules5e::SpellEffectKind::Teleport || !rules5e::find(state_, actor)) return false;
    const Vec2i from = pos(actor);
    const int fp = footprint_size(actor);
    if (dest == from || !grid_.footprint_walkable(dest, fp)) return false;
    if (kFeetPerTile * std::max(std::abs(dest.x - from.x), std::abs(dest.y - from.y)) > int(def.teleport.max_feet)) return false;
    const auto occ = occupied_except(actor);
    for (int yy = 0; yy < fp; ++yy)
        for (int xx = 0; xx < fp; ++xx)
            if (std::find(occ.begin(), occ.end(), Vec2i{dest.x + xx, dest.y + yy}) != occ.end()) return false;
    if (def.teleport.needs_sight && !los_spots(spot(actor), spot_at(actor, dest))) return false;
    if (const CombatantId src = rules5e::condition_source(state_, actor, rules5e::Condition::Frightened)) {
        const Spot ss = spot(src);
        if (rect_distance(dest, fp, ss.p, ss.n) < rect_distance(from, fp, ss.p, ss.n)) return false;   // not closer
    }
    return true;
}

std::vector<Vec2i> Combat::legal_teleport_tiles(CombatantId actor, const rules5e::SpellDef& def) const {
    std::vector<Vec2i> out;
    if (def.kind != rules5e::SpellEffectKind::Teleport || !rules5e::find(state_, actor)) return out;
    const Vec2i from = pos(actor);
    const int r = int(def.teleport.max_feet) / kFeetPerTile;
    for (int y = from.y - r; y <= from.y + r; ++y)
        for (int x = from.x - r; x <= from.x + r; ++x)
            if (grid_.in_bounds({x, y}) && teleport_dest_ok(actor, def, {x, y})) out.push_back({x, y});
    return out;
}

std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content, uint64_t seed,
                             const std::vector<LogEntry>& log, std::string* error, const std::vector<CombatantId>& surprised,
                             ScenarioEnd end) {
    return replay(reg, chunk, content, seed, CombatOptions{}, log, error, surprised, end);
}

std::optional<Combat> replay(ecs::Registry& reg, std::span<const world::Chunk* const> chunks, const ContentLibrary& content,
                             uint64_t seed, const std::vector<LogEntry>& log, std::string* error,
                             const std::vector<CombatantId>& surprised, ScenarioEnd end) {
    return replay(reg, chunks, content, seed, CombatOptions{}, log, error, surprised, end);
}

std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content, uint64_t seed,
                             CombatOptions options, const std::vector<LogEntry>& log, std::string* error,
                             const std::vector<CombatantId>& surprised, ScenarioEnd end) {
    world::Chunk local = chunk;   // same chunk-local frame as the single-chunk create
    local.coord.x = 0;
    local.coord.y = 0;
    const world::Chunk* one[] = {&local};
    return replay(reg, std::span<const world::Chunk* const>(one), content, seed, options, log, error, surprised, end);
}

std::optional<Combat> replay(ecs::Registry& reg, std::span<const world::Chunk* const> chunks, const ContentLibrary& content,
                             uint64_t seed, CombatOptions options, const std::vector<LogEntry>& log, std::string* error,
                             const std::vector<CombatantId>& surprised, ScenarioEnd end) {
    auto c = Combat::create(reg, chunks, content, seed, options, error);
    if (!c) return std::nullopt;
    if (!surprised.empty() && !c->apply_surprise(surprised, error)) return std::nullopt;
    if (end.kind != ScenarioEndKind::Teams || end.round_cap != 0) c->set_scenario_end(end);
    for (std::size_t i = 0; i < log.size(); ++i) {
        const LogEntry& e = log[i];
        c->install_hooks();
        c->step_to_.reset();
        c->ready_to_.reset();
        if (e.step.request.kind == ActionKind::Move) c->step_to_ = e.to;
        if (e.step.request.kind == ActionKind::Ready) c->ready_to_ = e.to;
        c->derived_area_ = e.step.request.kind == ActionKind::CastSpell && e.to;   // logged targets came from an aim tile
        c->set_step_context(e.step.request);
        c->forced_placed_.clear();
        rules5e::StepResult sr = rules5e::replay_step(c->state_, e.step);   // rules' replay; no decider
        if (!sr.ok || sr.desync || sr.status != StepStatus::Done) {
            if (error) *error = "replay desync at entry " + std::to_string(i) + ": " +
                                (sr.ok ? std::string("logged decisions do not match") : std::string(sr.error));
            return std::nullopt;
        }
        c->derived_area_ = false;
        c->complete(*sr.logged, sr.events, e.to, sr.teardown_begin);
        c->log_.push_back({*sr.logged, e.to});
    }
    return c;
}

// ---- R-G12 transforms (VERSION_BUMP_BATCH 7.2q hand-off) ----

const Archetype* Combat::form_archetype(CombatantId creature, rules5e::FormKind kind, uint32_t source, uint8_t index) const {
    if (!content_) return nullptr;
    if (kind == rules5e::FormKind::Polymorph) return index < beasts_.size() ? beasts_[index] : nullptr;
    if (kind != rules5e::FormKind::WildShape) return nullptr;
    const Archetype* own = archetype(creature);
    if (!own) return nullptr;
    const auto ff = own->form_features.find(source);
    if (ff == own->form_features.end() || ff->second.kind != uint8_t(kind) || index >= ff->second.forms.size()) return nullptr;
    return content_->find(ff->second.forms[index]);
}

const Archetype* Combat::attack_archetype(CombatantId id) const {
    const auto* k = rules5e::find(state_, id);
    if (k && (k->form == rules5e::FormKind::Polymorph || k->form == rules5e::FormKind::WildShape) && k->form_archetype != 0)
        if (const Archetype* f = form_archetype(id, k->form, k->form_source, uint8_t(k->form_archetype - 1))) return f;
    return archetype(id);
}

namespace {
rules5e::FormStats stats_of(const rules5e::Combatant& c) {
    rules5e::FormStats f;
    f.abilities = c.abilities;
    f.hp = c.hp_max;
    f.hp_max = c.hp_max;
    f.ac = c.ac;
    f.proficiency_bonus = c.proficiency_bonus;
    f.speed_ft = c.speed_ft;
    f.save_proficiencies = c.save_proficiencies;
    f.skill_proficiencies = c.skill_proficiencies;
    f.resistances = c.resistances;
    f.immunities = c.immunities;
    f.vulnerabilities = c.vulnerabilities;
    f.condition_immunities = c.condition_immunities;
    f.resist_qualified = c.resist_qualified;
    f.immune_qualified = c.immune_qualified;
    f.attacks = c.attacks;
    f.multiattacks = c.multiattacks;
    f.attacks_per_action = c.attacks_per_action;
    f.size = c.size;
    f.creature_type = c.creature_type;
    f.roll_traits = c.roll_traits;
    f.triggers = c.triggers;
    f.reactions = c.reactions;
    f.bonus_attacks = c.bonus_attacks;
    f.fly_ft = c.fly_ft; f.swim_ft = c.swim_ft; f.climb_ft = c.climb_ft; f.burrow_ft = c.burrow_ft; f.hover = c.hover;   // feat/move-modes
    return f;
}
}  // namespace

std::optional<rules5e::FormStats> Combat::build_form(const rules5e::Combatant& who, rules5e::FormKind kind, uint32_t source,
                                                     uint8_t index, std::string* why) const {
    auto no = [why](const char* m) -> std::optional<rules5e::FormStats> {
        if (why) *why = m;
        return std::nullopt;
    };
    const Archetype* own = archetype(who.id);
    if (!own) return no("unknown creature");
    rules5e::FormStats f;
    if (kind == rules5e::FormKind::Shapechanger) {
        const auto ff = own->form_features.find(source);
        if (ff == own->form_features.end() || ff->second.kind != uint8_t(kind) || index >= ff->second.shapes.size())
            return no("unknown shapechanger form");
        const auto& sh = ff->second.shapes[index];
        f = stats_of(who);   // "statistics other than size are the same"; equipment is not transformed (attacks kept)
        f.hp = who.hp;
        f.size = own->base.size;   // the stat block size (the creature may already be in another shape)
        if (sh.size) f.size = rules5e::Size(uint8_t(*sh.size));
        if (sh.ac >= 0) f.ac = int8_t(sh.ac);
        if (sh.speed_ft >= 0) f.speed_ft = uint8_t(sh.speed_ft);
        if (sh.creature_type) f.creature_type = *sh.creature_type;
        if (!sh.blocked_actions.empty()) {   // form actions[]: drop the other own actions' attacks (and multiattacks using them)
            auto blocked = [&](std::string_view id) {
                return std::find(sh.blocked_actions.begin(), sh.blocked_actions.end(), id) != sh.blocked_actions.end();
            };
            std::vector<rules5e::Attack> kept;
            std::vector<int> remap(f.attacks.size(), -1);
            for (std::size_t i = 0; i < f.attacks.size(); ++i)
                if (!blocked(f.attacks[i].id)) { remap[i] = int(kept.size()); kept.push_back(f.attacks[i]); }
            std::vector<rules5e::AttackSequence> seqs;
            for (const auto& m : f.multiattacks) {
                if (blocked(m.id)) continue;
                rules5e::AttackSequence q{m.id, {}};
                bool ok = true;
                for (uint8_t ai : m.attacks) {
                    if (ai >= remap.size() || remap[ai] < 0) { ok = false; break; }
                    q.attacks.push_back(uint8_t(remap[ai]));
                }
                if (ok) seqs.push_back(std::move(q));
            }
            f.attacks = std::move(kept);
            f.multiattacks = std::move(seqs);
        }
    } else if (kind == rules5e::FormKind::WildShape || kind == rules5e::FormKind::Polymorph) {
        const Archetype* fa = form_archetype(who.id, kind, source, index);
        if (!fa) return no("unknown form archetype");
        if (fa->schema_type != "monster" || fa->base.creature_type != rules5e::CreatureType::Beast) return no("form is not a beast");
        if (fa->cr_x8 < 0) return no("form has no challenge rating");
        if (kind == rules5e::FormKind::WildShape) {
            const auto& ff = own->form_features.at(source);
            if (fa->cr_x8 > ff.max_cr_x8) return no("form above the druid's max CR");
            if (fa->fly_ft > 0 && ff.level < ff.no_fly_below) return no("flying form below the druid level");
            if (fa->swim_ft > 0 && ff.level < ff.no_swim_below) return no("swimming form below the druid level");
        } else {   // Polymorph: CR <= the target's CR, or its level for a character
            const int limit = own->cr_x8 >= 0 ? own->cr_x8 : int(own->char_level) * 8;
            if (fa->cr_x8 > limit) return no("form above the target's level / CR");
        }
        f = stats_of(fa->base);   // natural weapons and AC only: gear melds (no equipment AC or attacks)
        f.size = fa->base.size;
        if (kind == rules5e::FormKind::WildShape) f.blocked_features = own->humanoid_only_features;
    } else {
        return no("not a transform");
    }
    // Footprint (SUMMONS_TRANSFORMS §3.1 checks at the creature's own anchor): a bigger form must fit where it stands, every
    // new tile walkable and free of other creatures; otherwise the form is not available. A smaller form always fits.
    const int side = int(footprint_of(CreatureSize(uint8_t(f.size))));
    if (side > footprint_size(who.id)) {
        const Vec2i at = pos(who.id);
        if (!grid_.footprint_walkable(at, side)) return no("form does not fit here");
        const auto occ = occupied_except(who.id);
        for (int dy = 0; dy < side; ++dy)
            for (int dx = 0; dx < side; ++dx)
                if (std::find(occ.begin(), occ.end(), Vec2i{at.x + dx, at.y + dy}) != occ.end()) return no("form does not fit here");
    }
    return f;
}

void Combat::form_footprint(const rules5e::Event& e, std::vector<SimEvent>& out) {
    const CombatantId id = e.actor;
    const Slot* sl = slot(id);
    if (!sl) return;
    int before = int(sl->archetype->footprint);
    if (auto it = form_side_.find(id); it != form_side_.end()) before = it->second;
    const int after = footprint_size(id);
    if (e.kind == rules5e::EventKind::Transformed) form_side_[id] = after;
    else form_side_.erase(id);
    if (after == before) return;
    if (after > before) {   // only a revert grows unchecked: move to the nearest free anchor (Chebyshev ring, row-major ties)
        auto fits = [&](Vec2i t) {
            if (!grid_.footprint_walkable(t, after)) return false;
            const auto occ = occupied_except(id);
            for (int dy = 0; dy < after; ++dy)
                for (int dx = 0; dx < after; ++dx)
                    if (std::find(occ.begin(), occ.end(), Vec2i{t.x + dx, t.y + dy}) != occ.end()) return false;
            return true;
        };
        const Vec2i at = pos(id);
        if (!fits(at)) {
            std::optional<Vec2i> best;
            for (int r = 1; r <= 8 && !best; ++r)
                for (int y = at.y - r; y <= at.y + r && !best; ++y)
                    for (int x = at.x - r; x <= at.x + r && !best; ++x)
                        if (std::max(std::abs(x - at.x), std::abs(y - at.y)) == r && grid_.in_bounds({x, y}) && fits({x, y}))
                            best = Vec2i{x, y};
            if (best) {   // none within 8 tiles: it stays where it is (overlap tolerated)
                if (TilePos* p = reg_->get<TilePos>(sl->entity)) { p->x = best->x; p->y = best->y; }
                SimEvent mv;
                mv.kind = SimEvent::Kind::TileMoved;
                mv.actor = id;
                mv.tile = *best;
                out.push_back(mv);
            }
        }
    }
    SimEvent fe;
    fe.kind = SimEvent::Kind::Footprint;
    fe.actor = id;
    fe.tile = pos(id);
    fe.rules.a = after;
    out.push_back(fe);
}

bool Combat::set_altitude(CombatantId id, int16_t alt_ft, uint8_t cause) {
    for (auto& c : state_.combatants) {
        if (c.id != id) continue;
        if (c.alt_ft == alt_ft) return false;
        c.alt_ft = alt_ft;
        SimEvent e;
        e.kind = SimEvent::Kind::AltitudeChanged;
        e.actor = id;
        e.tile = pos(id);
        e.rules.a = int32_t(uint16_t(alt_ft));
        e.rules.b = cause;
        events_.push_back(e);
        return true;
    }
    return false;
}
} // namespace eng::sim
