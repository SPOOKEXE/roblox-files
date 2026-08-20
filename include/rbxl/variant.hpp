#pragma once
#include <rbxl/types.hpp>
#include <string>
#include <type_traits>
#include <variant>

namespace rbxl {

// One alternative per serialisable property type across both file formats.
// std::monostate means "no value"; a default-constructed Variant is Nil.
using Variant = std::variant<
    std::monostate,
    std::string,        // String
    bool,               // Bool
    int32_t,            // Int32
    int64_t,            // Int64
    float,              // Float32
    double,             // Float64
    UDim, UDim2, Ray, Faces, Axes, BrickColor,
    Color3, Color3uint8, Vector2, Vector2int16, Vector3, Vector3int16,
    CFrame, OptionalCFrame, Region3, Region3int16, Rect,
    EnumValue, EnumItem, Ref,
    NumberSequence, ColorSequence, NumberRange,
    PhysicalProperties, Font, UniqueId, SecurityCapabilities,
    Content, ContentId,
    BinaryString, ProtectedString, Bytecode,
    SharedString, NetAssetRef>;

// Mirrors the alternatives above, in the same order.
enum class VariantType {
    Nil = 0, String, Bool, Int32, Int64, Float32, Float64,
    UDim, UDim2, Ray, Faces, Axes, BrickColor,
    Color3, Color3uint8, Vector2, Vector2int16, Vector3, Vector3int16,
    CFrame, OptionalCFrame, Region3, Region3int16, Rect,
    EnumValue, EnumItem, Ref,
    NumberSequence, ColorSequence, NumberRange,
    PhysicalProperties, Font, UniqueId, SecurityCapabilities,
    Content, ContentId,
    BinaryString, ProtectedString, Bytecode,
    SharedString, NetAssetRef,
};

// variantTypeOf() is a raw static_cast from Variant::index(), so VariantType
// must stay in exact lockstep with Variant's alternative list. Pinning only
// the ends would miss a same-count reorder in the middle, so every alternative
// is pinned individually. If you add a type, add it to Variant, to VariantType,
// to this list, and to the switch in variantTypeName().
template <VariantType E, typename T>
inline constexpr bool kAlternativeIs =
    std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(E), Variant>, T>;

static_assert(kAlternativeIs<VariantType::Nil, std::monostate>, "Variant index 0 must be std::monostate");
static_assert(kAlternativeIs<VariantType::String, std::string>, "Variant index mismatch: String");
static_assert(kAlternativeIs<VariantType::Bool, bool>, "Variant index mismatch: Bool");
static_assert(kAlternativeIs<VariantType::Int32, int32_t>, "Variant index mismatch: Int32");
static_assert(kAlternativeIs<VariantType::Int64, int64_t>, "Variant index mismatch: Int64");
static_assert(kAlternativeIs<VariantType::Float32, float>, "Variant index mismatch: Float32");
static_assert(kAlternativeIs<VariantType::Float64, double>, "Variant index mismatch: Float64");
static_assert(kAlternativeIs<VariantType::UDim, UDim>, "Variant index mismatch: UDim");
static_assert(kAlternativeIs<VariantType::UDim2, UDim2>, "Variant index mismatch: UDim2");
static_assert(kAlternativeIs<VariantType::Ray, Ray>, "Variant index mismatch: Ray");
static_assert(kAlternativeIs<VariantType::Faces, Faces>, "Variant index mismatch: Faces");
static_assert(kAlternativeIs<VariantType::Axes, Axes>, "Variant index mismatch: Axes");
static_assert(kAlternativeIs<VariantType::BrickColor, BrickColor>, "Variant index mismatch: BrickColor");
static_assert(kAlternativeIs<VariantType::Color3, Color3>, "Variant index mismatch: Color3");
static_assert(kAlternativeIs<VariantType::Color3uint8, Color3uint8>, "Variant index mismatch: Color3uint8");
static_assert(kAlternativeIs<VariantType::Vector2, Vector2>, "Variant index mismatch: Vector2");
static_assert(kAlternativeIs<VariantType::Vector2int16, Vector2int16>, "Variant index mismatch: Vector2int16");
static_assert(kAlternativeIs<VariantType::Vector3, Vector3>, "Variant index mismatch: Vector3");
static_assert(kAlternativeIs<VariantType::Vector3int16, Vector3int16>, "Variant index mismatch: Vector3int16");
static_assert(kAlternativeIs<VariantType::CFrame, CFrame>, "Variant index mismatch: CFrame");
static_assert(kAlternativeIs<VariantType::OptionalCFrame, OptionalCFrame>, "Variant index mismatch: OptionalCFrame");
static_assert(kAlternativeIs<VariantType::Region3, Region3>, "Variant index mismatch: Region3");
static_assert(kAlternativeIs<VariantType::Region3int16, Region3int16>, "Variant index mismatch: Region3int16");
static_assert(kAlternativeIs<VariantType::Rect, Rect>, "Variant index mismatch: Rect");
static_assert(kAlternativeIs<VariantType::EnumValue, EnumValue>, "Variant index mismatch: EnumValue");
static_assert(kAlternativeIs<VariantType::EnumItem, EnumItem>, "Variant index mismatch: EnumItem");
static_assert(kAlternativeIs<VariantType::Ref, Ref>, "Variant index mismatch: Ref");
static_assert(kAlternativeIs<VariantType::NumberSequence, NumberSequence>, "Variant index mismatch: NumberSequence");
static_assert(kAlternativeIs<VariantType::ColorSequence, ColorSequence>, "Variant index mismatch: ColorSequence");
static_assert(kAlternativeIs<VariantType::NumberRange, NumberRange>, "Variant index mismatch: NumberRange");
static_assert(kAlternativeIs<VariantType::PhysicalProperties, PhysicalProperties>, "Variant index mismatch: PhysicalProperties");
static_assert(kAlternativeIs<VariantType::Font, Font>, "Variant index mismatch: Font");
static_assert(kAlternativeIs<VariantType::UniqueId, UniqueId>, "Variant index mismatch: UniqueId");
static_assert(kAlternativeIs<VariantType::SecurityCapabilities, SecurityCapabilities>, "Variant index mismatch: SecurityCapabilities");
static_assert(kAlternativeIs<VariantType::Content, Content>, "Variant index mismatch: Content");
static_assert(kAlternativeIs<VariantType::ContentId, ContentId>, "Variant index mismatch: ContentId");
static_assert(kAlternativeIs<VariantType::BinaryString, BinaryString>, "Variant index mismatch: BinaryString");
static_assert(kAlternativeIs<VariantType::ProtectedString, ProtectedString>, "Variant index mismatch: ProtectedString");
static_assert(kAlternativeIs<VariantType::Bytecode, Bytecode>, "Variant index mismatch: Bytecode");
static_assert(kAlternativeIs<VariantType::SharedString, SharedString>, "Variant index mismatch: SharedString");
static_assert(kAlternativeIs<VariantType::NetAssetRef, NetAssetRef>, "Variant index mismatch: NetAssetRef");

// Belt-and-braces: the per-alternative pins above already imply this, but a
// cheap length check is still worth having.
static_assert(std::variant_size_v<Variant> ==
              static_cast<std::size_t>(VariantType::NetAssetRef) + 1,
              "VariantType must have exactly one enumerator per Variant alternative");

inline VariantType variantTypeOf(const Variant& v) {
    return static_cast<VariantType>(v.index());
}

const char* variantTypeName(VariantType type);

// Deep equality. Floats compare bitwise so that NaN payloads and signed zero
// survive round-trip assertions unchanged.
bool variantEqual(const Variant& a, const Variant& b);

}  // namespace rbxl
