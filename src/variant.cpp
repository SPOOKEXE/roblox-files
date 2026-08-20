#include <rbxl/variant.hpp>

#include <cstring>

namespace rbxl {

namespace {

bool floatEqual(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

bool doubleEqual(double a, double b) {
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

bool cframeEqual(const CFrame& a, const CFrame& b) {
    if (!floatEqual(a.position.x, b.position.x) || !floatEqual(a.position.y, b.position.y) ||
        !floatEqual(a.position.z, b.position.z)) {
        return false;
    }
    for (int i = 0; i < 9; ++i) {
        if (!floatEqual(a.rotation[i], b.rotation[i])) return false;
    }
    return true;
}

bool vector2Equal(const Vector2& a, const Vector2& b) {
    return floatEqual(a.x, b.x) && floatEqual(a.y, b.y);
}

bool vector3Equal(const Vector3& a, const Vector3& b) {
    return floatEqual(a.x, b.x) && floatEqual(a.y, b.y) && floatEqual(a.z, b.z);
}

bool vector2int16Equal(const Vector2int16& a, const Vector2int16& b) {
    return a.x == b.x && a.y == b.y;
}

bool vector3int16Equal(const Vector3int16& a, const Vector3int16& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool color3Equal(const Color3& a, const Color3& b) {
    return floatEqual(a.r, b.r) && floatEqual(a.g, b.g) && floatEqual(a.b, b.b);
}

bool color3uint8Equal(const Color3uint8& a, const Color3uint8& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

bool udimEqual(const UDim& a, const UDim& b) {
    return floatEqual(a.scale, b.scale) && a.offset == b.offset;
}

}  // namespace

const char* variantTypeName(VariantType type) {
    switch (type) {
        case VariantType::Nil: return "Nil";
        case VariantType::String: return "String";
        case VariantType::Bool: return "Bool";
        case VariantType::Int32: return "Int32";
        case VariantType::Int64: return "Int64";
        case VariantType::Float32: return "Float32";
        case VariantType::Float64: return "Float64";
        case VariantType::UDim: return "UDim";
        case VariantType::UDim2: return "UDim2";
        case VariantType::Ray: return "Ray";
        case VariantType::Faces: return "Faces";
        case VariantType::Axes: return "Axes";
        case VariantType::BrickColor: return "BrickColor";
        case VariantType::Color3: return "Color3";
        case VariantType::Color3uint8: return "Color3uint8";
        case VariantType::Vector2: return "Vector2";
        case VariantType::Vector2int16: return "Vector2int16";
        case VariantType::Vector3: return "Vector3";
        case VariantType::Vector3int16: return "Vector3int16";
        case VariantType::CFrame: return "CFrame";
        case VariantType::OptionalCFrame: return "OptionalCFrame";
        case VariantType::Region3: return "Region3";
        case VariantType::Region3int16: return "Region3int16";
        case VariantType::Rect: return "Rect";
        case VariantType::EnumValue: return "EnumValue";
        case VariantType::EnumItem: return "EnumItem";
        case VariantType::Ref: return "Ref";
        case VariantType::NumberSequence: return "NumberSequence";
        case VariantType::ColorSequence: return "ColorSequence";
        case VariantType::NumberRange: return "NumberRange";
        case VariantType::PhysicalProperties: return "PhysicalProperties";
        case VariantType::Font: return "Font";
        case VariantType::UniqueId: return "UniqueId";
        case VariantType::SecurityCapabilities: return "SecurityCapabilities";
        case VariantType::Content: return "Content";
        case VariantType::ContentId: return "ContentId";
        case VariantType::BinaryString: return "BinaryString";
        case VariantType::ProtectedString: return "ProtectedString";
        case VariantType::Bytecode: return "Bytecode";
        case VariantType::SharedString: return "SharedString";
        case VariantType::NetAssetRef: return "NetAssetRef";
    }
    return "Unknown";
}

bool variantEqual(const Variant& a, const Variant& b) {
    if (a.index() != b.index()) return false;

    return std::visit(
        [&](const auto& lhs) -> bool {
            using T = std::decay_t<decltype(lhs)>;
            const T& rhs = std::get<T>(b);

            if constexpr (std::is_same_v<T, std::monostate>) {
                return true;
            } else if constexpr (std::is_same_v<T, float>) {
                return floatEqual(lhs, rhs);
            } else if constexpr (std::is_same_v<T, double>) {
                return doubleEqual(lhs, rhs);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return lhs == rhs;
            } else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int32_t> ||
                                  std::is_same_v<T, int64_t>) {
                return lhs == rhs;
            } else if constexpr (std::is_same_v<T, UDim>) {
                return udimEqual(lhs, rhs);
            } else if constexpr (std::is_same_v<T, UDim2>) {
                return udimEqual(lhs.x, rhs.x) && udimEqual(lhs.y, rhs.y);
            } else if constexpr (std::is_same_v<T, Ray>) {
                return vector3Equal(lhs.origin, rhs.origin) &&
                       vector3Equal(lhs.direction, rhs.direction);
            } else if constexpr (std::is_same_v<T, Faces> || std::is_same_v<T, Axes>) {
                return lhs.bits == rhs.bits;
            } else if constexpr (std::is_same_v<T, BrickColor>) {
                return lhs.number == rhs.number;
            } else if constexpr (std::is_same_v<T, Color3>) {
                return color3Equal(lhs, rhs);
            } else if constexpr (std::is_same_v<T, Color3uint8>) {
                return color3uint8Equal(lhs, rhs);
            } else if constexpr (std::is_same_v<T, Vector2>) {
                return vector2Equal(lhs, rhs);
            } else if constexpr (std::is_same_v<T, Vector2int16>) {
                return vector2int16Equal(lhs, rhs);
            } else if constexpr (std::is_same_v<T, Vector3>) {
                return vector3Equal(lhs, rhs);
            } else if constexpr (std::is_same_v<T, Vector3int16>) {
                return vector3int16Equal(lhs, rhs);
            } else if constexpr (std::is_same_v<T, CFrame>) {
                return cframeEqual(lhs, rhs);
            } else if constexpr (std::is_same_v<T, OptionalCFrame>) {
                if (lhs.hasValue != rhs.hasValue) return false;
                return !lhs.hasValue || cframeEqual(lhs.value, rhs.value);
            } else if constexpr (std::is_same_v<T, Region3>) {
                return vector3Equal(lhs.min, rhs.min) && vector3Equal(lhs.max, rhs.max);
            } else if constexpr (std::is_same_v<T, Region3int16>) {
                return vector3int16Equal(lhs.min, rhs.min) && vector3int16Equal(lhs.max, rhs.max);
            } else if constexpr (std::is_same_v<T, Rect>) {
                return vector2Equal(lhs.min, rhs.min) && vector2Equal(lhs.max, rhs.max);
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                return lhs.value == rhs.value;
            } else if constexpr (std::is_same_v<T, EnumItem>) {
                return lhs.enumType == rhs.enumType && lhs.value == rhs.value;
            } else if constexpr (std::is_same_v<T, Ref>) {
                return lhs.target == rhs.target;
            } else if constexpr (std::is_same_v<T, NumberSequence>) {
                if (lhs.keypoints.size() != rhs.keypoints.size()) return false;
                for (std::size_t i = 0; i < lhs.keypoints.size(); ++i) {
                    const auto& l = lhs.keypoints[i];
                    const auto& r = rhs.keypoints[i];
                    if (!floatEqual(l.time, r.time) || !floatEqual(l.value, r.value) ||
                        !floatEqual(l.envelope, r.envelope)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, ColorSequence>) {
                if (lhs.keypoints.size() != rhs.keypoints.size()) return false;
                for (std::size_t i = 0; i < lhs.keypoints.size(); ++i) {
                    const auto& l = lhs.keypoints[i];
                    const auto& r = rhs.keypoints[i];
                    if (!floatEqual(l.time, r.time) || !color3Equal(l.color, r.color) ||
                        !floatEqual(l.envelope, r.envelope)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, NumberRange>) {
                return floatEqual(lhs.min, rhs.min) && floatEqual(lhs.max, rhs.max);
            } else if constexpr (std::is_same_v<T, PhysicalProperties>) {
                return lhs.custom == rhs.custom &&
                       lhs.hasAcousticAbsorption == rhs.hasAcousticAbsorption &&
                       floatEqual(lhs.density, rhs.density) &&
                       floatEqual(lhs.friction, rhs.friction) &&
                       floatEqual(lhs.elasticity, rhs.elasticity) &&
                       floatEqual(lhs.frictionWeight, rhs.frictionWeight) &&
                       floatEqual(lhs.elasticityWeight, rhs.elasticityWeight) &&
                       floatEqual(lhs.acousticAbsorption, rhs.acousticAbsorption);
            } else if constexpr (std::is_same_v<T, Font>) {
                return lhs.family == rhs.family && lhs.weight == rhs.weight &&
                       lhs.style == rhs.style && lhs.cachedFaceId == rhs.cachedFaceId;
            } else if constexpr (std::is_same_v<T, UniqueId>) {
                return lhs.index == rhs.index && lhs.time == rhs.time && lhs.random == rhs.random;
            } else if constexpr (std::is_same_v<T, SecurityCapabilities>) {
                return lhs.value == rhs.value;
            } else if constexpr (std::is_same_v<T, Content>) {
                return lhs.sourceType == rhs.sourceType && lhs.uri == rhs.uri &&
                       lhs.object == rhs.object;
            } else if constexpr (std::is_same_v<T, ContentId>) {
                return lhs.url == rhs.url;
            } else if constexpr (std::is_same_v<T, BinaryString>) {
                return lhs.data == rhs.data;
            } else if constexpr (std::is_same_v<T, ProtectedString>) {
                return lhs.value == rhs.value;
            } else if constexpr (std::is_same_v<T, Bytecode>) {
                return lhs.data == rhs.data;
            } else if constexpr (std::is_same_v<T, SharedString>) {
                return lhs.key == rhs.key && lhs.value == rhs.value;
            } else if constexpr (std::is_same_v<T, NetAssetRef>) {
                return lhs.key == rhs.key && lhs.value == rhs.value;
            } else {
                static_assert(!sizeof(T), "unhandled Variant alternative in variantEqual");
                return false;
            }
        },
        a);
}

}  // namespace rbxl
