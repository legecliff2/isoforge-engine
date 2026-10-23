#include "sim/light_driver.hpp"

#include <algorithm>
#include <utility>

#include "rules5e/spells.hpp"
#include "sim/combat.hpp"
#include "world/chunk_streamer.hpp"

namespace eng::sim {

namespace {
world::ChunkCoord add(world::ChunkCoord c, world::ChunkCoord off) { return {c.x + off.x, c.y + off.y, c.level}; }

// Flags that affect light composition or occlusion (LIGHTING.md §4). Other zone flags (difficult, silence, …) are ignored.
constexpr uint8_t kLightZoneFlags = rules5e::kZoneLightObscured | rules5e::kZoneHeavyObscured | rules5e::kZoneMagicalDarkness |
                                    rules5e::kZoneOpaque;

// Fill `lz.tiles` with every tile Combat::zone_covers reports for `z` (Chebyshev sphere/cylinder, centred cube; walls /
// rings cover nothing today). Bounding box matches zone_covers so the loop is exact, not approximate.
void fill_zone_tiles(const Combat& combat, const rules5e::ActiveZone& z, const rules5e::SpellDef& def, LightZone& lz) {
    if (z.ring) return;
    const int sz = int(z.size_ft != 0 ? z.size_ft : def.zone.size_ft) / kFeetPerTile;
    int min_x = 0, max_x = -1, min_y = 0, max_y = -1;
    if (def.zone.anchor == rules5e::ZoneAnchor::Creature) {
        if (!rules5e::find(combat.state(), z.owner)) return;
        const Vec2i op = combat.pos(z.owner);
        const int on = combat.footprint_size(z.owner);
        const int pad = (def.zone.shape == rules5e::AreaShape::Cube) ? sz / 2 : sz;
        min_x = op.x - pad;
        max_x = op.x + on - 1 + pad;
        min_y = op.y - pad;
        max_y = op.y + on - 1 + pad;
    } else {
        if (!z.has_anchor_tile) return;
        const Vec2i a{z.anchor_x, z.anchor_y};
        if (def.zone.shape == rules5e::AreaShape::Sphere || def.zone.shape == rules5e::AreaShape::Cylinder) {
            min_x = a.x - sz;
            max_x = a.x + sz;
            min_y = a.y - sz;
            max_y = a.y + sz;
        } else if (def.zone.shape == rules5e::AreaShape::Cube) {
            const int lo = -((sz - 1) / 2), hi = sz / 2;
            min_x = a.x + lo;
            max_x = a.x + hi;
            min_y = a.y + lo;
            max_y = a.y + hi;
        } else {
            return;   // walls (Line) are not modelled yet (Combat::zone_covers)
        }
    }
    for (int y = min_y; y <= max_y; ++y)
        for (int x = min_x; x <= max_x; ++x)
            if (combat.zone_covers(z, {x, y})) lz.tiles.push_back({x, y});
}

void append_active_zones(const Combat& combat, std::vector<LightZone>& out) {
    for (const rules5e::ActiveZone& z : combat.state().zones) {
        const rules5e::SpellDef* d = combat.spell_def(z.spell_id);
        if (!d || d->kind != rules5e::SpellEffectKind::Zone) continue;
        if ((d->zone.flags & kLightZoneFlags) == 0) continue;
        LightZone lz;
        lz.zone_id = z.zone_id;
        lz.flags = d->zone.flags;
        lz.spell_level = z.slot_level;
        fill_zone_tiles(combat, z, *d, lz);
        out.push_back(std::move(lz));
    }
}
} // namespace

std::vector<Viewer> combat_viewers(const Combat& combat) {
    std::vector<Viewer> out;
    for (const rules5e::Combatant& cb : combat.state().combatants) {
        if (cb.dead || cb.gone) continue;
        Viewer v;
        v.id = cb.id;
        v.team = cb.team;
        v.anchor = combat.pos(cb.id);
        v.footprint = combat.footprint_size(cb.id);
        v.tile = carrier_centre(v.anchor, v.footprint);
        v.senses = senses_of(cb);
        v.blinded = cb.has(rules5e::Condition::Blinded);
        out.push_back(v);
    }
    std::sort(out.begin(), out.end(), [](const Viewer& a, const Viewer& b) { return a.id < b.id; });
    return out;
}

LightDriver::LightDriver(LightProviderInputs in) : in_(std::move(in)) {}

LightDriver::~LightDriver() {
    if (pinned_ && in_.streamer) in_.streamer->unpin(in_.pin_owner);
}

std::shared_ptr<const LightFrame> LightDriver::latest() const {
    std::lock_guard<std::mutex> lk(mu_);
    return latest_;
}

std::shared_ptr<const LightFrame> LightDriver::step(const Combat& combat) {
    const world::ChunkCoord off = combat.frame_offset();
    const std::vector<world::ChunkCoord>& frame = combat.frame_chunks();
    const int16_t level = frame.empty() ? int16_t(0) : frame.front().level;
    const Vec2i tile_off{off.x * world::kChunkSize, off.y * world::kChunkSize};
    const std::vector<Viewer> viewers = combat_viewers(combat);

    // Streaming: pin the combat's chunks while it runs (halos follow from the streamer), foci = creatures then camera.
    if (in_.streamer) {
        if (!combat.ended() && !pinned_) {
            std::vector<world::ChunkCoord> w;
            for (const auto& c : frame) w.push_back(add(c, off));
            in_.streamer->pin(in_.pin_owner, w);
            pinned_ = true;
        } else if (combat.ended() && pinned_) {
            in_.streamer->unpin(in_.pin_owner);
            pinned_ = false;
        }
        std::vector<world::Focus> foci;
        for (const Viewer& v : viewers) foci.push_back(world::Focus{{v.tile.x + tile_off.x, v.tile.y + tile_off.y}, level, 1});
        if (in_.camera_tile) foci.push_back(world::Focus{*in_.camera_tile, level, 1});
        in_.streamer->set_foci(foci);
    }

    // Halo gate (§7): only Complete halos are evaluated; Pending blocks, Failed is an error state.
    std::vector<world::ChunkCoord> ready, blocked;
    split_by_halo(frame, [&](world::ChunkCoord c) {
        return in_.streamer ? in_.streamer->halo_state(add(c, off)) : world::HaloState::Complete;
    }, ready, blocked);
    pending_ = false;
    failed_.clear();
    for (const auto& c : blocked) {
        const world::HaloState hs = in_.streamer->halo_state(add(c, off));
        if (hs == world::HaloState::Failed) failed_.push_back(add(c, off));
        else pending_ = true;
    }

    auto chunk_at = [&](world::ChunkCoord wc) -> const world::Chunk* {
        if (in_.streamer) return in_.streamer->get(wc);
        for (const world::Chunk* ch : in_.chunks)
            if (ch && ch->coord == wc) return ch;
        return nullptr;
    };

    LightWorld w;
    w.grid = &combat.grid();
    // Static chunk@2 lights of every resident chunk in the ready chunks' 3x3 halos (kMaxLightFt < kChunkSize).
    std::vector<world::ChunkCoord> halo;
    for (const auto& c : ready)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) halo.push_back(world::ChunkCoord{c.x + dx, c.y + dy, c.level});
    std::sort(halo.begin(), halo.end());
    halo.erase(std::unique(halo.begin(), halo.end()), halo.end());
    for (const auto& fc : halo) {
        const world::ChunkCoord wc = add(fc, off);
        const world::Chunk* ch = chunk_at(wc);
        if (!ch) continue;   // known absent (no sources) or outside a non-streamed world
        for (std::size_t i = 0; i < ch->lights.size(); ++i) {
            const world::Chunk::Light& cl = ch->lights[i];
            StaticLight sl;
            sl.tile = cl.tile;
            sl.bright_ft = cl.bright_ft;
            sl.dim_ft = cl.dim_ft;
            sl.flags = cl.flags;
            sl.stable_id = cl.stable_id;
            LightSource s = static_source(wc, uint32_t(i), sl, cl.preset);   // id in world coords; tile into the frame
            s.tile = Vec2i{s.tile.x - tile_off.x, s.tile.y - tile_off.y};
            w.sources.push_back(std::move(s));
        }
    }
    // Ambient: a chunk@2 ambient authored identically by every ready chunk wins; otherwise the default.
    w.ambient = in_.ambient;
    {
        std::optional<world::Chunk::Ambient> common;
        bool same = !ready.empty();
        for (const auto& c : ready) {
            const world::Chunk* ch = chunk_at(add(c, off));
            if (!ch || !ch->ambient) { same = false; break; }
            if (common && *common != *ch->ambient) { same = false; break; }
            common = ch->ambient;
        }
        if (same && common) w.ambient = Ambient{AmbientKind(common->kind), Light(common->level)};
    }
    if (in_.attached_sources) in_.attached_sources(combat, w.sources);
    // R-G11 → light: map live ActiveZones that carry obscurement / magical darkness / opaque into LightWorld::zones so
    // LightSample flags (kObscured*, kMagicalDark) and opaque occlusion match Fog Cloud / Darkness / Web / Wall of Fire.
    append_active_zones(combat, w.zones);
    w.normalise();
    if (in_.incremental) stats_ = cache_.begin_step_incremental(std::move(w), ready);
    else {
        cache_.begin_step(std::move(w));
        stats_ = LightCache::StepStats{true, 0, 0};
    }
    vis_.begin_step(cache_, viewers, ready);

    LightFrameInputs fi;
    fi.chunks = ready;
    fi.blocked = blocked;
    fi.visibility = &vis_;
    std::shared_ptr<const LightFrame> prev = latest();
    auto f = publish_light_frame(cache_, fi, ++step_, prev.get());
    {
        std::lock_guard<std::mutex> lk(mu_);
        latest_ = f;
    }
    return f;
}

} // namespace eng::sim
