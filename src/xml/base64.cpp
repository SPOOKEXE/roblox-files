#include "xml/base64.hpp"

#include <array>
#include <cctype>

namespace rbxl {
namespace xml {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 0xFF marks "not a base64 character"; 0xFE marks '=' (padding, valid only
// at the end of the significant input).
constexpr uint8_t kInvalid = 0xFF;
constexpr uint8_t kPad = 0xFE;

std::array<uint8_t, 256> buildDecodeTable() {
    std::array<uint8_t, 256> table{};
    table.fill(kInvalid);
    for (uint8_t i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = i;
    }
    table[static_cast<unsigned char>('=')] = kPad;
    return table;
}

bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

}  // namespace

std::string base64Encode(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8) |
                                static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
    }
    const size_t remaining = size - i;
    if (remaining == 1) {
        const uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
                                (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

Result<std::vector<uint8_t>> base64Decode(const std::string& text) {
    static const std::array<uint8_t, 256> kDecodeTable = buildDecodeTable();

    std::string clean;
    clean.reserve(text.size());
    for (char c : text) {
        if (!isWhitespace(c)) clean.push_back(c);
    }
    if (clean.empty()) return std::vector<uint8_t>();
    if (clean.size() % 4 != 0) {
        return makeError(ErrorCode::Malformed, "base64 length is not a multiple of 4");
    }

    std::vector<uint8_t> out;
    out.reserve((clean.size() / 4) * 3);
    for (size_t i = 0; i < clean.size(); i += 4) {
        uint8_t vals[4];
        int padCount = 0;
        for (int j = 0; j < 4; ++j) {
            const unsigned char ch = static_cast<unsigned char>(clean[i + j]);
            const uint8_t v = kDecodeTable[ch];
            if (v == kInvalid) {
                return makeError(ErrorCode::Malformed, "invalid base64 character", i + j);
            }
            if (v == kPad) {
                // Padding is only legal in the final group, and only in the
                // last one or two positions (once it starts, every
                // remaining character in the group must also be '=').
                if (i + 4 != clean.size()) {
                    return makeError(ErrorCode::Malformed, "unexpected padding", i + j);
                }
                if (j < 2) {
                    return makeError(ErrorCode::Malformed, "unexpected padding", i + j);
                }
                ++padCount;
            } else if (padCount > 0) {
                return makeError(ErrorCode::Malformed, "data after padding", i + j);
            }
            vals[j] = v;
        }

        const uint32_t chunk = (static_cast<uint32_t>(padCount >= 3 ? 0 : vals[0]) << 18) |
                                (static_cast<uint32_t>(padCount >= 3 ? 0 : vals[1]) << 12) |
                                (static_cast<uint32_t>(padCount >= 2 ? 0 : vals[2]) << 6) |
                                (static_cast<uint32_t>(padCount >= 1 ? 0 : vals[3]));
        out.push_back(static_cast<uint8_t>((chunk >> 16) & 0xFF));
        if (padCount < 2) out.push_back(static_cast<uint8_t>((chunk >> 8) & 0xFF));
        if (padCount < 1) out.push_back(static_cast<uint8_t>(chunk & 0xFF));
    }
    return out;
}

}  // namespace xml
}  // namespace rbxl
