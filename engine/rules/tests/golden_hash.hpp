#pragma once
// Test-only golden hash: FNV-1a 64 over canonical little-endian bytes (§4.5).
// Stand-in until detmath::stable_hash (xxHash3-64) lands; goldens must be regenerated then.
#include <cstdint>
#include <initializer_list>

struct GoldenHash {
    uint64_t h = 0xCBF29CE484222325ull;
    void byte(uint8_t b) { h ^= b; h *= 0x100000001B3ull; }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) byte(uint8_t(v >> (8 * i))); }
    void i32(int32_t v) { u32(uint32_t(v)); }
};
