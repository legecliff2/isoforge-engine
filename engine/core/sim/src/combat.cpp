#include "sim/combat.hpp"

#include <algorithm>
#include <cstdlib>

#include "assets/schema_registry.hpp"
#include "base/canonical_hash.hpp"
#include "rules5e/conditions.hpp"

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
    // Single-chunk combat keeps the v0 chunk-local frame (local == world tile), whatever the
    // chunk's coord: goldens and existing callers are unchanged.
    world::Chunk local = chunk;
    local.coord.x = 0;
    local.coord.y = 0;
    const world::Chunk* one[] = {&local};
    return create(reg, std::span<const world::Chunk* const>(one), content, seed, error);
}

std::optional<Combat> Combat::create(ecs::Registry& reg, std::span<const world::Chunk* const> chunks_in,
                                     const ContentLibrary& content, uint64_t seed, std::string* error) {
    // Canonical chunk order (coord), so callers' order (streamer, threads) cannot matter.
    std::vector<const world::Chunk*> chunks(chunks_in.begin(), chunks_in.end());
    std::sort(chunks.begin(), chunks.end(), [](const world::Chunk* a, const world::Chunk* b) { return a->coord < b->coord; });
    Grid grid{std::span<const world::Chunk* const>(chunks)};
    if (!grid.ok()) { if (error) *error = grid.error(); return std::nullopt; }
    struct WSpawn { const world::EntitySpawn* sp; Vec2i tile; };
    std::vector<WSpawn> spawns;
    for (const world::Chunk* ch : chunks)
        for (const auto& sp : ch->spawns) spawns.push_back({&sp, Grid::to_world(ch->coord, sp.tile)});
    Combat c(reg, std::move(grid));
    c.seed_ = seed;
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
        cs.push_back(std::move(k));
    }
    std::vector<rules5e::Event> opening;
    if (!content.spells()->all().empty()) c.spells_ = content.spells();
    c.state_ = rules5e::begin_encounter(std::move(cs), seed, &opening);
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
    const TilePos* p = reg_->get<TilePos>(s->entity);
    return p ? Vec2i{p->x, p->y} : Vec2i{};
}
std::optional<uint8_t> Combat::multiattack_option(CombatantId id, std::string_view option_id) const {
    const auto* c = rules5e::find(state_, id);
    if (!c) return std::nullopt;
    for (std::size_t i = 0; i < c->multiattacks.size() && i < 255; ++i)
        if (c->multiattacks[i].id == option_id) return uint8_t(i + 1);
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
    if (distance_tiles(caster, target) * kFeetPerTile > int(def.range_ft)) return false;
    return caster == target || line_of_sight(caster, target);
}

int Combat::footprint_size(CombatantId id) const {
    const Slot* s = slot(id);
    return s ? int(s->archetype->footprint) : 1;
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

std::vector<Vec2i> Combat::occupied_except(CombatantId id) const {
    std::vector<Vec2i> out;
    for (const auto& s : slots_) {
        const auto* c = rules5e::find(state_, s.id);
        if (s.id == id || !c || c->dead) continue;
        if (s.archetype->footprint == 1) { out.push_back(pos(s.id)); continue; }
        for (Vec2i t : footprint_tiles(s.id)) out.push_back(t);
    }
    return out;
}

void Combat::apply_rules_events(const std::vector<rules5e::Event>& evs) {
    for (const auto& e : evs) {
        SimEvent se;
        se.kind = SimEvent::Kind::Rules;
        se.rules = e;
        events_.push_back(se);
    }
    sync_ecs();
}

void Combat::sync_ecs() {
    for (const auto& s : slots_)
        if (const auto* c = rules5e::find(state_, s.id))
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
    if (spells_) {
        state_.hooks.can_provide_components = [](const rules5e::EncounterState&, const rules5e::Combatant&,
                                                 const rules5e::SpellDef& d) { return d.material_cost_cp == 0; };
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
                const Archetype* arch = archetype(ra.actor);
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

bool Combat::can_use_feature(CombatantId id, uint32_t fid) const {
    const ActiveFeature* f = find_active(archetype(id), fid);
    if (!f || state_.ended || state_.pending || rules5e::current_actor(state_) != id) return false;
    if (feature_uses_left(id, fid) == 0) return false;
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
void Combat::complete(const rules5e::LoggedStep& ls, const std::vector<rules5e::Event>& evs, std::optional<Vec2i> to) {
    const auto& q = ls.request;
    apply_rules_events(evs);
    std::vector<CombatantId> fired;
    for (const auto& e : evs) {
        if (e.kind == rules5e::EventKind::Moved) {
            if (e.reaction_depth == 0 && q.kind == ActionKind::Move && e.actor == q.actor && to) move_tile(q.actor, *to, evs);
            else if (e.reaction_depth > 0 && std::find(fired.begin(), fired.end(), e.actor) != fired.end())
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
    for (const auto& e : evs)
        if (e.kind == rules5e::EventKind::ReadyTriggered || e.kind == rules5e::EventKind::ReadyExpired) readied_dest_.erase(e.actor);
    if (q.kind == ActionKind::Ready && q.ready_kind == ActionKind::Move && to) readied_dest_[q.actor] = *to;
    step_to_.reset();
    ready_to_.reset();
}

bool Combat::ranged_attack_facts(CombatantId actor, uint8_t ai, Spot from, Spot tp, ActionRequest& req,
                                 std::string* error) const {
    const Archetype* arch = archetype(actor);
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
    complete(ls, sr.events, to);
    log_.push_back({ls, to});
    r.reactions = std::move(sr.reactions);
    return r;
}

SubmitResult Combat::submit(const Command& cmd) {
    SubmitResult r;
    if (state_.pending) { r.error = "a reaction window is waiting for a decision"; r.needs = pending(); return r; }
    if (state_.ended) { r.error = "encounter ended"; return r; }
    install_hooks();
    return cmd.kind == CommandKind::Step ? do_step(cmd.to) : do_rules(cmd.request, cmd.to, cmd.aim);
}

SubmitResult Combat::decide(const ReactionDecision& d) {
    SubmitResult r;
    const auto nd = pending();
    if (!nd) { r.error = "no pending reaction window"; return r; }
    if (d.window_seq != nd->window_seq) { r.error = "decision for a different window"; r.needs = nd; return r; }
    install_hooks();
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
    if (req.kind == ActionKind::OpportunityAttack) { r.error = "opportunity attacks come from reaction windows"; return r; }
    if (req.kind == ActionKind::ReactionDecision) { r.error = "use decide() for reaction decisions"; return r; }
    if (req.kind == ActionKind::UseFeature) {   // parameters come from content, not the caller
        const ActiveFeature* f = find_active(archetype(actor), req.feature_id);
        if (!f) { r.error = "no such active feature"; return r; }
        if (feature_uses_left(actor, req.feature_id) == 0) { r.error = "feature has no uses left"; return r; }
        req.as_bonus_action = f->cost == FeatureCost::BonusAction;   // rules read FeatureSpec::cost when a spec exists
        req.heal_dice = f->heal;   // zero unless the hook set a heal
    }
    if (req.kind == ActionKind::Attack) {
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
        if (aim) {   // area template: derive the targets from the aim tile (opt-in; an explicit list wins)
            const auto* def = spells_->find(req.spell_id);
            if (def->area == rules5e::AreaShape::None || !req.targets.empty()) { r.error = "aim needs an area spell and no explicit targets"; return r; }
            std::string err;
            req.targets = area_targets(actor, *def, to, &err);
            if (req.targets.empty()) { r.error = err; return r; }
            req.target = req.targets[0];
            derived_area_ = true;
            aim_to_ = to;
        }
        const CombatantId first = !req.targets.empty() ? req.targets[0] : req.target;
        if (!rules5e::find(state_, first)) { r.error = "bad target"; return r; }
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
            const Archetype* arch = archetype(actor);
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
            const auto path = grid_.path(from, {to}, occupied_except(actor), fp);
            if (path.empty()) { r.error = "readied move destination unreachable"; return r; }
            int cost = 0;
            Vec2i prev = from;
            for (Vec2i p : path) { cost += grid_.move_cost_ft(prev, p, fp); prev = p; }
            if (cost > int(a->speed_ft)) { r.error = "readied move longer than speed"; return r; }
            req.move_ft = int16_t(cost);
            req.move_tiles = uint8_t(std::min<std::size_t>(path.size(), 255));   // path length (rules counts it only when it fires on the readier's own turn)
            req.leaves_reach = true;   // rules polls; sim's in_range decides who is actually left
            ready_to_ = to;
        } else {
            r.error = "only Attack or Move can be readied";
            return r;
        }
    }
    step_to_.reset();
    return finish(rules5e::step(state_, req));
}

SubmitResult Combat::do_step(Vec2i to) {
    SubmitResult r;
    const CombatantId mover = rules5e::current_actor(state_);
    const Vec2i from = pos(mover);
    if (chebyshev(from, to) != 1) { r.error = "step must be to an adjacent tile"; return r; }
    // Every tile the footprint newly enters must be walkable and free (the tiles it keeps are its own).
    const int fp = footprint_size(mover);
    const auto occ = occupied_except(mover);
    for (int dy = 0; dy < fp; ++dy)
        for (int dx = 0; dx < fp; ++dx) {
            const Vec2i t{to.x + dx, to.y + dy};
            if (t.x >= from.x && t.x < from.x + fp && t.y >= from.y && t.y < from.y + fp) continue;
            if (!grid_.walkable(t)) { r.error = "tile not walkable"; return r; }
            if (std::find(occ.begin(), occ.end(), t) != occ.end()) { r.error = "tile occupied"; return r; }
        }
    const int cost = grid_.move_cost_ft(from, to, fp);
    if (cost > state_.budget.movement_ft) { r.error = "not enough movement"; return r; }
    ActionRequest mv{mover, ActionKind::Move};
    mv.move_ft = int16_t(cost);
    mv.move_tiles = 1;
    mv.leaves_reach = leaves_reach(mover, from, to);
    step_to_ = to;
    return finish(rules5e::step(state_, mv));
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
    for (const auto& s : slots_) {
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
    // 0xA6: event triggers' once-per-turn / round bookkeeping (row 9a), only when some combatant has triggers.
    if (rules5e::has_trigger_state(state_)) { h.byte(0xA6); h.u64(rules5e::triggers_digest(state_)); }
    return h.value();
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
        if (q.bonus_option != 0) { h.byte(0xB0); h.byte(q.bonus_option); }   // R-G6 bonus attack option: folded only when set
        if (q.innate) h.byte(0xA4);                       // innate CastSpell: folded only when set, under a tag byte
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

std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content, uint64_t seed,
                             const std::vector<LogEntry>& log, std::string* error) {
    world::Chunk local = chunk;   // same chunk-local frame as the single-chunk create
    local.coord.x = 0;
    local.coord.y = 0;
    const world::Chunk* one[] = {&local};
    return replay(reg, std::span<const world::Chunk* const>(one), content, seed, log, error);
}

std::optional<Combat> replay(ecs::Registry& reg, std::span<const world::Chunk* const> chunks, const ContentLibrary& content,
                             uint64_t seed, const std::vector<LogEntry>& log, std::string* error) {
    auto c = Combat::create(reg, chunks, content, seed, error);
    if (!c) return std::nullopt;
    for (std::size_t i = 0; i < log.size(); ++i) {
        const LogEntry& e = log[i];
        c->install_hooks();
        c->step_to_.reset();
        c->ready_to_.reset();
        if (e.step.request.kind == ActionKind::Move) c->step_to_ = e.to;
        if (e.step.request.kind == ActionKind::Ready) c->ready_to_ = e.to;
        c->derived_area_ = e.step.request.kind == ActionKind::CastSpell && e.to;   // logged targets came from an aim tile
        rules5e::StepResult sr = rules5e::replay_step(c->state_, e.step);   // rules' replay; no decider
        if (!sr.ok || sr.desync || sr.status != StepStatus::Done) {
            if (error) *error = "replay desync at entry " + std::to_string(i) + ": " +
                                (sr.ok ? std::string("logged decisions do not match") : std::string(sr.error));
            return std::nullopt;
        }
        c->derived_area_ = false;
        c->complete(*sr.logged, sr.events, e.to);
        c->log_.push_back({*sr.logged, e.to});
    }
    return c;
}

} // namespace eng::sim
