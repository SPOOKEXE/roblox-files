#include <doctest.h>
#include "binary/valuecodec.hpp"
#include <limits>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

static std::vector<Variant> decodeOk(TypeId type, std::vector<uint8_t> data, size_t count,
                                     const CodecContext& ctx = {}) {
    auto r = decodeValueArray(type, data.data(), data.size(), count, ctx);
    REQUIRE(r);
    return r.value();
}

static std::vector<uint8_t> encodeOk(TypeId type, const std::vector<Variant>& values,
                                     const CodecContext& ctx = {}) {
    std::vector<uint8_t> out;
    REQUIRE(encodeValueArray(type, values, out, ctx));
    return out;
}

TEST_CASE("String values are length-prefixed and sequential") {
    std::vector<uint8_t> data{0x02, 0, 0, 0, 'h', 'i', 0x01, 0, 0, 0, 'x'};
    auto values = decodeOk(TypeId::String, data, 2);
    REQUIRE(values.size() == 2);
    CHECK(std::get<std::string>(values[0]) == "hi");
    CHECK(std::get<std::string>(values[1]) == "x");
    CHECK(encodeOk(TypeId::String, values) == data);
}

TEST_CASE("Bool values are one byte each") {
    auto values = decodeOk(TypeId::Bool, {0x01, 0x00, 0x01}, 3);
    CHECK(std::get<bool>(values[0]));
    CHECK_FALSE(std::get<bool>(values[1]));
    CHECK(std::get<bool>(values[2]));
    CHECK(encodeOk(TypeId::Bool, values) == std::vector<uint8_t>{0x01, 0x00, 0x01});
}

TEST_CASE("Int32 values are zigzagged, big-endian, and interleaved") {
    // The Offset half of the spec's UDim example: values 2 and 4.
    auto values = decodeOk(TypeId::Int32, {0, 0, 0, 0, 0, 0, 0x04, 0x08}, 2);
    CHECK(std::get<int32_t>(values[0]) == 2);
    CHECK(std::get<int32_t>(values[1]) == 4);
    CHECK(encodeOk(TypeId::Int32, values) ==
          std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0x04, 0x08});
}

TEST_CASE("Float32 values use the Roblox format and are interleaved") {
    // The Scale half of the spec's UDim example: values 1.0 and 3.0.
    auto values = decodeOk(TypeId::Float32, {0x7f, 0x80, 0x00, 0x80, 0, 0, 0, 0}, 2);
    CHECK(std::get<float>(values[0]) == 1.0f);
    CHECK(std::get<float>(values[1]) == 3.0f);
    CHECK(encodeOk(TypeId::Float32, values) ==
          std::vector<uint8_t>{0x7f, 0x80, 0x00, 0x80, 0, 0, 0, 0});
}

TEST_CASE("Float64 values are plain little-endian IEEE doubles") {
    std::vector<uint8_t> data{0, 0, 0, 0, 0, 0, 0xf0, 0x3f};   // 1.0
    auto values = decodeOk(TypeId::Float64, data, 1);
    CHECK(std::get<double>(values[0]) == 1.0);
    CHECK(encodeOk(TypeId::Float64, values) == data);
}

TEST_CASE("Faces and Axes are single-byte bitfields") {
    // Spec: Front / (Back, Top) / (Bottom, Left, Right) -> 01 18 26
    auto faces = decodeOk(TypeId::Faces, {0x01, 0x18, 0x26}, 3);
    CHECK(std::get<Faces>(faces[0]).bits == 0x01);
    CHECK(std::get<Faces>(faces[2]).bits == 0x26);
    // Spec: X / (X,Y) / (X,Z) -> 01 03 05
    auto axes = decodeOk(TypeId::Axes, {0x01, 0x03, 0x05}, 3);
    CHECK(std::get<Axes>(axes[1]).bits == 0x03);
    CHECK(encodeOk(TypeId::Axes, axes) == std::vector<uint8_t>{0x01, 0x03, 0x05});
}

TEST_CASE("BrickColor numbers are untransformed, big-endian, interleaved") {
    // Spec: Really red (1004), Bright green (37), Really blue (1010).
    std::vector<uint8_t> data{0, 0, 0, 0, 0, 0, 0x03, 0x00, 0x03, 0xEC, 0x25, 0xF2};
    auto values = decodeOk(TypeId::BrickColor, data, 3);
    CHECK(std::get<BrickColor>(values[0]).number == 1004u);
    CHECK(std::get<BrickColor>(values[1]).number == 37u);
    CHECK(std::get<BrickColor>(values[2]).number == 1010u);
    CHECK(encodeOk(TypeId::BrickColor, values) == data);
}

TEST_CASE("Referents accumulate across the array") {
    // Spec: raw [1619, 1, 4, 2, 3, 5] means [1619, 1620, 1624, 1626, 1629, 1634].
    std::vector<int32_t> raw{1619, 1, 4, 2, 3, 5};
    std::vector<uint8_t> flat(raw.size() * 4), woven(raw.size() * 4);
    for (size_t i = 0; i < raw.size(); ++i)
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(raw[i]));
    bit::interleave(flat.data(), woven.data(), raw.size(), 4);

    auto values = decodeOk(TypeId::Referent, woven, raw.size());
    const int32_t expected[] = {1619, 1620, 1624, 1626, 1629, 1634};
    for (size_t i = 0; i < raw.size(); ++i)
        CHECK(static_cast<int32_t>(std::get<Ref>(values[i]).target) == expected[i]);

    // Encoding must re-apply the delta, reproducing the original bytes exactly.
    CHECK(encodeOk(TypeId::Referent, values) == woven);
}

TEST_CASE("A null referent decodes to kNoInstance") {
    std::vector<uint8_t> flat(4), woven(4);
    bit::writeU32BE(flat.data(), bit::zigzagEncode32(-1));
    bit::interleave(flat.data(), woven.data(), 1, 4);
    auto values = decodeOk(TypeId::Referent, woven, 1);
    CHECK(std::get<Ref>(values[0]).target == kNoInstance);
}

TEST_CASE("Vector3int16 is three little-endian i16 in sequence") {
    // NOTE: the published spec's positive example is a typo (it shows big-endian
    // bytes). Its prose and its negative example both say little-endian, which is
    // what Roblox actually writes. Do not "fix" this test to match the document.
    std::vector<uint8_t> data{0x01, 0x00, 0x02, 0x00, 0x03, 0x00,
                              0xFF, 0xFF, 0xFE, 0xFF, 0xFD, 0xFF};
    auto values = decodeOk(TypeId::Vector3int16, data, 2);
    auto a = std::get<Vector3int16>(values[0]);
    auto b = std::get<Vector3int16>(values[1]);
    CHECK(a.x == 1); CHECK(a.y == 2); CHECK(a.z == 3);
    CHECK(b.x == -1); CHECK(b.y == -2); CHECK(b.z == -3);
    CHECK(encodeOk(TypeId::Vector3int16, values) == data);
}

TEST_CASE("Color3uint8 stores three separate component arrays") {
    // Spec: values (0,255,255) and (63,0,127) -> 00 3f ff 00 ff 7f
    std::vector<uint8_t> data{0x00, 0x3f, 0xff, 0x00, 0xff, 0x7f};
    auto values = decodeOk(TypeId::Color3uint8, data, 2);
    auto a = std::get<Color3uint8>(values[0]);
    auto b = std::get<Color3uint8>(values[1]);
    CHECK(a.r == 0);  CHECK(a.g == 255); CHECK(a.b == 255);
    CHECK(b.r == 63); CHECK(b.g == 0);   CHECK(b.b == 127);
    CHECK(encodeOk(TypeId::Color3uint8, values) == data);
}

TEST_CASE("Int64 is zigzagged, big-endian, and interleaved at width 8") {
    std::vector<Variant> values{int64_t{-5}, int64_t{1}, int64_t{9000000000LL}};
    auto data = encodeOk(TypeId::Int64, values);
    CHECK(data.size() == 24);
    auto back = decodeOk(TypeId::Int64, data, 3);
    CHECK(std::get<int64_t>(back[0]) == -5);
    CHECK(std::get<int64_t>(back[1]) == 1);
    CHECK(std::get<int64_t>(back[2]) == 9000000000LL);
}

TEST_CASE("SecurityCapabilities uses the Int64 encoding as unsigned bits") {
    std::vector<Variant> values{SecurityCapabilities{0xFFFFFFFFFFFFFFFFull},
                                SecurityCapabilities{0}};
    auto data = encodeOk(TypeId::SecurityCapabilities, values);
    auto back = decodeOk(TypeId::SecurityCapabilities, data, 2);
    CHECK(std::get<SecurityCapabilities>(back[0]).value == 0xFFFFFFFFFFFFFFFFull);
    CHECK(std::get<SecurityCapabilities>(back[1]).value == 0ull);
}

TEST_CASE("SharedString resolves through the file's shared table") {
    std::vector<SharedString> table{{"key0", "hello"}, {"key1", "world"}};
    CodecContext ctx; ctx.sharedStrings = &table;
    std::vector<uint8_t> flat{0, 0, 0, 1, 0, 0, 0, 0}, woven(8);
    bit::interleave(flat.data(), woven.data(), 2, 4);
    auto values = decodeOk(TypeId::SharedString, woven, 2, ctx);
    CHECK(std::get<SharedString>(values[0]).value == "world");
    CHECK(std::get<SharedString>(values[1]).value == "hello");
}

TEST_CASE("An out-of-range shared string index is an error, not a crash") {
    std::vector<SharedString> table{{"key0", "hello"}};
    CodecContext ctx; ctx.sharedStrings = &table;
    std::vector<uint8_t> flat{0, 0, 0, 9}, woven(4);
    bit::interleave(flat.data(), woven.data(), 1, 4);
    auto r = decodeValueArray(TypeId::SharedString, woven.data(), woven.size(), 1, ctx);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Malformed);
}

TEST_CASE("Truncated input is reported rather than read past the end") {
    // Two Int32 values need 8 bytes; supply 5.
    auto r = decodeValueArray(TypeId::Int32, std::vector<uint8_t>{1, 2, 3, 4, 5}.data(), 5, 2, {});
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Truncated);
}

TEST_CASE("An interleaved array whose count*width overflows size_t is rejected") {
    // width = 4 is Int32's interleave width. count is chosen so that
    // count * 4 wraps size_t on THIS build (64-bit); the guard threshold
    // scales with size_t's actual width, so this exercises the same
    // arithmetic path a 32-bit size_t build would hit at a smaller count,
    // without needing a 32-bit build or allocating anything large: `count`
    // is just a number here, and the guard must fire before it is ever
    // used to size or index a buffer. The data buffer stays 4 bytes.
    const size_t count = (std::numeric_limits<size_t>::max)() / 4 + 1;
    std::vector<uint8_t> data{0, 0, 0, 0};
    auto r = decodeValueArray(TypeId::Int32, data.data(), data.size(), count, {});
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Malformed);
}

TEST_CASE("Unknown type ids are reported as such") {
    CHECK(isKnownTypeId(0x01));
    CHECK(isKnownTypeId(0x22));
    CHECK_FALSE(isKnownTypeId(0x00));
    CHECK_FALSE(isKnownTypeId(0x7F));
}
