#include "iso2d/light_map.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace eng::iso2d {

Color light_tint(uint8_t packed, Fog fog) noexcept {
    switch (fog) {
    case Fog::Unexplored: return {0.f, 0.f, 0.f, 1.f};                 // hidden
    case Fog::Remembered: return {0.30f, 0.31f, 0.36f, 1.f};           // memory: no current light shown
    case Fog::Visible: break;
    }
    float m = 1.f;
    switch (light_level(packed)) {
    case LightLevel::Bright: m = 1.f; break;
    case LightLevel::Dim: m = 0.62f; break;
    case LightLevel::Dark: m = 0.22f; break;
    }
    if (packed & light_bits::kMagicalDark) m = std::min(m, 0.12f);
    Color c{m, m, m, 1.f};
    if (packed & light_bits::kObscuredHeavy) c = {m * 0.8f, m * 0.82f, m * 0.86f, 1.f};
    return c;
}

LightMaps::LightMaps(render::IRenderDevice& d) : dev_(d) {}
LightMaps::~LightMaps() {
    for (auto& [k, m] : maps_) dev_.destroy(m.tex);
}

void LightMaps::on_stream_delta(std::span<const LightChunkKey> added, std::span<const LightChunkKey> removed) {
    for (const LightChunkKey& k : removed) {
        auto it = maps_.find(k);
        if (it == maps_.end()) continue;
        dev_.destroy(it->second.tex);
        maps_.erase(it);
    }
    for (const LightChunkKey& k : added) {
        if (maps_.count(k)) continue;
        Map m;
        render::TextureDesc td{};
        td.w = td.h = kLightChunk;
        td.fmt = render::Format::R8_UNORM;
        td.filter = render::Filter::Linear;   // smoothing is presentation
        td.debug_name = "light_map";
        m.tex = dev_.create_texture(td, std::as_bytes(std::span(m.cpu)));
        maps_.emplace(k, m);
    }
}

bool LightMaps::update(LightChunkKey k, std::span<const uint8_t, kLightTiles> samples, RectI r) {
    auto it = maps_.find(k);
    if (it == maps_.end()) return false;
    std::copy(samples.begin(), samples.end(), it->second.cpu.begin());
    const int x0 = std::clamp(r.x, 0, kLightChunk), y0 = std::clamp(r.y, 0, kLightChunk);
    const int x1 = std::clamp(r.x + r.w, 0, kLightChunk), y1 = std::clamp(r.y + r.h, 0, kLightChunk);
    if (x1 <= x0 || y1 <= y0) return true;
    std::vector<std::byte> sub;
    sub.reserve(size_t((x1 - x0) * (y1 - y0)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) sub.push_back(std::byte(it->second.cpu[size_t(y * kLightChunk + x)]));
    dev_.update_texture(it->second.tex, RectI{x0, y0, x1 - x0, y1 - y0}, sub);
    bytes_uploaded_ += sub.size();
    ++uploads_;
    return true;
}

std::vector<LightChunkKey> LightMaps::keys() const {
    std::vector<LightChunkKey> out;
    for (const auto& kv : maps_) out.push_back(kv.first);
    return out;
}

render::TextureId LightMaps::texture(LightChunkKey k) const {
    auto it = maps_.find(k);
    return it == maps_.end() ? render::TextureId{} : it->second.tex;
}

uint8_t LightMaps::sample(LightChunkKey k, Vec2i p) const {
    auto it = maps_.find(k);
    if (it == maps_.end() || p.x < 0 || p.y < 0 || p.x >= kLightChunk || p.y >= kLightChunk) return 0;
    return it->second.cpu[size_t(p.y * kLightChunk + p.x)];
}

// ---- fog ----

void decode_fog(const FogMask& seen, const FogMask& explored, std::span<uint8_t, kLightTiles> out) noexcept {
    for (int y = 0; y < kLightChunk; ++y)
        for (int x = 0; x < kLightChunk; ++x)
            out[size_t(y * kLightChunk + x)] = fog_texel_of(fog_of(fog_mask_test(seen, x, y), fog_mask_test(explored, x, y)));
}

FogMaps::FogMaps(render::IRenderDevice& d) : dev_(d) {}
FogMaps::~FogMaps() {
    for (auto& [k, m] : maps_) dev_.destroy(m.tex);
}

void FogMaps::on_stream_delta(std::span<const LightChunkKey> added, std::span<const LightChunkKey> removed) {
    for (const LightChunkKey& k : removed) {
        auto it = maps_.find(k);
        if (it == maps_.end()) continue;
        dev_.destroy(it->second.tex);
        maps_.erase(it);
    }
    for (const LightChunkKey& k : added) {
        if (maps_.count(k)) continue;
        Map m;   // all kUnseen
        render::TextureDesc td{};
        td.w = td.h = kLightChunk;
        td.fmt = render::Format::R8_UNORM;
        td.filter = render::Filter::Linear;   // soft fog edges are presentation
        td.debug_name = "fog_map";
        m.tex = dev_.create_texture(td, std::as_bytes(std::span(m.cpu)));
        maps_.emplace(k, m);
    }
}

bool FogMaps::update(LightChunkKey k, std::span<const uint8_t, kLightTiles> texels) {
    auto it = maps_.find(k);
    if (it == maps_.end()) return false;
    auto& cpu = it->second.cpu;
    int x0 = kLightChunk, y0 = kLightChunk, x1 = -1, y1 = -1;
    for (int y = 0; y < kLightChunk; ++y)
        for (int x = 0; x < kLightChunk; ++x)
            if (cpu[size_t(y * kLightChunk + x)] != texels[size_t(y * kLightChunk + x)]) {
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    if (x1 < 0) return true;
    std::copy(texels.begin(), texels.end(), cpu.begin());
    std::vector<std::byte> sub;
    sub.reserve(size_t((x1 - x0 + 1) * (y1 - y0 + 1)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) sub.push_back(std::byte(cpu[size_t(y * kLightChunk + x)]));
    dev_.update_texture(it->second.tex, RectI{x0, y0, x1 - x0 + 1, y1 - y0 + 1}, sub);
    bytes_uploaded_ += sub.size();
    ++uploads_;
    return true;
}

bool FogMaps::update(LightChunkKey k, const FogMask& seen, const FogMask& explored) {
    std::array<uint8_t, kLightTiles> t{};
    decode_fog(seen, explored, t);
    return update(k, t);
}

render::TextureId FogMaps::texture(LightChunkKey k) const {
    auto it = maps_.find(k);
    return it == maps_.end() ? render::TextureId{} : it->second.tex;
}

Fog FogMaps::fog(LightChunkKey k, Vec2i p) const {
    auto it = maps_.find(k);
    if (it == maps_.end() || p.x < 0 || p.y < 0 || p.x >= kLightChunk || p.y >= kLightChunk) return Fog::Unexplored;
    return fog_of_texel(it->second.cpu[size_t(p.y * kLightChunk + p.x)]);
}

std::vector<LightChunkKey> FogMaps::keys() const {
    std::vector<LightChunkKey> out;
    for (const auto& kv : maps_) out.push_back(kv.first);
    return out;
}

} // namespace eng::iso2d
