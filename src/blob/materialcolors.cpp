#include <rbxl/blob.hpp>

// Terrain.MaterialColors: exactly 69 bytes, 23 materials of packed RGB, one
// Color3uint8 triple per material with no header or padding.
namespace rbxl {
namespace blob {

namespace {
constexpr std::size_t kMaterialCount = 23;
constexpr std::size_t kBlobSize = kMaterialCount * 3;   // 69
}  // namespace

Result<std::vector<Color3uint8>> parseMaterialColors(const std::vector<uint8_t>& data) {
    if (data.size() != kBlobSize) {
        return makeError(ErrorCode::Malformed, "MaterialColors blob must be exactly 69 bytes");
    }
    std::vector<Color3uint8> out;
    out.reserve(kMaterialCount);
    for (std::size_t i = 0; i < kMaterialCount; ++i) {
        out.push_back(Color3uint8{data[i * 3], data[i * 3 + 1], data[i * 3 + 2]});
    }
    return out;
}

std::vector<uint8_t> serializeMaterialColors(const std::vector<Color3uint8>& colors) {
    std::vector<uint8_t> out(kBlobSize, 0);
    const std::size_t n = colors.size() < kMaterialCount ? colors.size() : kMaterialCount;
    for (std::size_t i = 0; i < n; ++i) {
        out[i * 3] = colors[i].r;
        out[i * 3 + 1] = colors[i].g;
        out[i * 3 + 2] = colors[i].b;
    }
    return out;
}

}  // namespace blob
}  // namespace rbxl
