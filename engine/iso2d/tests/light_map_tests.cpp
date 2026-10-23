#include <doctest/doctest.h>

#include <array>
#include <vector>

#include "iso2d/light_map.hpp"
#include "render_null/null_device.hpp"

using namespace eng;
using namespace eng::iso2d;

namespace {
// Forwards to NullDevice (handle validation) and records texture uploads.
class RecDevice final : public render::IRenderDevice {
public:
    render::DeviceCaps caps() const override { return null.caps(); }
    render::TextureId create_texture(const render::TextureDesc& d, std::span<const std::byte> px) override {
        ++created;
        last_fmt = d.fmt;
        return null.create_texture(d, px);
    }
    render::BufferId create_buffer(const render::BufferDesc& d) override { return null.create_buffer(d); }
    render::PipelineId create_pipeline(const render::PipelineDesc& d) override { return null.create_pipeline(d); }
    void update_buffer(render::BufferId b, uint32_t o, std::span<const std::byte> s) override { null.update_buffer(b, o, s); }
    void update_texture(render::TextureId t, RectI r, std::span<const std::byte> s) override {
        rects.push_back(r);
        CHECK(s.size() == size_t(r.w * r.h));
        last = {s.begin(), s.end()};
        null.update_texture(t, r, s);
    }
    void destroy(render::TextureId t) override { ++destroyed; null.destroy(t); }
    void destroy(render::BufferId b) override { null.destroy(b); }
    void destroy(render::PipelineId p) override { null.destroy(p); }
    bool begin_frame() override { return null.begin_frame(); }
    render::ICommandList& command_list() override { return null.command_list(); }
    void end_frame() override { null.end_frame(); }
    void on_resize(uint32_t w, uint32_t h) override { null.on_resize(w, h); }

    render::NullDevice null;
    int created = 0, destroyed = 0;
    render::Format last_fmt{};
    std::vector<RectI> rects;
    std::vector<std::byte> last;
};
} // namespace

TEST_CASE("light map: pack / unpack and tint ordering (Dark never shown lit, fog hides light)") {
    for (auto l : {LightLevel::Dark, LightLevel::Dim, LightLevel::Bright})
        for (uint8_t f = 0; f < 16; ++f) {
            const uint8_t p = pack_light(l, f);
            CHECK(light_level(p) == l);
            CHECK((p >> 2) == f);
        }
    auto lum = [](uint8_t p, Fog fg) { return light_tint(p, fg).r; };
    const uint8_t dark = pack_light(LightLevel::Dark, 0), dim = pack_light(LightLevel::Dim, 0),
                  bright = pack_light(LightLevel::Bright, 0), sun = pack_light(LightLevel::Bright, 2);
    CHECK(lum(dark, Fog::Visible) < lum(dim, Fog::Visible));
    CHECK(lum(dim, Fog::Visible) < lum(bright, Fog::Visible));
    CHECK(lum(sun, Fog::Visible) == lum(bright, Fog::Visible));
    // Dark (with any flags) is never brighter than plain Dark.
    for (uint8_t f = 0; f < 16; ++f) CHECK(lum(pack_light(LightLevel::Dark, f), Fog::Visible) <= lum(dark, Fog::Visible));
    // Remembered / unexplored show no current light: same tint whatever the sample.
    CHECK(light_tint(bright, Fog::Remembered).r == light_tint(dark, Fog::Remembered).r);
    CHECK(light_tint(bright, Fog::Unexplored).r == 0.f);
    CHECK(fog_of(true, false) == Fog::Visible);
    CHECK(fog_of(false, true) == Fog::Remembered);
    CHECK(fog_of(false, false) == Fog::Unexplored);
}

TEST_CASE("light map: obscurement and magical-dark tints are distinct; fog still hides current light") {
    // sim LightFlags -> pack_light second arg: kMagicalDark=1, kSunlight=2, kObscuredLight=4, kObscuredHeavy=8
    const uint8_t plain = pack_light(LightLevel::Bright, 0);
    const uint8_t light = pack_light(LightLevel::Bright, 4);   // Web / lightly obscured
    const uint8_t heavy = pack_light(LightLevel::Bright, 8);   // Fog / Stinking Cloud heavy
    const uint8_t mag = pack_light(LightLevel::Bright, 1);     // magical darkness
    const uint8_t dim = pack_light(LightLevel::Dim, 0);
    const Color p = light_tint(plain, Fog::Visible), l = light_tint(light, Fog::Visible),
                h = light_tint(heavy, Fog::Visible), m = light_tint(mag, Fog::Visible),
                d = light_tint(dim, Fog::Visible);
    CHECK((plain & light_bits::kObscuredLight) == 0);
    CHECK((light & light_bits::kObscuredLight) != 0);
    CHECK((heavy & light_bits::kObscuredHeavy) != 0);
    CHECK((mag & light_bits::kMagicalDark) != 0);
    // ObscuredLight cools and dims vs plain Bright, but stays brighter than Dim and than Heavy.
    CHECK(l.r < p.r);
    CHECK(l.b > l.r);                 // cool cast
    CHECK(l.r > d.r);
    CHECK(h.r < l.r);                 // Heavy stronger than Light
    CHECK(h.b - h.r > l.b - l.r);     // Heavy cooler (larger blue bias)
    // Magical darkness is darker than Dim and than light obscurement.
    CHECK(m.r < d.r);
    CHECK(m.r < l.r);
    CHECK(m.b > m.r);                 // cool purple-black
    // Fog: Remembered / Unexplored ignore the packed sample (no current light / FoW inventing).
    CHECK(light_tint(light, Fog::Remembered).r == light_tint(plain, Fog::Remembered).r);
    CHECK(light_tint(heavy, Fog::Remembered).r == light_tint(mag, Fog::Remembered).r);
    CHECK(light_tint(heavy, Fog::Unexplored).r == 0.f);
    CHECK(light_tint(mag, Fog::Unexplored).b == 0.f);
    // Dark + any flags still never shown brighter than plain Dark.
    const uint8_t dark = pack_light(LightLevel::Dark, 0);
    const float dark_r = light_tint(dark, Fog::Visible).r;
    for (uint8_t f : {uint8_t(1), uint8_t(4), uint8_t(8), uint8_t(1 | 8)})
        CHECK(light_tint(pack_light(LightLevel::Dark, f), Fog::Visible).r <= dark_r);
}

TEST_CASE("light map: one R8 texture per chunk from stream deltas; dirty-rect uploads only") {
    RecDevice dev;
    {
        LightMaps lm(dev);
        const std::vector<LightChunkKey> add{{0, 0, 0}, {1, 0, 0}};
        lm.on_stream_delta(add, {});
        CHECK(lm.chunks() == 2);
        CHECK(dev.created == 2);
        CHECK(dev.last_fmt == render::Format::R8_UNORM);
        CHECK(lm.texture({1, 0, 0}).valid());
        CHECK_FALSE(lm.texture({5, 5, 0}).valid());
        lm.on_stream_delta(add, {});   // re-adding is a no-op
        CHECK(dev.created == 2);

        std::array<uint8_t, kLightTiles> s{};
        s[size_t(3 * kLightChunk + 2)] = pack_light(LightLevel::Bright, 0);
        s[size_t(4 * kLightChunk + 3)] = pack_light(LightLevel::Dim, 0);
        CHECK(lm.update({0, 0, 0}, s, RectI{2, 3, 2, 2}));
        REQUIRE(dev.rects.size() == 1);
        CHECK(dev.rects[0] == RectI{2, 3, 2, 2});
        CHECK(dev.last.size() == 4);
        CHECK(uint8_t(dev.last[0]) == pack_light(LightLevel::Bright, 0));
        CHECK(uint8_t(dev.last[3]) == pack_light(LightLevel::Dim, 0));
        CHECK(lm.sample({0, 0, 0}, {2, 3}) == pack_light(LightLevel::Bright, 0));
        CHECK(lm.bytes_uploaded() == 4);

        CHECK(lm.update({0, 0, 0}, s, RectI{}));            // nothing dirty: no upload
        CHECK(lm.update({0, 0, 0}, s, RectI{30, 30, 10, 10}));   // clipped to the chunk
        CHECK(dev.rects.back() == RectI{30, 30, 2, 2});
        CHECK_FALSE(lm.update({9, 9, 0}, s, RectI{0, 0, 32, 32}));   // not resident: ignored
        CHECK(dev.rects.size() == 2);

        const std::vector<LightChunkKey> rem{{0, 0, 0}};
        lm.on_stream_delta({}, rem);
        CHECK(lm.chunks() == 1);
        CHECK(dev.destroyed == 1);
        CHECK(lm.sample({0, 0, 0}, {2, 3}) == 0);
    }
    CHECK(dev.destroyed == 2);           // destructor frees the rest
    CHECK(dev.null.live_textures() == 0);
    CHECK(dev.null.errors() == 0);
}

TEST_CASE("fog: mask decode follows the sim::TileMask layout (bit = y*32+x, word = bit/64, LSB-first)") {
    FogMask m{};
    m[0] |= uint64_t(1) << 0;    // (0, 0)
    m[0] |= uint64_t(1) << 63;   // (31, 1): last bit of word 0
    m[1] |= uint64_t(1) << 0;    // (0, 2): first bit of word 1
    m[7] |= uint64_t(1) << 37;   // bit 485 = (5, 15)
    m[15] |= uint64_t(1) << 63;  // (31, 31)
    int set = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) set += fog_mask_test(m, x, y);
    CHECK(set == 5);
    CHECK(fog_mask_test(m, 0, 0));
    CHECK(fog_mask_test(m, 31, 1));
    CHECK_FALSE(fog_mask_test(m, 31, 0));
    CHECK(fog_mask_test(m, 0, 2));
    CHECK(fog_mask_test(m, 5, 15));
    CHECK(fog_mask_test(m, 31, 31));

    std::array<uint8_t, kLightTiles> t{};
    decode_fog(m, m, t);   // seen + explored
    CHECK(t[0] == fog_texel::kSeen);
    CHECK(t[size_t(1 * 32 + 31)] == fog_texel::kSeen);
    CHECK(t[size_t(15 * 32 + 5)] == fog_texel::kSeen);
    CHECK(t[1] == fog_texel::kUnseen);
}

TEST_CASE("fog: three states (unseen black, explored dimmed, seen normal) and tints") {
    FogMask seen{}, explored{};
    seen[0] = 1;                       // (0,0) seen now
    explored[0] = 1 | 2;               // (1,0) explored only
    std::array<uint8_t, kLightTiles> t{};
    decode_fog(seen, explored, t);
    CHECK(fog_of_texel(t[0]) == Fog::Visible);
    CHECK(fog_of_texel(t[1]) == Fog::Remembered);
    CHECK(fog_of_texel(t[2]) == Fog::Unexplored);
    const uint8_t bright = pack_light(LightLevel::Bright, 0);
    const Color v = light_tint(bright, Fog::Visible), r = light_tint(bright, Fog::Remembered), u = light_tint(bright, Fog::Unexplored);
    CHECK(v.r == 1.f);
    CHECK(r.r > 0.f);
    CHECK(r.r < v.r);
    CHECK(u.r == 0.f); CHECK(u.g == 0.f); CHECK(u.b == 0.f);
}

TEST_CASE("fog maps: one R8 texture per chunk, changed-texel uploads only, removed frees") {
    RecDevice dev;
    {
        FogMaps f(dev);
        const LightChunkKey k{0, 0, 0}, k2{1, 0, 0};
        f.on_stream_delta(std::array{k, k2}, {});
        CHECK(f.chunks() == 2);
        CHECK(dev.last_fmt == render::Format::R8_UNORM);
        CHECK(f.fog(k, {3, 3}) == Fog::Unexplored);
        FogMask seen{}, explored{};
        seen[1] = uint64_t(1) << 4;   // (4, 2)
        explored = seen;
        explored[1] |= uint64_t(1) << 5;   // (5, 2)
        CHECK(f.update(k, seen, explored));
        REQUIRE(dev.rects.size() == 1);
        CHECK(dev.rects[0] == RectI{4, 2, 2, 1});
        CHECK(f.fog(k, {4, 2}) == Fog::Visible);
        CHECK(f.fog(k, {5, 2}) == Fog::Remembered);
        CHECK(f.update(k, seen, explored));   // unchanged: no upload
        CHECK(dev.rects.size() == 1);
        CHECK_FALSE(f.update({9, 9, 0}, seen, explored));
        f.on_stream_delta({}, std::array{k2});
        CHECK(f.chunks() == 1);
        CHECK(dev.destroyed == 1);
        CHECK(f.fog(k2, {0, 0}) == Fog::Unexplored);
    }
    CHECK(dev.destroyed == 2);
}
