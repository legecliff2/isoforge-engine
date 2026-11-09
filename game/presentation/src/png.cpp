#include "presentation/png.hpp"

#include <cstdlib>
#include <cstring>

// puff (Mark Adler, zlib contrib) inflates a raw deflate stream. PNG IDAT is a zlib wrapper
// around that stream. Unaltered; see third_party/puff/puff.h.
extern "C" {
#include "puff.h"
}

namespace eng::presentation {

namespace {

uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t adler32(const uint8_t* data, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a += data[i];
        if (a >= 65521) a -= 65521;
        b += a;
        if (b >= 65521) b -= 65521;
    }
    return (b << 16) | a;
}

uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = int(a) + int(b) - int(c);
    const int pa = std::abs(p - int(a)), pb = std::abs(p - int(b)), pc = std::abs(p - int(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

int channels_of(uint8_t color_type) {
    switch (color_type) {
    case 0: return 1;   // grey
    case 2: return 3;   // rgb
    case 4: return 2;   // grey + alpha
    case 6: return 4;   // rgba
    default: return 0;
    }
}

void to_rgba(const uint8_t* src, int ch, uint8_t* dst) {
    if (ch == 4) {
        std::memcpy(dst, src, 4);
        return;
    }
    if (ch == 3) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
        return;
    }
    if (ch == 2) {
        dst[0] = dst[1] = dst[2] = src[0]; dst[3] = src[1];
        return;
    }
    dst[0] = dst[1] = dst[2] = src[0]; dst[3] = 255;
}

} // namespace

std::optional<ImageRgba> decode_png(std::span<const std::byte> bytes) {
    if (bytes.size() < 8) return std::nullopt;
    const auto* b = reinterpret_cast<const uint8_t*>(bytes.data());
    const size_t n = bytes.size();
    static constexpr uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (std::memcmp(b, kSig, 8) != 0) return std::nullopt;

    uint32_t w = 0, h = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;
    bool have_ihdr = false;
    std::vector<uint8_t> idat;
    size_t i = 8;
    while (i + 12 <= n) {
        const uint32_t len = read_be32(b + i);
        if (i + 12 + size_t(len) > n) return std::nullopt;
        const uint8_t* type = b + i + 4;
        const uint8_t* data = b + i + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13 || have_ihdr) return std::nullopt;
            w = read_be32(data);
            h = read_be32(data + 4);
            bit_depth = data[8];
            color_type = data[9];
            const uint8_t compression = data[10], filter = data[11];
            interlace = data[12];
            if (compression != 0 || filter != 0) return std::nullopt;
            have_ihdr = true;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), data, data + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        i += 12 + size_t(len);
    }
    const int ch = channels_of(color_type);
    if (!have_ihdr || w == 0 || h == 0 || bit_depth != 8 || ch == 0 || interlace != 0) return std::nullopt;
    if (w > 16384 || h > 16384) return std::nullopt;
    const size_t stride = size_t(w) * size_t(ch);
    const size_t raw_size = size_t(h) * (stride + 1);
    if (idat.size() < 6) return std::nullopt;

    const uint8_t cmf = idat[0], flg = idat[1];
    if ((cmf & 0x0F) != 8 || (flg & 0x20) != 0) return std::nullopt;
    if ((uint32_t(cmf) * 256u + flg) % 31u != 0) return std::nullopt;

    std::vector<uint8_t> raw(raw_size);
    unsigned long destlen = static_cast<unsigned long>(raw_size);
    unsigned long sourcelen = static_cast<unsigned long>(idat.size() - 2);
    if (puff(raw.data(), &destlen, idat.data() + 2, &sourcelen) != 0) return std::nullopt;
    if (destlen != raw_size) return std::nullopt;
    if (size_t(2) + size_t(sourcelen) + 4 > idat.size()) return std::nullopt;
    const uint8_t* ad = idat.data() + 2 + sourcelen;
    const uint32_t expect = read_be32(ad);
    if (adler32(raw.data(), raw.size()) != expect) return std::nullopt;

    ImageRgba out;
    out.w = w;
    out.h = h;
    out.rgba.resize(size_t(w) * h * 4);
    std::vector<uint8_t> prev(stride, 0), cur(stride, 0);
    for (uint32_t y = 0; y < h; ++y) {
        const size_t row = size_t(y) * (stride + 1);
        const uint8_t filter_ty = raw[row];
        const uint8_t* in = raw.data() + row + 1;
        if (filter_ty > 4) return std::nullopt;
        for (size_t x = 0; x < stride; ++x) {
            const uint8_t left = x >= size_t(ch) ? cur[x - size_t(ch)] : 0;
            const uint8_t up = prev[x];
            const uint8_t ul = x >= size_t(ch) ? prev[x - size_t(ch)] : 0;
            uint8_t v = in[x];
            if (filter_ty == 1) v = uint8_t(v + left);
            else if (filter_ty == 2) v = uint8_t(v + up);
            else if (filter_ty == 3) v = uint8_t(v + uint8_t((int(left) + int(up)) / 2));
            else if (filter_ty == 4) v = uint8_t(v + paeth(left, up, ul));
            cur[x] = v;
        }
        for (uint32_t x = 0; x < w; ++x) to_rgba(cur.data() + size_t(x) * size_t(ch), ch, out.px(x, y));
        prev.swap(cur);
    }
    return out;
}

ImageRgba magenta_checker(uint32_t w, uint32_t h) {
    ImageRgba im;
    im.w = w;
    im.h = h;
    im.rgba.resize(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = im.px(x, y);
            if (((x / 4) ^ (y / 4)) & 1u) {
                p[0] = 255; p[1] = 0; p[2] = 255; p[3] = 255;
            } else {
                p[0] = 0x1E; p[1] = 0x22; p[2] = 0x28; p[3] = 255;
            }
        }
    }
    return im;
}

ImageRgba hflip_pivot(const ImageRgba& src) {
    ImageRgba d;
    d.w = src.w;
    d.h = src.h;
    d.rgba.assign(size_t(src.w) * src.h * 4, 0);
    if (src.w == 0 || src.h == 0 || src.rgba.size() != size_t(src.w) * src.h * 4) return d;
    for (uint32_t y = 0; y < src.h; ++y)
        for (uint32_t x = 1; x < src.w; ++x)
            std::memcpy(d.px(x, y), src.px(src.w - x, y), 4);
    return d;
}

} // namespace eng::presentation
