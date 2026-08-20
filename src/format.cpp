#include <rbxl/format.hpp>

#include "binary/decode.hpp"
#include "binary/encode.hpp"
#include "xml/decode.hpp"
#include "xml/encode.hpp"

#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

// The front door: sniffs a container's format from its bytes or a path's
// extension, and picks the matching codec (src/binary or src/xml) for
// load/save. See detectFormat for the sniffing rule and saveFile for how it
// resolves SaveOptions::format against the path's extension.
namespace rbxl {
namespace {

bool isAsciiSpace(uint8_t c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

// Skips a run of whitespace, XML declarations ("<?...?>") and comments
// ("<!--...-->") starting at *pos, in any order and any number of times.
// Returns false if a declaration or comment is left unterminated.
bool skipXmlPrelude(const uint8_t* data, std::size_t size, std::size_t& pos) {
    for (;;) {
        while (pos < size && isAsciiSpace(data[pos])) ++pos;

        if (pos + 1 < size && data[pos] == '<' && data[pos + 1] == '?') {
            std::size_t end = pos + 2;
            while (end + 1 < size && !(data[end] == '?' && data[end + 1] == '>')) ++end;
            if (end + 1 >= size) return false;
            pos = end + 2;
            continue;
        }

        if (pos + 3 < size && data[pos] == '<' && data[pos + 1] == '!' &&
            data[pos + 2] == '-' && data[pos + 3] == '-') {
            std::size_t end = pos + 4;
            while (end + 2 < size && !(data[end] == '-' && data[end + 1] == '-' && data[end + 2] == '>'))
                ++end;
            if (end + 2 >= size) return false;
            pos = end + 3;
            continue;
        }

        return true;
    }
}

std::string lowercase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

Result<Format> detectFormat(const uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return makeError(ErrorCode::BadMagic, "empty input");
    }

    static const char kBinaryMagic[] = "<roblox!";
    constexpr std::size_t kBinaryMagicLen = sizeof(kBinaryMagic) - 1;
    if (size >= kBinaryMagicLen && std::memcmp(data, kBinaryMagic, kBinaryMagicLen) == 0) {
        return Format::Binary;
    }

    std::size_t pos = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        pos = 3;  // UTF-8 BOM
    }

    if (!skipXmlPrelude(data, size, pos)) {
        return makeError(ErrorCode::BadMagic, "unterminated XML declaration or comment");
    }

    static const char kXmlRoot[] = "<roblox";
    constexpr std::size_t kXmlRootLen = sizeof(kXmlRoot) - 1;
    if (pos + kXmlRootLen <= size && std::memcmp(data + pos, kXmlRoot, kXmlRootLen) == 0) {
        // Require a tag boundary after "<roblox" so "<robloxfoo...>" does not match.
        uint8_t next = (pos + kXmlRootLen < size) ? data[pos + kXmlRootLen] : uint8_t{'>'};
        if (isAsciiSpace(next) || next == '>' || next == '/') {
            return Format::Xml;
        }
    }

    return makeError(ErrorCode::BadMagic, "not a recognised Roblox container");
}

Result<Format> formatFromExtension(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 == path.size()) {
        return makeError(ErrorCode::InvalidArgument, "path has no extension: " + path);
    }
    std::string ext = lowercase(path.substr(dot + 1));
    if (ext == "rbxl" || ext == "rbxm") return Format::Binary;
    if (ext == "rbxlx" || ext == "rbxmx") return Format::Xml;
    return makeError(ErrorCode::InvalidArgument, "unrecognised extension in path: " + path);
}

Result<Dom> loadBuffer(const uint8_t* data, std::size_t size) {
    RBXL_TRY(format, detectFormat(data, size));
    if (format == Format::Binary) {
        return binary::decode(data, size);
    }
    return xml::decode(reinterpret_cast<const char*>(data), size);
}

Result<Dom> loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return makeError(ErrorCode::Io, "could not open file: " + path);
    }
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    if (in.bad()) {
        return makeError(ErrorCode::Io, "error reading file: " + path);
    }
    return loadBuffer(bytes.data(), bytes.size());
}

namespace {

void appendWarnings(Diagnostics* diagnostics, std::vector<std::string>&& warnings) {
    if (diagnostics == nullptr) return;
    diagnostics->warnings.insert(diagnostics->warnings.end(),
                                  std::make_move_iterator(warnings.begin()),
                                  std::make_move_iterator(warnings.end()));
}

}  // namespace

Result<std::vector<uint8_t>> saveBuffer(const Dom& dom, const SaveOptions& options,
                                         Diagnostics* diagnostics) {
    // No path to infer from here: an unset format means Binary.
    Format format = options.format.value_or(Format::Binary);

    if (format == Format::Binary) {
        binary::EncodeOptions opts;
        opts.compression = options.compression;
        opts.level = options.level;
        opts.reflection = options.reflection;

        binary::EncodeDiagnostics codecDiagnostics;
        auto result = binary::encode(dom, opts, &codecDiagnostics);
        appendWarnings(diagnostics, std::move(codecDiagnostics.warnings));
        return result;
    }

    xml::EncodeDiagnostics codecDiagnostics;
    auto result = xml::encode(dom, options.pretty, &codecDiagnostics);
    appendWarnings(diagnostics, std::move(codecDiagnostics.warnings));
    if (!result) return result.error();
    return std::vector<uint8_t>(result.value().begin(), result.value().end());
}

Status saveFile(const Dom& dom, const std::string& path, SaveOptions options,
                 Diagnostics* diagnostics) {
    // options.format == nullopt means "infer from the path's extension".
    // This is independent of every other field: changing compression, level,
    // pretty, or reflection never disables inference, and an explicitly set
    // format (Binary or Xml) is always honoured, even against a mismatched
    // extension.
    if (!options.format.has_value()) {
        auto extFormat = formatFromExtension(path);
        if (extFormat) {
            options.format = extFormat.value();
        } else {
            options.format = Format::Binary;
            if (diagnostics != nullptr) {
                diagnostics->warnings.push_back(
                    "saveFile: could not infer a format from the extension of \"" + path +
                    "\"; defaulting to Binary");
            }
        }
    }

    RBXL_TRY(bytes, saveBuffer(dom, options, diagnostics));

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return makeError(ErrorCode::Io, "could not open file for writing: " + path);
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return makeError(ErrorCode::Io, "error writing file: " + path);
    }
    return Status{};
}

}  // namespace rbxl
