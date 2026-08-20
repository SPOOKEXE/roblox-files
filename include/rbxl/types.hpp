#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rbxl {

using InstanceId = uint32_t;
constexpr InstanceId kNoInstance = static_cast<InstanceId>(-1);

struct Vector2      { float x = 0, y = 0; };
struct Vector3      { float x = 0, y = 0, z = 0; };
struct Vector2int16 { int16_t x = 0, y = 0; };
struct Vector3int16 { int16_t x = 0, y = 0, z = 0; };

// Rotation is row-major: R00 R01 R02 R10 R11 R12 R20 R21 R22.
struct CFrame {
    Vector3 position;
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
struct OptionalCFrame { bool hasValue = false; CFrame value; };

struct UDim  { float scale = 0; int32_t offset = 0; };
struct UDim2 { UDim x, y; };
struct Ray   { Vector3 origin, direction; };
struct Rect  { Vector2 min, max; };
struct Region3      { Vector3 min, max; };
struct Region3int16 { Vector3int16 min, max; };

struct Color3      { float r = 0, g = 0, b = 0; };
struct Color3uint8 { uint8_t r = 0, g = 0, b = 0; };
struct BrickColor  { uint32_t number = 0; };

// Bitfields. Faces: Front, Bottom, Left, Back, Top, Right in the low 6 bits.
// Axes: X, Y, Z in the low 3 bits.
struct Faces { uint8_t bits = 0; };
struct Axes  { uint8_t bits = 0; };

struct NumberSequenceKeypoint { float time = 0, value = 0, envelope = 0; };
struct NumberSequence { std::vector<NumberSequenceKeypoint> keypoints; };

// `envelope` is serialised by Roblox but has no effect.
struct ColorSequenceKeypoint { float time = 0; Color3 color; float envelope = 0; };
struct ColorSequence { std::vector<ColorSequenceKeypoint> keypoints; };

struct NumberRange { float min = 0, max = 0; };

struct PhysicalProperties {
    bool custom = false;
    bool hasAcousticAbsorption = false;
    float density = 0, friction = 0, elasticity = 0;
    float frictionWeight = 0, elasticityWeight = 0;
    float acousticAbsorption = 1.0f;   // spec default when the flag bit is clear
};

struct Font {
    std::string family;        // a content URI, e.g. rbxasset://fonts/families/Arial.json
    uint16_t weight = 400;     // FontWeight value, 100..900 in steps of 100
    uint8_t style = 0;         // FontStyle: 0 = Normal, 1 = Italic
    std::string cachedFaceId;  // may be empty; omitted from XML when empty
};

// Roblox's instance identity stamp. Note the field ORDER and the Random
// rotation differ between the binary and XML encodings; see the codec tasks.
struct UniqueId {
    uint32_t index = 0;
    uint32_t time = 0;      // seconds since 2021-01-01
    int64_t random = 0;
};

struct SecurityCapabilities { uint64_t value = 0; };

// The modern Content datatype (Roblox release 645+). Distinct from ContentId.
struct Content {
    enum class SourceType : uint32_t { None = 0, Uri = 1, Object = 2 };
    SourceType sourceType = SourceType::None;
    std::string uri;                    // when sourceType == Uri
    InstanceId object = kNoInstance;    // when sourceType == Object
};

// The legacy type, called `Content` before release 645. Serialised as <url>.
struct ContentId { std::string url; };

struct Ref { InstanceId target = kNoInstance; };

// Enum properties. Named EnumValue because `Enum` reads badly in C++.
struct EnumValue { uint32_t value = 0; };

// Attributes-only type carrying the enum's name alongside its value.
struct EnumItem { std::string enumType; uint32_t value = 0; };

// Arbitrary bytes. Roblox hides several private formats behind this type
// (attributes, tags, terrain material colours); see blob.hpp.
struct BinaryString { std::vector<uint8_t> data; };

// Source code. Whitespace must be preserved exactly; XML writes it as CDATA.
struct ProtectedString { std::string value; };

// Precompiled Luau. Never interpret or modify this; read and write it as-is.
struct Bytecode { std::vector<uint8_t> data; };

// A string held once in the file's shared table and referenced by many
// instances. `key` holds the raw identifier bytes (16 bytes in binary files,
// the base64-decoded `md5` attribute in XML). Roblox ignores the key's value.
struct SharedString { std::string key; std::string value; };

// Stored identically to SharedString but semantically an asset reference.
struct NetAssetRef { std::string key; std::string value; };

}  // namespace rbxl
