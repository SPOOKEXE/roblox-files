#pragma once
#include <rbxl/types.hpp>
#include <string>
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

// variantTypeOf casts Variant::index() straight to VariantType, so the two
// lists above must stay in lockstep. These pin the first and last
// alternatives so an insertion in the wrong place fails to compile.
static_assert(static_cast<VariantType>(0) == VariantType::Nil,
              "VariantType must start with Nil = 0 to match Variant's first alternative");
static_assert(static_cast<VariantType>(std::variant_size_v<Variant> - 1) == VariantType::NetAssetRef,
              "VariantType's last enumerator must match Variant's last alternative");
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
