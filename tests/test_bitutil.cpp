#include <doctest.h>
#include <rbxl/bitutil.hpp>
#include <cstdint>
#include <vector>

using namespace rbxl::bit;

TEST_CASE("byte order helpers are explicit, not memcpy") {
    const uint8_t buf[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(readU32LE(buf) == 0x04030201u);
    CHECK(readU32BE(buf) == 0x01020304u);
    CHECK(readU16LE(buf) == 0x0201u);
    CHECK(readU16BE(buf) == 0x0102u);
    CHECK(readU64LE(buf) == 0x0807060504030201ull);
    CHECK(readU64BE(buf) == 0x0102030405060708ull);

    uint8_t out[8] = {};
    writeU32BE(out, 0x01020304u);
    CHECK(out[0] == 0x01);
    CHECK(out[3] == 0x04);
    writeU32LE(out, 0x01020304u);
    CHECK(out[0] == 0x04);
    CHECK(out[3] == 0x01);
}

TEST_CASE("zigzag transformation matches the spec") {
    // Spec: x >= 0 -> 2x, x < 0 -> 2|x| - 1.
    CHECK(zigzagEncode32(0) == 0u);
    CHECK(zigzagEncode32(2) == 4u);
    CHECK(zigzagEncode32(4) == 8u);
    CHECK(zigzagEncode32(-1) == 1u);
    CHECK(zigzagEncode32(-2) == 3u);
    CHECK(zigzagDecode32(0) == 0);
    CHECK(zigzagDecode32(4) == 2);
    CHECK(zigzagDecode32(8) == 4);
    CHECK(zigzagDecode32(1) == -1);
    CHECK(zigzagDecode32(3) == -2);

    CHECK(zigzagDecode32(zigzagEncode32(INT32_MIN)) == INT32_MIN);
    CHECK(zigzagDecode32(zigzagEncode32(INT32_MAX)) == INT32_MAX);
    CHECK(zigzagDecode64(zigzagEncode64(INT64_MIN)) == INT64_MIN);
    CHECK(zigzagDecode64(zigzagEncode64(INT64_MAX)) == INT64_MAX);
}

TEST_CASE("Roblox float format is IEEE-754 rotated left by one bit") {
    // IEEE-754 1.0f is 0x3F800000. Roblox moves the sign bit to the low end.
    CHECK(encodeRobloxFloat(1.0f) == 0x7F000000u);
    CHECK(decodeRobloxFloat(0x7F000000u) == 1.0f);
    CHECK(encodeRobloxFloat(3.0f) == 0x80800000u);
    CHECK(decodeRobloxFloat(0x80800000u) == 3.0f);
    // The sign bit lands in bit 0, so negation flips the least significant bit.
    CHECK(encodeRobloxFloat(-1.0f) == 0x7F000001u);
    CHECK(decodeRobloxFloat(0x7F000001u) == -1.0f);
    CHECK(decodeRobloxFloat(0x80000000u) == 2.0f);
    CHECK(decodeRobloxFloat(0x80000001u) == -2.0f);
    CHECK(decodeRobloxFloat(0x80800001u) == -3.0f);

    for (float v : {0.0f, -0.0f, 1.5f, -1234.5f, 3.4028235e38f, 1.17549435e-38f}) {
        CHECK(decodeRobloxFloat(encodeRobloxFloat(v)) == v);
    }
}

TEST_CASE("interleaving stores arrays column-wise") {
    // Spec: the sequence A0 A1 B0 B1 C0 C1 is stored as A0 B0 C0 A1 B1 C1.
    const uint8_t flat[6] = {0xA0, 0xA1, 0xB0, 0xB1, 0xC0, 0xC1};
    uint8_t woven[6] = {};
    interleave(flat, woven, /*count=*/3, /*width=*/2);
    const uint8_t expected[6] = {0xA0, 0xB0, 0xC0, 0xA1, 0xB1, 0xC1};
    CHECK(std::vector<uint8_t>(woven, woven + 6) == std::vector<uint8_t>(expected, expected + 6));

    uint8_t back[6] = {};
    deinterleave(woven, back, 3, 2);
    CHECK(std::vector<uint8_t>(back, back + 6) == std::vector<uint8_t>(flat, flat + 6));
}

TEST_CASE("golden vector: interleaved UDim scales decode to 1.0 and 3.0") {
    // From the spec: UDim {1, 2} and {3, 4} encode Scale as 7f 80 00 80 00 00 00 00.
    const uint8_t src[8] = {0x7f, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00};
    uint8_t flat[8] = {};
    deinterleave(src, flat, /*count=*/2, /*width=*/4);
    CHECK(decodeRobloxFloat(readU32BE(flat + 0)) == 1.0f);
    CHECK(decodeRobloxFloat(readU32BE(flat + 4)) == 3.0f);
}

TEST_CASE("golden vector: interleaved UDim offsets decode to 2 and 4") {
    // From the spec: the Offset half of the same pair is 00 00 00 00 00 00 04 08.
    const uint8_t src[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
    uint8_t flat[8] = {};
    deinterleave(src, flat, 2, 4);
    CHECK(zigzagDecode32(readU32BE(flat + 0)) == 2);
    CHECK(zigzagDecode32(readU32BE(flat + 4)) == 4);
}

TEST_CASE("golden vector: interleaved Vector3 array") {
    // From the spec: Vector3(1,2,3) and Vector3(-1,-2,-3) as three component arrays.
    auto decodeAxis = [](const uint8_t (&src)[8], float& a, float& b) {
        uint8_t flat[8] = {};
        deinterleave(src, flat, 2, 4);
        a = decodeRobloxFloat(readU32BE(flat + 0));
        b = decodeRobloxFloat(readU32BE(flat + 4));
    };
    const uint8_t xs[8] = {0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t ys[8] = {0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t zs[8] = {0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x01};
    float a = 0, b = 0;
    decodeAxis(xs, a, b); CHECK(a ==  1.0f); CHECK(b == -1.0f);
    decodeAxis(ys, a, b); CHECK(a ==  2.0f); CHECK(b == -2.0f);
    decodeAxis(zs, a, b); CHECK(a ==  3.0f); CHECK(b == -3.0f);
}

TEST_CASE("golden vector: interleaved BrickColor numbers") {
    // From the spec: 1004, 37, 1010 encode as 00 00 00 00 00 00 03 00 03 EC 25 F2.
    const uint8_t src[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x03, 0x00, 0x03, 0xEC, 0x25, 0xF2};
    uint8_t flat[12] = {};
    deinterleave(src, flat, /*count=*/3, /*width=*/4);
    CHECK(readU32BE(flat + 0) == 1004u);
    CHECK(readU32BE(flat + 4) == 37u);
    CHECK(readU32BE(flat + 8) == 1010u);
}
