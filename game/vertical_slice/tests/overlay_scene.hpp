#pragma once
// Deterministic torch-heavy scene for the fog + glow overlay benchmark and the overlay determinism test.
// Pure function of (glows, frame): a 2 x 2 chunk view (64 x 64 tiles; the SliceView world batch holds 8192 sprites, so a 3 x 3 view would overflow it and drop glows), 8 PCs carrying torches, the rest
// static torches / lanterns / candles packed into a 16 x 16 tile square in the middle so every halo
// overlaps others, fog of war for team 0 (seen within ~20 tiles, explored within ~40) and per-frame
// motion: one PC steps each frame, so one source moves, its chunk's samples change and its visibility
// chunk is dirty. No RNG, no clock: identical inputs on every machine.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "view_model.hpp"

namespace slice::overlay_scene {

inline constexpr int kChunks = 2, kTiles = kChunks * 32, kPcs = 8, kCenter = kTiles / 2;

inline eng::Vec2i pc_tile(int i, uint64_t frame) {
    const int base_x = kCenter - 6 + (i % 4) * 4, base_y = kCenter - 4 + (i / 4) * 8;
    // PC (frame % 8) is the one that moved this frame; each PC walks a 4-step back-and-forth.
    const uint64_t steps = frame / kPcs + ((frame % kPcs) >= uint64_t(i) ? 1 : 0);
    const int phase = int(steps % 4);
    return {base_x + (phase < 2 ? phase : 4 - phase), base_y};
}

inline ViewModel build(int glows, uint64_t frame) {
    ViewModel vm;
    vm.sim_live = true;
    vm.round = int(frame);
    vm.turn = "PC0";
    for (int y = 0; y < kTiles; ++y)
        for (int x = 0; x < kTiles; ++x) {
            TileView t;
            t.tile = {x, y};
            const bool wall = (x % 11 == 3 && y % 7 != 0) || (y % 13 == 5 && x % 5 == 0);
            t.kind = wall ? TileKind::Wall : (x * 7 + y * 3) % 17 == 0 ? TileKind::Rubble : TileKind::Floor;
            t.layer = wall ? 2 : 0;
            vm.tiles.push_back(t);
        }
    for (int i = 0; i < kPcs; ++i) {
        ActorView a;
        a.id = uint32_t(100 + i);
        a.name = "PC" + std::to_string(i);
        a.tile = pc_tile(i, frame);
        a.hp = 10 + i; a.hp_max = 20;
        a.team = 0; a.is_pc = true; a.current = i == int(frame % kPcs);
        vm.actors.push_back(a);
    }
    LightView& L = vm.light;
    L.present = true;
    L.frame = frame + 1;
    L.fog = true;
    L.fog_team = 0;
    static const char* const kPresets[] = {"srd51.core.light.torch", "srd51.core.light.lantern", "srd51.core.light.candle",
                                           "srd51.core.light.lamp"};
    for (int i = 0; i < glows; ++i) {
        LightSourceView s;
        s.attached = i < kPcs;
        s.index = uint32_t(i + 1);
        if (i < kPcs) { s.carrier = 1; s.carrier_id = uint32_t(100 + i); s.tile = pc_tile(i, frame); }
        else s.tile = {kCenter - 8 + int((i * 37u) % 16u), kCenter - 8 + int((i * 53u) % 16u)};
        s.bright_ft = 20; s.dim_ft = 20;
        s.lit = true;
        s.preset = kPresets[i % 4];
        L.sources.push_back(s);
    }
    L.visuals["srd51.core.light.lantern"] = eng::iso2d::glow_style_from_visual({255, 215, 140, 900, eng::iso2d::GlowFlicker::None});
    const int moved = int(frame % kPcs);
    const eng::Vec2i mt = pc_tile(moved, frame);
    for (int cy = 0; cy < kChunks; ++cy)
        for (int cx = 0; cx < kChunks; ++cx) {
            LightChunkView c;
            c.cx = cx; c.cy = cy;
            c.has_fog = true;
            for (int ly = 0; ly < 32; ++ly)
                for (int lx = 0; lx < 32; ++lx) {
                    const int x = cx * 32 + lx, y = cy * 32 + ly;
                    int best = 1 << 30;
                    for (const LightSourceView& s : L.sources)
                        best = std::min(best, std::max(std::abs(x - s.tile.x), std::abs(y - s.tile.y)));
                    const uint8_t lvl = best <= 4 ? 2 : best <= 8 ? 1 : 0;
                    c.samples[size_t(ly * 32 + lx)] = lvl;
                    const int d = std::max(std::abs(x - mt.x), std::abs(y - mt.y));
                    const int dc = std::max(std::abs(x - kCenter), std::abs(y - kCenter));
                    const int b = ly * 32 + lx;
                    if (dc <= 18 || d <= 6) c.seen_now[size_t(b / 64)] |= uint64_t(1) << (b % 64);
                    if (d <= 40 || std::max(std::abs(x - kCenter), std::abs(y - kCenter)) <= 24)
                        c.explored[size_t(b / 64)] |= uint64_t(1) << (b % 64);
                }
            L.chunks.push_back(c);
        }
    if (frame == 0) {
        for (const auto& c : L.chunks) {
            L.added.push_back({c.cx, c.cy, c.level});
            L.dirty.push_back({{c.cx, c.cy, c.level}, eng::RectI{0, 0, 32, 32}});
        }
    } else {
        // Chunks the mover's old / new light (8 tiles) or sight bump (6 tiles) reaches are dirty (conservative).
        const eng::Vec2i pt = pc_tile(moved, frame - 1);
        auto near = [](const LightChunkView& c, eng::Vec2i t, int r) {
            const int x0 = c.cx * 32, y0 = c.cy * 32;
            return t.x + r >= x0 && t.x - r < x0 + 32 && t.y + r >= y0 && t.y - r < y0 + 32;
        };
        for (const auto& c : L.chunks) {
            if (near(c, mt, 9) || near(c, pt, 9)) L.dirty.push_back({{c.cx, c.cy, c.level}, eng::RectI{0, 0, 32, 32}});
            if (near(c, mt, 7) || near(c, pt, 7)) L.vis_dirty.push_back({c.cx, c.cy, c.level});
        }
    }
    return vm;
}

} // namespace slice::overlay_scene
