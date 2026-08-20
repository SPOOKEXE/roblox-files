#pragma once
#include <rbxl/result.hpp>
#include <rbxl/dom.hpp>
#include <rbxl/compression.hpp>
#include <rbxl/reflection.hpp>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The public front door: format sniffing plus load/save that pick the right
// codec (src/binary, src/xml) for the caller. See format.cpp for the sniffing
// rules and the extension-vs-explicit-format resolution saveFile applies.
namespace rbxl {

enum class Format { Binary, Xml };

// Sniffs the container from its bytes. Binary files start with the literal
// "<roblox!"; XML files have a <roblox> root, possibly preceded by a UTF-8
// BOM, whitespace, an XML declaration, and/or comments, in any combination.
// Anything else is ErrorCode::BadMagic.
Result<Format> detectFormat(const uint8_t* data, std::size_t size);

// Infers the target format from a path's suffix, case-insensitively:
//   .rbxl / .rbxm  -> Binary      .rbxlx / .rbxmx -> Xml
// Any other (or missing) suffix is ErrorCode::InvalidArgument.
Result<Format> formatFromExtension(const std::string& path);

// `format`'s default of nullopt means "infer": saveFile infers from the
// path's extension (falling back to Binary, with a diagnostic, when the
// extension is unrecognised); saveBuffer has no path to infer from, so
// nullopt there simply means Binary. Assigning a plain Format still works
// (`options.format = Format::Xml;`) since std::optional converts from its
// value type. Every other field is independent of format resolution: changing
// compression, level, pretty, or reflection never disables extension
// inference.
struct SaveOptions {
    std::optional<Format> format;                     // nullopt = infer
    Compression compression = Compression::Zstd;
    int level = 3;
    bool pretty = true;                               // XML only
    const ReflectionDatabase* reflection = nullptr;
};

// Warnings collected from whichever codec ran, e.g. preserved chunks that
// could not be re-emitted or property types with no representation in the
// target format.
struct Diagnostics {
    std::vector<std::string> warnings;
};

// Sniffs `data` and decodes it with the matching codec.
Result<Dom> loadBuffer(const uint8_t* data, std::size_t size);

// Reads the whole file into memory and calls loadBuffer. A file that cannot
// be opened or read is ErrorCode::Io; one that opens but sniffs as neither
// container is whatever detectFormat/the codec reports.
Result<Dom> loadFile(const std::string& path);

// Encodes `dom` with the codec named by `options.format`, forwarding any
// codec diagnostics into `*diagnostics` when non-null. `options.format ==
// nullopt` means Binary: there is no path here to infer a format from.
Result<std::vector<uint8_t>> saveBuffer(const Dom& dom, const SaveOptions& options,
                                         Diagnostics* diagnostics = nullptr);

// Like saveBuffer, then writes the result to `path`. When `options.format`
// is nullopt, the format is inferred from `path`'s extension; when the
// extension is unrecognised (or absent), it falls back to Binary and appends
// a warning to `*diagnostics` (when non-null) naming the path and the
// fallback. An explicitly set `options.format` is always honoured, even
// against a mismatched extension.
Status saveFile(const Dom& dom, const std::string& path, SaveOptions options = {},
                 Diagnostics* diagnostics = nullptr);

}  // namespace rbxl
