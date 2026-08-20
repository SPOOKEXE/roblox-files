#pragma once
#include <rbxl/result.hpp>
#include <rbxl/dom.hpp>
#include <string>
#include <vector>

// Turns a `Dom` back into an .rbxlx/.rbxmx document: the inverse of decode.cpp.
// See encode.cpp for the element-by-element mapping this follows.
namespace rbxl {
namespace xml {

struct EncodeDiagnostics {
    std::vector<std::string> warnings;
};

// `pretty` controls indentation only; the document is byte-identical either
// way once whitespace-only text nodes are ignored. `diagnostics`, when
// non-null, receives one warning per dropped `Dom::unknownChunks()` entry and
// per property whose type has no XML representation, so a caller such as
// `convert` can report what did not survive the trip.
Result<std::string> encode(const Dom& dom, bool pretty = true,
                            EncodeDiagnostics* diagnostics = nullptr);

}  // namespace xml
}  // namespace rbxl
