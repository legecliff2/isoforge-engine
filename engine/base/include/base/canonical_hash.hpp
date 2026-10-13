#pragma once
// StableHash over a canonical serialization (ARCHITECTURE.md §4.5): xxHash3-64 over bytes
// written explicitly little-endian, strings length-prefixed, never raw memory/padding.
//
// Lives in base because world/ecs need it and detmath does not yet export stable_hash
// (§4.8 sketch). When detmath::stable_hash lands it must produce identical output
// (both are XXH3_64bits, seed 0), so ids/hashes derived here stay valid.
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace eng {

uint64_t stable_hash(std::span<const uint8_t> bytes) noexcept;   // XXH3_64bits, seed 0

class CanonicalHasher {
public:
    void byte(uint8_t b) { buf_.push_back(b); }
    void bytes(std::span<const uint8_t> s) { buf_.insert(buf_.end(), s.begin(), s.end()); }
    void u16(uint16_t v) { for (int i = 0; i < 2; ++i) byte(uint8_t(v >> (8 * i))); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) byte(uint8_t(v >> (8 * i))); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void str(std::string_view s);   // u32 length prefix + bytes
    uint64_t value() const noexcept { return stable_hash(buf_); }
    std::span<const uint8_t> canonical_bytes() const noexcept { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

} // namespace eng
