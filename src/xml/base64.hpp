#pragma once
#include <rbxl/result.hpp>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// Standard base64 (RFC 4648 alphabet, `+`/`/` with `=` padding).
// Used for SharedString/NetAssetRef `md5` keys and for BinaryString content.
namespace rbxl {
namespace xml {

std::string base64Encode(const uint8_t* data, size_t size);

// Whitespace anywhere in `text` is ignored. Any other character outside the
// base64 alphabet, or a total significant length that is not a multiple of
// 4, is an error.
Result<std::vector<uint8_t>> base64Decode(const std::string& text);

}  // namespace xml
}  // namespace rbxl
