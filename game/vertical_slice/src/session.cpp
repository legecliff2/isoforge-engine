#include "session.hpp"

#include "event_feed.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "assets/pack_set.hpp"
#include "ecs/registry.hpp"
#include "rules5e/encounter.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "sim/driver.hpp"
#include "sim/light.hpp"
#include "world/file_chunk_provider.hpp"

#include <span>

namespace slice {

using namespace eng;

TileKind classify_tile(const std::string& id) {
    auto has = [&](const char* s) { return id.find(s) != std::string::npos; };
    if (id.empty()) return TileKind::Unknown;
    if (has("wall")) return TileKind::Wall;
    if (has("door")) return TileKind::Door;
    if (has("rubble")) return TileKind::Rubble;
    if (has("raised")) return TileKind::Raised;
    if (has("water")) return TileKind::Water;
    if (has("floor")) return TileKind::Floor;
    return TileKind::Unknown;
}

namespace {
std::string display_name(const std::string& archetype) {
    std::string s = archetype.substr(archetype.rfind('.') + 1);   // "fighter_1"
    if (auto u = s.find('_'); u != std::string::npos) s = s.substr(0, u);
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}
} // namespace

// sim::LightFrame -> plain-data LightView (packed samples, flattened ids). No light is computed here.
static void copy_light(const sim::LightFrame& f, const sim::LightFrame* prev, LightView& L) {
    L.present = true;
    L.frame = f.step;
    for (const sim::ChunkLightMap& m : f.chunks) {
        LightChunkView c;
        c.cx = m.coord.x; c.cy = m.coord.y; c.level = m.coord.level;
        for (size_t i = 0; i < m.samples.size(); ++i)
            c.samples[i] = uint8_t((uint8_t(m.samples[i].level) & 3u) | ((m.samples[i].flags & 0x0Fu) << 2));
        c.has_fog = false;   // LightFrame carries no visibility masks yet (contract gap)
        L.chunks.push_back(c);
    }
    auto has = [](const sim::LightFrame* fr, const world::ChunkCoord& k) {
        if (!fr) return false;
        for (const auto& m : fr->chunks) if (m.coord == k) return true;
        return false;
    };
    for (const auto& m : f.chunks) if (!has(prev, m.coord)) L.added.push_back({m.coord.x, m.coord.y, m.coord.level});
    if (prev) for (const auto& m : prev->chunks) if (!has(&f, m.coord)) L.removed.push_back({m.coord.x, m.coord.y, m.coord.level});
    for (const auto& k : f.dirty_chunks) L.dirty.push_back({{k.x, k.y, k.level}, eng::RectI{0, 0, 32, 32}});
    for (const sim::LightSource& s : f.sources) {
        LightSourceView v;
        v.attached = s.id.kind == sim::LightKind::Attached;
        v.cx = s.id.cx; v.cy = s.id.cy; v.level = s.id.level; v.index = s.id.index;
        v.tile = s.tile;
        v.carrier = s.carrier == sim::Carrier::Creature ? 1 : s.carrier == sim::Carrier::Item ? 2 : 0;
        v.carrier_id = s.carrier_id;
        v.bright_ft = s.bright_ft; v.dim_ft = s.dim_ft;
        v.lit = s.lit;
        v.magical = (s.flags & sim::light_src::kMagical) != 0;
        v.sunlight = (s.flags & sim::light_src::kSunlight) != 0;
        v.preset = s.preset;
        L.sources.push_back(std::move(v));
    }
}

struct Session::Impl {
    Options opt;
    std::unique_ptr<world::Chunk> chunk;
    std::unique_ptr<sim::ContentLibrary> content;
    std::unique_ptr<ecs::Registry> reg;
    std::optional<sim::Combat> combat;
    sim::NearestEnemyPolicy policy;
    uint16_t round = 0, turn = 0xFFFF;
    std::optional<sim::CombatantId> target;
    size_t events_seen = 0;
    FeedState feed;
    std::string error;
    std::vector<TileView> tiles;
    sim::LightCache light_cache;
    std::shared_ptr<const sim::LightFrame> light, light_prev;
    uint64_t light_step = 0;

    // Publishes the sim's LightFrame for the current committed state (start of the next step).
    void publish_light() {
        sim::LightWorld w;
        w.grid = &combat->grid();
        uint32_t next_id = 1;
        if (opt.pc_torches)
            for (const rules5e::Combatant& cb : combat->state().combatants) {
                const sim::Archetype* a = combat->archetype(cb.id);
                if (!a || a->schema_type != "character" || cb.dead) continue;
                sim::AttachedLight l;
                l.id = next_id++;
                l.carrier = sim::Carrier::Creature;
                l.carrier_id = cb.id;
                l.bright_ft = 20;
                l.dim_ft = 20;
                l.flags = sim::light_src::kLit;
                w.sources.push_back(sim::attached_source(
                    l, sim::carrier_centre(combat->pos(cb.id), combat->footprint_size(cb.id)), "srd51.core.light.torch"));
            }
        w.normalise();
        light_cache.begin_step(std::move(w));
        const world::ChunkCoord coords[] = {chunk->coord};
        light_prev = light;
        light = sim::publish_light_frame(light_cache, coords, ++light_step, light_prev.get());
    }

    std::string name_of(sim::CombatantId id) const {
        const sim::Archetype* a = combat ? combat->archetype(id) : nullptr;
        return a ? display_name(a->id) : "?";
    }
    // Feeds only newly *committed* events (Combat::events() never holds provisional ones).
    void drain_events() {
        const auto& evs = combat->events();
        if (events_seen >= evs.size()) return;
        apply_events(std::span<const sim::SimEvent>(evs).subspan(events_seen), feed,
                     [this](uint32_t id) { return name_of(id); });
        events_seen = evs.size();
    }
};

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;

bool Session::apply_pack_config(const std::string& text, Options& o, std::string* error) {
    auto fail = [&](std::string e) { if (error) *error = std::move(e); return false; };
    assets::Json j;
    try {
        j = assets::Json::parse(text);
    } catch (const std::exception& ex) {
        return fail(std::string("pack config: ") + ex.what());
    }
    if (!j.is_object()) return fail("pack config: not an object");
    for (const char* key : {"pack_roots", "packs"}) {
        if (!j.contains(key)) continue;
        const auto& a = j[key];
        if (!a.is_array()) return fail(std::string("pack config: ") + key + " must be an array of strings");
        std::vector<std::string> v;
        for (const auto& e : a) {
            if (!e.is_string()) return fail(std::string("pack config: ") + key + " must be an array of strings");
            v.push_back(e.get<std::string>());
        }
        (std::string(key) == "packs" ? o.packs : o.pack_roots) = std::move(v);
    }
    return true;
}

std::unique_ptr<Session> Session::open(const Options& o, std::string* error) {
    std::unique_ptr<Session> s(new Session());
    Impl& m = *s->impl_;
    m.opt = o;
    auto fail = [&](std::string e) { if (error) *error = std::move(e); return nullptr; };

    // Ordered pack set: content (and, later, scripts/spells) all come from it.
    if (o.packs.empty()) return fail("no --pack requested");
    std::vector<std::filesystem::path> roots(o.pack_roots.begin(), o.pack_roots.end());
    const assets::PackSet set = assets::PackSet::resolve(roots, o.packs);
    if (!set.ok()) return fail("pack set: " + set.error_strings().front());
    const assets::PackInfo* chunk_pack = set.find(o.packs.front());
    if (!chunk_pack) return fail("pack set: '" + o.packs.front() + "' did not resolve");

    // Chunk through the world module.
    const world::ChunkCoord coord{0, 0, 0};
    world::FileChunkProvider provider({{coord, chunk_pack->dir / "chunks/0_0_0.json"}});
    world::ChunkResult r = provider.load(world::ChunkRequest{coord, o.seed, 1});
    if (r.status != world::ChunkStatus::Ready || !r.chunk) return fail("chunk load failed: " + r.error);
    m.chunk = std::make_unique<world::Chunk>(std::move(*r.chunk));

    m.content = std::make_unique<sim::ContentLibrary>();
    auto errs = m.content->load(set);
    if (!errs.empty()) return fail("content load failed: " + errs.front());

    for (int layer = 0; layer < world::kChunkLayers; ++layer)
        for (int i = 0; i < world::kChunkSize * world::kChunkSize; ++i) {
            const world::TileCell& c = m.chunk->layers[size_t(layer)][size_t(i)];
            if (c.tile == 0 || c.tile >= m.chunk->palette.size()) continue;
            TileView t;
            t.tile = {i % world::kChunkSize, i / world::kChunkSize};
            t.layer = uint8_t(layer);
            t.elevation = c.elevation;
            t.kind = classify_tile(m.chunk->palette[c.tile]);
            m.tiles.push_back(t);
        }

    if (o.with_sim) {
        m.reg = std::make_unique<ecs::Registry>();
        std::string err;
        m.combat = sim::Combat::create(*m.reg, *m.chunk, *m.content, o.seed, &err);
        if (!m.combat) return fail("sim create failed: " + err);
        for (const rules5e::Combatant& cb : m.combat->state().combatants) m.feed.tiles[cb.id] = m.combat->pos(cb.id);
        m.drain_events();
        m.publish_light();
    }
    return s;
}

bool Session::step() {
    Impl& m = *impl_;
    if (!m.combat || m.combat->ended() || !m.error.empty()) return false;
    sim::Combat& c = *m.combat;
    const sim::ReactionDeciderFn decider = [&](const sim::Combat& cc, const sim::NeedsDecision& n) {
        return m.policy.decide_reaction(cc, n);
    };
    sim::SubmitResult r;
    if (c.pending()) {
        // The previous step paused on a reaction window: answer it now (AI decides).
        r = c.settle(decider);
    } else {
        // Same loop body as sim::run_to_end, one command per call, but a NeedsDecision pause is
        // left visible for one step instead of being settled immediately.
        const sim::CombatantId self = rules5e::current_actor(c.state());
        if (c.state().round != m.round || c.state().turn_index != m.turn) {
            m.round = c.state().round;
            m.turn = c.state().turn_index;
            m.target = m.policy.choose_target(c, self);
        }
        r = c.submit(sim::next_command(c, m.target));
    }
    if (!r.ok) m.error = r.error;
    m.drain_events();   // a paused request appended nothing; its events arrive when it completes
    if (!c.pending()) m.publish_light();   // light follows committed state only
    return r.ok;
}

bool Session::paused() const { return impl_->combat && impl_->combat->pending().has_value(); }

bool Session::ended() const { return impl_->combat && impl_->combat->ended(); }
const std::string& Session::error() const { return impl_->error; }

ViewModel Session::view() const {
    const Impl& m = *impl_;
    ViewModel v;
    v.tiles = m.tiles;
    v.log = m.feed.log;
    if (m.combat) {
        if (auto n = m.combat->pending()) v.pending = "WAITING: " + m.name_of(n->reactor) + " REACTION?";
    }
    if (m.combat) {
        const sim::Combat& c = *m.combat;
        v.sim_live = true;
        v.round = c.state().round;
        v.ended = c.ended();
        const sim::CombatantId cur = c.ended() ? 0 : rules5e::current_actor(c.state());
        for (const rules5e::Combatant& cb : c.state().combatants) {
            ActorView a;
            a.id = cb.id;
            const sim::Archetype* arch = c.archetype(cb.id);
            a.name = arch ? display_name(arch->id) : "?";
            a.is_pc = arch && arch->schema_type == "character";
            // Tile from the committed event stream (v3: readied moves, TileMoved after rules events).
            auto it = m.feed.tiles.find(cb.id);
            a.tile = it != m.feed.tiles.end() ? it->second : c.pos(cb.id);
            a.hp = cb.hp; a.hp_max = cb.hp_max; a.team = cb.team;
            a.current = cb.id == cur;
            a.dead = cb.dead;
            a.down = !cb.dead && !rules5e::is_conscious(cb);
            if (a.current) v.turn = a.name;
            v.actors.push_back(std::move(a));
        }
        std::sort(v.actors.begin(), v.actors.end(), [](const ActorView& x, const ActorView& y) { return x.id < y.id; });
    } else {
        for (const world::EntitySpawn& sp : m.chunk->spawns) {
            ActorView a;
            a.id = sp.stable_id;
            a.name = display_name(sp.archetype);
            a.tile = sp.tile;
            if (const sim::Archetype* arch = m.content->find(sp.archetype)) {
                a.hp = a.hp_max = arch->base.hp_max;
                a.is_pc = arch->schema_type == "character";
            }
            a.team = a.is_pc ? 0 : 1;
            v.actors.push_back(std::move(a));
        }
    }
    if (m.light) copy_light(*m.light, m.light_prev.get(), v.light);
    if (!m.error.empty()) v.status = "SIM ERROR: " + m.error;
    return v;
}

} // namespace slice
