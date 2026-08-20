#include <doctest.h>
#include "binary/valuecodec.hpp"
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

static std::vector<Variant> decodeOk(TypeId t, std::vector<uint8_t> d, size_t n,
                                     const CodecContext& ctx = {}) {
    auto r = decodeValueArray(t, d.data(), d.size(), n, ctx);
    REQUIRE(r);
    return r.value();
}
static std::vector<uint8_t> encodeOk(TypeId t, const std::vector<Variant>& v,
                                     const CodecContext& ctx = {}) {
    std::vector<uint8_t> out;
    REQUIRE(encodeValueArray(t, v, out, ctx));
    return out;
}

TEST_CASE("UDim splits into a Scale array then an Offset array") {
    // Spec: UDim{1,2} and UDim{3,4}.
    std::vector<uint8_t> data{0x7f, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
    auto values = decodeOk(TypeId::UDim, data, 2);
    CHECK(std::get<UDim>(values[0]).scale == 1.0f);
    CHECK(std::get<UDim>(values[0]).offset == 2);
    CHECK(std::get<UDim>(values[1]).scale == 3.0f);
    CHECK(std::get<UDim>(values[1]).offset == 4);
    CHECK(encodeOk(TypeId::UDim, values) == data);
}

TEST_CASE("UDim2 orders components X.Scale, Y.Scale, X.Offset, Y.Offset") {
    // Spec: a single UDim2 {0.75, -30, -1.5, 60}.
    std::vector<uint8_t> data{0x7e, 0x80, 0x00, 0x00, 0x7f, 0x80, 0x00, 0x01,
                              0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x78};
    auto values = decodeOk(TypeId::UDim2, data, 1);
    auto v = std::get<UDim2>(values[0]);
    CHECK(v.x.scale == 0.75f);
    CHECK(v.x.offset == -30);
    CHECK(v.y.scale == -1.5f);
    CHECK(v.y.offset == 60);
    CHECK(encodeOk(TypeId::UDim2, values) == data);
}

TEST_CASE("Color3 is three interleaved Float32 arrays") {
    // Spec: Color3 for RGB 255, 180, 20.
    std::vector<uint8_t> data{0x7f, 0x00, 0x00, 0x00, 0x7e, 0x69,
                              0x69, 0x6a, 0x7b, 0x41, 0x41, 0x42};
    auto values = decodeOk(TypeId::Color3, data, 1);
    auto c = std::get<Color3>(values[0]);
    CHECK(c.r == 1.0f);
    CHECK(c.g == doctest::Approx(180.0f / 255.0f));
    CHECK(c.b == doctest::Approx(20.0f / 255.0f));
    CHECK(encodeOk(TypeId::Color3, values) == data);
}

TEST_CASE("Vector3 is three interleaved Float32 arrays") {
    // Spec: Vector3(1,2,3) and Vector3(-1,-2,-3).
    std::vector<uint8_t> data{0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                              0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                              0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x01};
    auto values = decodeOk(TypeId::Vector3, data, 2);
    auto a = std::get<Vector3>(values[0]);
    auto b = std::get<Vector3>(values[1]);
    CHECK(a.x == 1.0f); CHECK(a.y == 2.0f); CHECK(a.z == 3.0f);
    CHECK(b.x == -1.0f); CHECK(b.y == -2.0f); CHECK(b.z == -3.0f);
    CHECK(encodeOk(TypeId::Vector3, values) == data);
}

TEST_CASE("Vector2 is two interleaved Float32 arrays") {
    // Spec: Vector2(-100.80, 200.55) and Vector2(200.55, -100.80).
    std::vector<uint8_t> data{0x85, 0x86, 0x93, 0x91, 0x33, 0x19, 0x35, 0x9a,
                              0x86, 0x85, 0x91, 0x93, 0x19, 0x33, 0x9a, 0x35};
    auto values = decodeOk(TypeId::Vector2, data, 2);
    CHECK(std::get<Vector2>(values[0]).x == doctest::Approx(-100.80f));
    CHECK(std::get<Vector2>(values[0]).y == doctest::Approx(200.55f));
    CHECK(std::get<Vector2>(values[1]).x == doctest::Approx(200.55f));
    CHECK(encodeOk(TypeId::Vector2, values) == data);
}

TEST_CASE("Rect orders components Min.X, Min.Y, Max.X, Max.Y") {
    // Spec: Rect(-1,-10,8,9) and Rect(0,1,5,6).
    std::vector<uint8_t> data{0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                              0x82, 0x7f, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00,
                              0x82, 0x81, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
                              0x82, 0x81, 0x20, 0x80, 0x00, 0x00, 0x00, 0x00};
    auto values = decodeOk(TypeId::Rect, data, 2);
    auto a = std::get<Rect>(values[0]);
    CHECK(a.min.x == -1.0f); CHECK(a.min.y == -10.0f);
    CHECK(a.max.x == 8.0f);  CHECK(a.max.y == 9.0f);
    CHECK(encodeOk(TypeId::Rect, values) == data);
}

TEST_CASE("Ray is six plain little-endian floats in sequence") {
    std::vector<Variant> values{Ray{{1, 2, 3}, {4, 5, 6}}};
    auto data = encodeOk(TypeId::Ray, values);
    CHECK(data.size() == 24);
    CHECK(bit::readF32LE(data.data()) == 1.0f);
    CHECK(bit::readF32LE(data.data() + 20) == 6.0f);
    auto back = decodeOk(TypeId::Ray, data, 1);
    CHECK(std::get<Ray>(back[0]).direction.z == 6.0f);
}

TEST_CASE("NumberRange is two plain little-endian floats") {
    // Spec: NumberRange(0, 0.5) and NumberRange(0.5, 1).
    std::vector<uint8_t> data{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f,
                              0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x80, 0x3f};
    auto values = decodeOk(TypeId::NumberRange, data, 2);
    CHECK(std::get<NumberRange>(values[0]).min == 0.0f);
    CHECK(std::get<NumberRange>(values[0]).max == 0.5f);
    CHECK(std::get<NumberRange>(values[1]).max == 1.0f);
    CHECK(encodeOk(TypeId::NumberRange, values) == data);
}

TEST_CASE("CFrame uses rotation id 0x02 for the identity orientation") {
    // Spec id table: 0x02 is (0,0,0), i.e. the identity rotation matrix.
    std::vector<Variant> values{CFrame{}};
    auto data = encodeOk(TypeId::CFrame, values);
    CHECK(data[0] == 0x02);              // id byte, no matrix follows
    CHECK(data.size() == 1 + 12);        // id + one interleaved Vector3
    auto back = decodeOk(TypeId::CFrame, data, 1);
    auto c = std::get<CFrame>(back[0]);
    CHECK(c.rotation[0] == 1.0f);
    CHECK(c.rotation[4] == 1.0f);
    CHECK(c.rotation[8] == 1.0f);
    CHECK(c.rotation[1] == 0.0f);
}

TEST_CASE("CFrame writes a full matrix when the rotation is not a special case") {
    CFrame tilted;
    const float m[9] = {0.5f, -0.5f, 0.7f, 0.7f, 0.5f, -0.5f, 0.1f, 0.8f, 0.6f};
    for (int i = 0; i < 9; ++i) tilted.rotation[i] = m[i];
    tilted.position = {4, 5, 6};
    auto data = encodeOk(TypeId::CFrame, {tilted});
    CHECK(data[0] == 0x00);              // id 0 means a matrix follows
    CHECK(data.size() == 1 + 36 + 12);
    auto back = decodeOk(TypeId::CFrame, data, 1);
    auto c = std::get<CFrame>(back[0]);
    for (int i = 0; i < 9; ++i) CHECK(c.rotation[i] == m[i]);
    CHECK(c.position.x == 4.0f);
}

TEST_CASE("CFrame positions are stored as one Vector3 array after all id bytes") {
    CFrame a; a.position = {1, 2, 3};
    CFrame b; b.position = {4, 5, 6};
    auto data = encodeOk(TypeId::CFrame, {a, b});
    // Two identity ids, then a single interleaved Vector3 array of two values.
    CHECK(data[0] == 0x02);
    CHECK(data[1] == 0x02);
    CHECK(data.size() == 2 + 24);
    auto back = decodeOk(TypeId::CFrame, data, 2);
    CHECK(std::get<CFrame>(back[1]).position.z == 6.0f);
}

TEST_CASE("NumberSequence stores a count then keypoint triples") {
    NumberSequence seq;
    seq.keypoints = {{0.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.5f}};
    auto data = encodeOk(TypeId::NumberSequence, {seq});
    CHECK(bit::readU32LE(data.data()) == 3u);
    CHECK(data.size() == 4 + 3 * 12);
    auto back = decodeOk(TypeId::NumberSequence, data, 1);
    auto s = std::get<NumberSequence>(back[0]);
    REQUIRE(s.keypoints.size() == 3);
    CHECK(s.keypoints[2].envelope == 0.5f);
}

TEST_CASE("ColorSequence keypoints carry a trailing unused envelope float") {
    ColorSequence seq;
    seq.keypoints = {{0.0f, Color3{1, 1, 1}, 0.0f}, {1.0f, Color3{0, 0, 0}, 0.0f}};
    auto data = encodeOk(TypeId::ColorSequence, {seq});
    CHECK(bit::readU32LE(data.data()) == 2u);
    CHECK(data.size() == 4 + 2 * 20);   // 5 floats per keypoint
    auto back = decodeOk(TypeId::ColorSequence, data, 1);
    CHECK(std::get<ColorSequence>(back[0]).keypoints.size() == 2);
}

TEST_CASE("PhysicalProperties encodes its flag bits per the spec") {
    // Spec worked example, four values.
    std::vector<uint8_t> data{
        0x00,
        0x01, 0x33, 0x33, 0x33, 0x3f, 0x9a, 0x99, 0x99, 0x3e,
              0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f,
        0x02,
        0x03, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x3e,
              0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x3f};
    auto values = decodeOk(TypeId::PhysicalProperties, data, 4);
    auto v0 = std::get<PhysicalProperties>(values[0]);
    CHECK_FALSE(v0.custom);
    CHECK_FALSE(v0.hasAcousticAbsorption);

    auto v1 = std::get<PhysicalProperties>(values[1]);
    CHECK(v1.custom);
    CHECK_FALSE(v1.hasAcousticAbsorption);
    CHECK(v1.density == doctest::Approx(0.7f));
    CHECK(v1.acousticAbsorption == 1.0f);   // default when the bit is clear

    auto v2 = std::get<PhysicalProperties>(values[2]);
    CHECK_FALSE(v2.custom);
    CHECK(v2.hasAcousticAbsorption);

    auto v3 = std::get<PhysicalProperties>(values[3]);
    CHECK(v3.custom);
    CHECK(v3.hasAcousticAbsorption);
    CHECK(v3.density == doctest::Approx(0.25f));
    CHECK(v3.acousticAbsorption == doctest::Approx(0.5f));

    CHECK(encodeOk(TypeId::PhysicalProperties, values) == data);
}

TEST_CASE("OptionalCFrame nests a CFrame array and a Bool array") {
    // Spec worked example: one present identity-ish CFrame, one absent.
    OptionalCFrame present; present.hasValue = true;
    OptionalCFrame absent;  absent.hasValue = false;
    auto data = encodeOk(TypeId::OptionalCFrame, {present, absent});
    CHECK(data[0] == 0x10);                       // inner CFrame type id
    CHECK(data[data.size() - 3] == 0x02);         // inner Bool type id
    CHECK(data[data.size() - 2] == 0x01);         // first value present
    CHECK(data[data.size() - 1] == 0x00);         // second value absent
    auto back = decodeOk(TypeId::OptionalCFrame, data, 2);
    CHECK(std::get<OptionalCFrame>(back[0]).hasValue);
    CHECK_FALSE(std::get<OptionalCFrame>(back[1]).hasValue);
}

TEST_CASE("UniqueId records are 16 bytes interleaved at width 16") {
    std::vector<Variant> values{UniqueId{1, 2, 3}, UniqueId{4, 5, 6}};
    auto data = encodeOk(TypeId::UniqueId, values);
    CHECK(data.size() == 32);
    auto back = decodeOk(TypeId::UniqueId, data, 2);
    CHECK(std::get<UniqueId>(back[0]).index == 1u);
    CHECK(std::get<UniqueId>(back[0]).time == 2u);
    CHECK(std::get<UniqueId>(back[0]).random == 3);
    CHECK(std::get<UniqueId>(back[1]).random == 6);
}

TEST_CASE("Font stores family, weight, style, and cached face id") {
    Font f; f.family = "rbxasset://fonts/families/Arial.json";
    f.weight = 700; f.style = 1; f.cachedFaceId = "";
    auto data = encodeOk(TypeId::Font, {f});
    auto back = decodeOk(TypeId::Font, data, 1);
    auto g = std::get<Font>(back[0]);
    CHECK(g.family == f.family);
    CHECK(g.weight == 700);
    CHECK(g.style == 1);
    CHECK(g.cachedFaceId.empty());
}

TEST_CASE("Content stores a source-type array then counted sections") {
    Content none;
    Content uri;  uri.sourceType = Content::SourceType::Uri; uri.uri = "rbxassetid://123";
    Content obj;  obj.sourceType = Content::SourceType::Object; obj.object = 7;
    auto data = encodeOk(TypeId::Content, {none, uri, obj});
    auto back = decodeOk(TypeId::Content, data, 3);
    CHECK(std::get<Content>(back[0]).sourceType == Content::SourceType::None);
    CHECK(std::get<Content>(back[1]).sourceType == Content::SourceType::Uri);
    CHECK(std::get<Content>(back[1]).uri == "rbxassetid://123");
    CHECK(std::get<Content>(back[2]).sourceType == Content::SourceType::Object);
    CHECK(std::get<Content>(back[2]).object == 7u);
}
