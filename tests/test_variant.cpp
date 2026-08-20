#include <doctest.h>
#include <rbxl/variant.hpp>
#include <cstring>

using namespace rbxl;

TEST_CASE("VariantType enum stays aligned with the variant alternatives") {
    CHECK(variantTypeOf(Variant{}) == VariantType::Nil);
    CHECK(variantTypeOf(Variant{std::string("hi")}) == VariantType::String);
    CHECK(variantTypeOf(Variant{true}) == VariantType::Bool);
    CHECK(variantTypeOf(Variant{int32_t{1}}) == VariantType::Int32);
    CHECK(variantTypeOf(Variant{int64_t{1}}) == VariantType::Int64);
    CHECK(variantTypeOf(Variant{1.0f}) == VariantType::Float32);
    CHECK(variantTypeOf(Variant{1.0}) == VariantType::Float64);
    CHECK(variantTypeOf(Variant{CFrame{}}) == VariantType::CFrame);
    CHECK(variantTypeOf(Variant{Font{}}) == VariantType::Font);
    CHECK(variantTypeOf(Variant{Content{}}) == VariantType::Content);
    CHECK(variantTypeOf(Variant{ContentId{}}) == VariantType::ContentId);
    CHECK(variantTypeOf(Variant{NetAssetRef{}}) == VariantType::NetAssetRef);
}

TEST_CASE("every VariantType has a name") {
    for (int i = 0; i <= static_cast<int>(VariantType::NetAssetRef); ++i) {
        const char* name = variantTypeName(static_cast<VariantType>(i));
        REQUIRE(name != nullptr);
        CHECK(std::strlen(name) > 0);
    }
}

TEST_CASE("variantEqual compares by value and by type") {
    CHECK(variantEqual(Variant{int32_t{5}}, Variant{int32_t{5}}));
    CHECK_FALSE(variantEqual(Variant{int32_t{5}}, Variant{int64_t{5}}));
    CHECK(variantEqual(Variant{Vector3{1, 2, 3}}, Variant{Vector3{1, 2, 3}}));
    CHECK_FALSE(variantEqual(Variant{Vector3{1, 2, 3}}, Variant{Vector3{1, 2, 4}}));

    NumberSequence a; a.keypoints.push_back({0.0f, 1.0f, 0.0f});
    NumberSequence b; b.keypoints.push_back({0.0f, 1.0f, 0.0f});
    CHECK(variantEqual(Variant{a}, Variant{b}));
    b.keypoints.push_back({1.0f, 0.0f, 0.0f});
    CHECK_FALSE(variantEqual(Variant{a}, Variant{b}));
}

TEST_CASE("float comparison is bitwise so signed zero is preserved") {
    CHECK_FALSE(variantEqual(Variant{0.0f}, Variant{-0.0f}));
}

TEST_CASE("Variant stays small enough for multi-million value files") {
    // RaceAPet.rbxl holds ~14 million property values. Guard against a new
    // alternative silently inflating every one of them. Raise deliberately,
    // with a measurement, never incidentally.
    CHECK(sizeof(Variant) <= 88);
}
