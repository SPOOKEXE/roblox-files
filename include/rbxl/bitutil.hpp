#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>

namespace rbxl {
namespace bit {

static_assert(std::numeric_limits<float>::is_iec559, "requires IEEE-754 float");
static_assert(std::numeric_limits<double>::is_iec559, "requires IEEE-754 double");

// --- Byte order ------------------------------------------------------------
// Always explicit. Never memcpy a multi-byte integer; the library must work on
// big-endian hosts.

inline uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline uint64_t readU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(readU32LE(p)) |
           (static_cast<uint64_t>(readU32LE(p + 4)) << 32);
}
inline uint64_t readU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(readU32BE(p)) << 32) |
           static_cast<uint64_t>(readU32BE(p + 4));
}

inline void writeU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
}
inline void writeU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v);
}
inline void writeU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);       p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24);
}
inline void writeU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}
inline void writeU64LE(uint8_t* p, uint64_t v) {
    writeU32LE(p, static_cast<uint32_t>(v));
    writeU32LE(p + 4, static_cast<uint32_t>(v >> 32));
}
inline void writeU64BE(uint8_t* p, uint64_t v) {
    writeU32BE(p, static_cast<uint32_t>(v >> 32));
    writeU32BE(p + 4, static_cast<uint32_t>(v));
}

// --- Integer transformation (zigzag) ---------------------------------------
// Maps signed values onto small unsigned ones so that runs of nearby numbers
// compress well: 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3, 2 -> 4 ...

inline uint32_t zigzagEncode32(int32_t v) {
    return (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
}
inline int32_t zigzagDecode32(uint32_t v) {
    return static_cast<int32_t>((v >> 1) ^ (~(v & 1u) + 1u));
}
inline uint64_t zigzagEncode64(int64_t v) {
    return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
}
inline int64_t zigzagDecode64(uint64_t v) {
    return static_cast<int64_t>((v >> 1) ^ (~(v & 1ull) + 1ull));
}

// --- Roblox float format ---------------------------------------------------
// Roblox stores float32 as  eeeeeeee mmmmmmmm mmmmmmmm mmmmmmms  (big-endian),
// i.e. IEEE-754's leading sign bit rotated round to the least significant bit.
// Encoding is therefore a 1-bit left rotate, decoding a 1-bit right rotate.

inline float decodeRobloxFloat(uint32_t robloxBits) {
    const uint32_t ieee = (robloxBits >> 1) | (robloxBits << 31);
    float out;
    std::memcpy(&out, &ieee, sizeof(out));
    return out;
}
inline uint32_t encodeRobloxFloat(float value) {
    uint32_t ieee;
    std::memcpy(&ieee, &value, sizeof(ieee));
    return (ieee << 1) | (ieee >> 31);
}

// Plain little-endian IEEE floats, used by the types that skip the Roblox format.
inline float readF32LE(const uint8_t* p) {
    const uint32_t bits = readU32LE(p);
    float out; std::memcpy(&out, &bits, sizeof(out)); return out;
}
inline void writeF32LE(uint8_t* p, float v) {
    uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); writeU32LE(p, bits);
}
inline double readF64LE(const uint8_t* p) {
    const uint64_t bits = readU64LE(p);
    double out; std::memcpy(&out, &bits, sizeof(out)); return out;
}
inline void writeF64LE(uint8_t* p, double v) {
    uint64_t bits; std::memcpy(&bits, &v, sizeof(bits)); writeU64LE(p, bits);
}

// --- Byte interleaving -----------------------------------------------------
// Arrays are stored in "columns": every value's first byte, then every value's
// second byte, and so on. Neighbouring values usually share high bytes, so this
// groups near-identical bytes together and the chunk compressor exploits it.
//
// src holds `count` values of `width` bytes each, laid out normally.
// dst receives the interleaved form: dst[b * count + i] == src[i * width + b].

inline void interleave(const uint8_t* src, uint8_t* dst, std::size_t count, std::size_t width) {
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t b = 0; b < width; ++b)
            dst[b * count + i] = src[i * width + b];
}

inline void deinterleave(const uint8_t* src, uint8_t* dst, std::size_t count, std::size_t width) {
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t b = 0; b < width; ++b)
            dst[i * width + b] = src[b * count + i];
}

}  // namespace bit
}  // namespace rbxl
