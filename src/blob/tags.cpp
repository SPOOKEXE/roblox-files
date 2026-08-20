#include <rbxl/blob.hpp>

// CollectionService tags ("Tags"). NUL-separated UTF-8 names with no
// trailing separator; an empty buffer means no tags.
namespace rbxl {
namespace blob {

Result<std::vector<std::string>> parseTags(const std::vector<uint8_t>& data) {
    std::vector<std::string> out;
    if (data.empty()) {
        return out;
    }
    std::size_t start = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] == 0) {
            out.emplace_back(reinterpret_cast<const char*>(data.data() + start), i - start);
            start = i + 1;
        }
    }
    // The final name has no trailing separator, so its bytes run to the end
    // of the buffer rather than to a NUL.
    out.emplace_back(reinterpret_cast<const char*>(data.data() + start), data.size() - start);
    return out;
}

std::vector<uint8_t> serializeTags(const std::vector<std::string>& tags) {
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) {
            out.push_back(0);
        }
        out.insert(out.end(), tags[i].begin(), tags[i].end());
    }
    return out;
}

}  // namespace blob
}  // namespace rbxl
