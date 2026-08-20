#pragma once
#include <rbxl/result.hpp>
#include <rbxl/variant.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Roblox hides three private encodings inside BinaryString properties. The
// Dom deliberately keeps BinaryString as raw bytes so a load/save round-trip
// stays lossless without understanding them; these helpers let a caller opt
// into structure for the three known uses. None of this shares anything with
// the binary chunk format's conventions: no zigzag, no Roblox float
// rotation, no interleaving, and (for attributes) an entirely different set
// of type ids.
namespace rbxl {
namespace blob {

// Instance.Attributes, stored in the "AttributesSerialize" BinaryString.
//
// Layout: a little-endian u32 count, then that many records of a String name
// (u32 length prefix plus bytes), a u8 type id, and a value in that type's
// encoding. Every integer and float is little-endian and untransformed.
//
// The type ids are their own table, distinct from the binary file format's
// (there, String is 0x01 and Bool is 0x02; here, String is 0x02 and Bool is
// 0x03):
//
//   String 0x02   Int32 0x04   Float64 0x06   UDim2 0x0A   Color3 0x0F
//   Vector3 0x11  EnumItem 0x15  ColorSequence 0x19  Rect 0x1C
//   Bool   0x03   Float32 0x05  UDim 0x09      BrickColor 0x0E  Vector2 0x10
//   CFrame 0x14   NumberSequence 0x17  NumberRange 0x1B  Font 0x21
//
// An entirely empty buffer means "no attributes" and parses to an empty map
// without requiring the count field; serializeAttributes mirrors this by
// emitting zero bytes for an empty map, matching what real Roblox places
// write for instances with no attributes set. A value whose Variant
// alternative has no attribute-blob encoding (e.g. Ref, Content, PhysicalProperties)
// is silently dropped by serializeAttributes, since this API has no error
// channel to report it through.
using AttributeMap = std::vector<std::pair<std::string, Variant>>;
Result<AttributeMap> parseAttributes(const std::vector<uint8_t>& data);
std::vector<uint8_t> serializeAttributes(const AttributeMap& attributes);

// CollectionService tags, stored in the "Tags" BinaryString as UTF-8 names
// separated by a single NUL byte, with no trailing separator after the last
// name. An empty buffer means no tags.
Result<std::vector<std::string>> parseTags(const std::vector<uint8_t>& data);
std::vector<uint8_t> serializeTags(const std::vector<std::string>& tags);

// Terrain.MaterialColors: exactly 69 bytes, three per-channel byte planes are
// NOT used here; the blob is 23 materials of packed RGB (r0 g0 b0 r1 g1 b1
// ...), one Color3uint8 triple per material, no header or padding.
Result<std::vector<Color3uint8>> parseMaterialColors(const std::vector<uint8_t>& data);
std::vector<uint8_t> serializeMaterialColors(const std::vector<Color3uint8>& colors);

}  // namespace blob
}  // namespace rbxl
