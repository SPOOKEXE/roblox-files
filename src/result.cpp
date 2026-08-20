#include <rbxl/result.hpp>

namespace rbxl {

std::string Error::toString() const {
    static const char* kNames[] = {"None", "Io", "BadMagic", "BadVersion", "Truncated",
                                   "Malformed", "UnsupportedType", "Compression",
                                   "XmlParse", "InvalidArgument"};
    std::string out = kNames[static_cast<int>(code)];
    out += ": ";
    out += message;
    if (offset != 0) out += " (at offset " + std::to_string(offset) + ")";
    return out;
}

}  // namespace rbxl
