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
#include "sim/equipment.hpp"
#include "sim/light.hpp"
#include "sim/light_driver.hpp"
#include "sim/visibility.hpp"
#include "rules5e/equipment.hpp"
#include "rules5e/item.hpp"
#include "rules5e/loadout.hpp"
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

// item@1 `visual` block (schema: schemas/item/v1.schema.json) -> render style. Validated content is
// assumed; anything malformed is skipped (keeps the built-in default) and reported in *why.
static bool parse_visual(const assets::Json& j, eng::iso2d::GlowStyle& out, std::string* why) {
    auto bad = [&](const char* w) { if (why) *why = w; return false; };
    if (!j.is_object() || !j.contains("color") || !j.contains("intensity")) return bad("visual needs color and intensity");
    const auto& c = j["color"];
    if (!c.is_object()) return bad("visual.color must be an object");
    eng::iso2d::GlowVisual v;
    uint8_t* ch[3] = {&v.r, &v.g, &v.b};
    const char* names[3] = {"r", "g", "b"};
    for (int i = 0; i < 3; ++i) {
        if (!c.contains(names[i]) || !c[names[i]].is_number_integer()) return bad("visual.color channels must be integers");
        const auto n = c[names[i]].get<int64_t>();
        if (n < 0 || n > 255) return bad("visual.color channel out of 0..255");
        *ch[i] = uint8_t(n);
    }
    if (!j["intensity"].is_number_integer()) return bad("visual.intensity must be an integer");
    const auto in = j["intensity"].get<int64_t>();
    if (in < 0 || in > 1000) return bad("visual.intensity out of 0..1000");
    v.intensity_permille = uint16_t(in);
    if (j.contains("flicker")) {
        const auto& f = j["flicker"];
        if (!f.is_string()) return bad("visual.flicker must be a string");
        const std::string fs = f.get<std::string>();
        if (fs == "none") v.flicker = eng::iso2d::GlowFlicker::None;
        else if (fs == "torch") v.flicker = eng::iso2d::GlowFlicker::Torch;
        else if (fs == "candle") v.flicker = eng::iso2d::GlowFlicker::Candle;
        else return bad("visual.flicker must be none|torch|candle");
    }
    out = eng::iso2d::glow_style_from_visual(v);
    return true;
}

// sim::LightFrame -> plain-data LightView (packed samples, flattened ids, the shown team's masks).
// No light or visibility is computed here: both come from the sim's frame.
static void copy_light(const sim::LightFrame& f, bool fog, uint8_t fog_team, LightView& L) {
    L.present = true;
    L.frame = f.step;
    L.fog_team = fog_team;
    L.fog = fog && !f.visibility.empty();
    const sim::TeamVisibility* team = nullptr;
    for (const sim::TeamVisibility& tv : f.visibility) if (tv.team == fog_team) team = &tv;
    auto ref = [](const world::ChunkCoord& k) { return LightChunkRef{k.x, k.y, k.level}; };
    for (const sim::ChunkLightMap& m : f.chunks) {
        LightChunkView c;
        c.cx = m.coord.x; c.cy = m.coord.y; c.level = m.coord.level;
        for (size_t i = 0; i < m.samples.size(); ++i)
            c.samples[i] = uint8_t((uint8_t(m.samples[i].level) & 3u) | ((m.samples[i].flags & 0x0Fu) << 2));
        c.has_fog = L.fog;
        if (L.fog && team)   // a chunk missing from the team's list stays all-zero: entirely unseen
            for (const sim::ChunkVisibility& cv : team->chunks)
                if (cv.coord == m.coord) { c.seen_now = cv.seen_now.words; c.explored = cv.explored.words; }
        L.chunks.push_back(c);
    }
    for (const auto& k : f.added_chunks) L.added.push_back(ref(k));
    for (const auto& k : f.removed_chunks) L.removed.push_back(ref(k));
    for (const auto& k : f.blocked_chunks) L.blocked.push_back(ref(k));
    for (const auto& k : f.dirty_vis_chunks) L.vis_dirty.push_back(ref(k));
    for (const sim::DirtyRect& d : f.dirty_rects)
        L.dirty.push_back({ref(d.coord), eng::RectI{d.x0, d.y0, d.x1 - d.x0 + 1, d.y1 - d.y0 + 1}});
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

// One equipment light -> attached source. Radii are the entry's current state (hooded lantern: 0/5), never the item
// default. Shape: a cone is published as a radius source (no facing exposed yet; LIGHTING.md 10.2 TODO).
sim::LightSource equipment_source(const sim::EquipmentLight& e, Vec2i holder_centre, std::string preset) {
    sim::AttachedLight l;
    l.id = e.light_id;
    l.bright_ft = e.bright_ft;
    l.dim_ft = e.dim_ft;
    l.state = e.state;
    l.flags = e.lit ? sim::light_src::kLit : uint8_t(0);
    Vec2i at = e.tile;
    if (e.holder_id != 0) {
        l.carrier = sim::Carrier::Creature;
        l.carrier_id = e.holder_id;
        at = holder_centre;
    } else {
        l.carrier = sim::Carrier::Tile;
        l.carrier_id = e.item;
        l.tile = e.tile;
    }
    return sim::attached_source(l, at, std::move(preset));
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
    // R-G13 phase 2: the sim publishes the frames (attach + every committed step); the game only supplies PC torches.
    std::unique_ptr<sim::LightDriver> light_driver;
    uint8_t fog_team = 0;
    std::shared_ptr<const sim::LightFrame> light, light_prev;
    std::map<std::string, eng::iso2d::GlowStyle, std::less<>> visuals;   // light preset -> glow style
    std::vector<std::string> warnings;   // load-time, non-fatal (e.g. conflicting visuals)

    // item content id -> light preset (`light.preset` when the item has one, else the item id itself), for equipment glows.
    std::map<std::string, std::string, std::less<>> light_presets;

    // Equipment producer: one attached source per light-capable item (Combat::equipment_lights(), light id order),
    // keyed by the sim's light id. Held or carried: Carrier::Creature on the holder (the glow follows the token); on the
    // ground (unequipped by drop / death / throw): Carrier::Tile at the item's tile. Unlit (stowed, never lit): lit=false,
    // so no glow and no light. Bright / dim are rules5e::light_radii(def, held) for the light_state_index (0 / 0 while
    // unlit or stowed: no glow); the preset only picks the visual.
    static void equipment_sources(const sim::Combat& c, const std::map<std::string, std::string, std::less<>>& presets,
                                  std::vector<sim::LightSource>& out) {
        const sim::ItemDefTable* table = c.item_defs();
        for (const sim::EquipmentLight& e : c.equipment_lights()) {
            const std::string* cid = table ? table->content_id(e.def) : nullptr;
            std::string preset;
            if (cid) {
                auto it = presets.find(*cid);
                preset = it != presets.end() ? it->second : *cid;
            }
            const Vec2i holder = e.holder_id ? sim::carrier_centre(c.pos(e.holder_id), c.footprint_size(e.holder_id)) : e.tile;
            out.push_back(equipment_source(e, holder, std::move(preset)));
        }
    }

    // Interim carried lights (pc_torches, then Options::carried_lights), as sim attached sources. Their ids live in
    // kInterimLightIdBase.. so they never collide with sim-allocated equipment light ids. Deterministic in the state.
    static void interim_sources(const Options& opt, const sim::Combat& c, std::vector<sim::LightSource>& out) {
        std::vector<CarriedLight> list;
        auto alive = [&](uint32_t id) {
            for (const rules5e::Combatant& cb : c.state().combatants)
                if (cb.id == id) return !cb.dead;
            return false;
        };
        if (opt.pc_torches)
            for (const rules5e::Combatant& cb : c.state().combatants) {
                const sim::Archetype* a = c.archetype(cb.id);
                if (!a || a->schema_type != "character" || cb.dead) continue;
                CarriedLight t;
                t.carrier_id = cb.id;
                list.push_back(t);
            }
        for (const CarriedLight& cl : opt.carried_lights)
            if (alive(cl.carrier_id)) list.push_back(cl);
        uint32_t next_id = kInterimLightIdBase;
        for (const CarriedLight& cl : list) {
            sim::AttachedLight l;
            l.id = next_id++;
            l.carrier = sim::Carrier::Creature;
            l.carrier_id = cl.carrier_id;
            l.bright_ft = cl.bright_ft;
            l.dim_ft = cl.dim_ft;
            l.flags = uint8_t(sim::light_src::kLit | (cl.magical ? sim::light_src::kMagical : 0));
            out.push_back(sim::attached_source(l, sim::carrier_centre(c.pos(cl.carrier_id), c.footprint_size(cl.carrier_id)),
                                               cl.preset));
        }
    }

    // Hands the attached-light list to the sim's LightDriver; the driver publishes frames on attach and
    // every committed step.
    void attach_light() {
        sim::LightProviderInputs in;
        in.chunks = {chunk.get()};
        // Producers of entity-attached sources, in a fixed order: equipment (sim light ids), then the interim ones.
        in.attached_sources = [this](const sim::Combat& c, std::vector<sim::LightSource>& out) {
            if (opt.equipment) equipment_sources(c, light_presets, out);
            interim_sources(opt, c, out);
        };
        light_driver = std::make_unique<sim::LightDriver>(std::move(in));
        combat->attach_light_driver(light_driver.get());
        pull_light();
    }
    // Picks up the driver's latest frame (new only after a committed step).
    void pull_light() {
        auto f = light_driver ? light_driver->latest() : nullptr;
        if (f == light) return;
        light_prev = light;
        light = std::move(f);
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

    // Presentation-only visuals (item@1 `visual`), read straight from the pack documents. The sim never
    // sees them (ContentLibrary doesn't load item@1), so they can't reach Combat::hash or log_hash.
    // Keyed by the item's `light.preset` (the preset its light uses), not by the item id. Items with a
    // `visual` but no `light.preset` are skipped (nothing to attach the style to; the preset default
    // stays). Several items styling one preset: lowest content id wins, deterministic in any pack
    // order, with a warning per differing loser.
    {
        std::map<std::string, std::map<std::string, eng::iso2d::GlowStyle>> by_preset;   // preset -> item id -> style
        for (const assets::PackDocument& d : set.documents()) {
            // Equipment glows: item id -> light.preset (any item with a light block that names one).
            if (d.doc.ok && d.doc.data.is_object() && d.doc.current.type == "item" && !d.content_id.empty() &&
                d.doc.data.contains("light") && d.doc.data["light"].is_object() && d.doc.data["light"].contains("preset") &&
                d.doc.data["light"]["preset"].is_string())
                m.light_presets.emplace(d.content_id, d.doc.data["light"]["preset"].get<std::string>());
            if (!d.doc.ok || !d.doc.data.is_object() || !d.doc.data.contains("visual")) continue;
            if (d.doc.current.type != "item" || d.content_id.empty()) continue;
            const auto& j = d.doc.data;
            if (!j.contains("light") || !j["light"].is_object() || !j["light"].contains("preset") ||
                !j["light"]["preset"].is_string() || j["light"]["preset"].get<std::string>().empty()) {
                m.warnings.push_back("item " + d.content_id + ": visual ignored (no light.preset)");
                continue;
            }
            eng::iso2d::GlowStyle st;
            std::string why;
            if (!parse_visual(j["visual"], st, &why)) {
                m.warnings.push_back("item " + d.content_id + ": visual ignored (" + why + ")");
                continue;
            }
            by_preset[j["light"]["preset"].get<std::string>()][d.content_id] = st;
        }
        auto same = [](const eng::iso2d::GlowStyle& a, const eng::iso2d::GlowStyle& b) {
            return a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b && a.color.a == b.color.a &&
                   a.intensity == b.intensity && a.flicker == b.flicker;
        };
        for (const auto& [preset, items] : by_preset) {
            const auto& [winner, style] = *items.begin();   // lowest content id
            for (auto it = std::next(items.begin()); it != items.end(); ++it)
                if (!same(it->second, style))
                    m.warnings.push_back("light preset " + preset + ": conflicting visual from item " + it->first +
                                         " ignored; using " + winner + " (lowest content id)");
            m.visuals[preset] = style;
        }
    }

    m.content = std::make_unique<sim::ContentLibrary>();
    auto errs = m.content->load(set);
    if (!errs.empty()) return fail("content load failed: " + errs.front());
    if (o.content_hook) o.content_hook(*m.content);

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
        m.combat = sim::Combat::create(*m.reg, *m.chunk, *m.content, o.seed, sim::CombatOptions{o.equipment}, &err);
        if (!m.combat) return fail("sim create failed: " + err);
        for (const rules5e::Combatant& cb : m.combat->state().combatants) m.feed.tiles[cb.id] = m.combat->pos(cb.id);
        m.drain_events();
        m.fog_team = 0;
        for (const rules5e::Combatant& cb : m.combat->state().combatants) {
            const sim::Archetype* a = m.combat->archetype(cb.id);
            if (a && a->schema_type == "character") { m.fog_team = cb.team; break; }
        }
        if (o.fog_team) m.fog_team = *o.fog_team;
        m.attach_light();
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
    m.pull_light();   // the driver publishes on committed steps only
    return r.ok;
}

bool Session::submit_equip(uint32_t actor, uint8_t op, uint32_t item, uint8_t hand, uint8_t state) {
    Impl& m = *impl_;
    if (!m.combat || m.combat->ended() || m.combat->pending() || !m.error.empty()) return false;
    if (rules5e::current_actor(m.combat->state()) != actor) return false;
    rules5e::ActionRequest q{actor, rules5e::ActionKind::UseObject, 0};
    q.equip_op = op;
    q.equip_item = item;
    q.equip_hand = hand;
    q.equip_state = state;
    sim::Command c;
    c.request = q;
    const sim::SubmitResult r = m.combat->submit(c);
    if (!r.ok) return false;
    m.drain_events();
    m.pull_light();
    return true;
}

bool Session::end_turns_until(uint32_t actor) {
    Impl& m = *impl_;
    for (int i = 0; i < 16 && m.combat && !m.combat->ended() && rules5e::current_actor(m.combat->state()) != actor; ++i) {
        sim::Command c;
        c.request = rules5e::ActionRequest{rules5e::current_actor(m.combat->state()), rules5e::ActionKind::EndTurn, 0};
        if (!m.combat->submit(c).ok) return false;
        m.drain_events();
        m.pull_light();
    }
    return m.combat && rules5e::current_actor(m.combat->state()) == actor;
}

std::vector<Session::EquippedLight> Session::equipment_lights() const {
    std::vector<EquippedLight> out;
    if (!impl_->combat) return out;
    for (const sim::EquipmentLight& e : impl_->combat->equipment_lights())
        out.push_back({e.light_id, e.item, e.holder_id, e.tile, e.lit, e.state, e.bright_ft, e.dim_ft});
    return out;
}

bool Session::paused() const { return impl_->combat && impl_->combat->pending().has_value(); }

bool Session::ended() const { return impl_->combat && impl_->combat->ended(); }
const std::vector<std::string>& Session::warnings() const { return impl_->warnings; }
const std::string& Session::error() const { return impl_->error; }

// Loadout -> EquipVisual list (read-only, through sim::Combat::loadout / item_defs): main hand, off hand (a two-handed
// item, held in both, once at the main hand), then carried weapons / shields slung on the back (instance order).
// Lit state and radii are rules5e::light_radii, the same values the light producer publishes.
static void equipment_visuals(const sim::Combat& c, sim::CombatantId id, std::vector<EquipVisual>& out) {
    out.clear();
    const rules5e::Loadout* lo = c.loadout(id);
    const sim::ItemDefTable* table = c.item_defs();
    if (!lo || !table) return;
    auto kind_of = [](const rules5e::ItemDef& d) {
        switch (d.kind) {
        case rules5e::ItemKind::Weapon: return EquipKind::Weapon;
        case rules5e::ItemKind::Shield: return EquipKind::Shield;
        case rules5e::ItemKind::Light: return EquipKind::Light;
        default: return d.light.states.empty() ? EquipKind::Other : EquipKind::Light;
        }
    };
    auto add = [&](const rules5e::HeldItem& h, AttachSlot slot) {
        const rules5e::ItemDef* d = table->find(h.def);
        if (h.instance == 0 || !d) return;
        EquipVisual e;
        e.item = h.instance;
        e.slot = slot;
        e.kind = kind_of(*d);
        e.hands = d->hands;
        const rules5e::LightState r = rules5e::light_radii(*d, h);
        e.lit = (h.flags & rules5e::kHeldLit) != 0;
        e.light_state = rules5e::light_state_index(h.flags);
        e.bright_ft = r.bright_ft;
        e.dim_ft = r.dim_ft;
        if (const std::string* cid = table->content_id(h.def)) e.content_id = *cid;
        out.push_back(std::move(e));
    };
    add(lo->view.main, AttachSlot::MainHand);
    if (lo->view.off.instance != lo->view.main.instance) add(lo->view.off, AttachSlot::OffHand);
    std::vector<rules5e::HeldItem> slung;
    for (const rules5e::HeldItem& h : lo->view.carried) {
        const rules5e::ItemDef* d = table->find(h.def);
        if (d && (d->kind == rules5e::ItemKind::Weapon || d->kind == rules5e::ItemKind::Shield)) slung.push_back(h);
    }
    std::sort(slung.begin(), slung.end(), [](const auto& x, const auto& y) { return x.instance < y.instance; });
    for (const rules5e::HeldItem& h : slung) add(h, AttachSlot::Back);
}

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
            equipment_visuals(c, cb.id, a.equipment);
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
    if (m.light) copy_light(*m.light, m.opt.fog, m.fog_team, v.light);
    v.light.visuals = m.visuals;
    if (!m.error.empty()) v.status = "SIM ERROR: " + m.error;
    return v;
}

} // namespace slice
