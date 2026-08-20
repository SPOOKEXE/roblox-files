#pragma once
#include <rbxl/result.hpp>
#include <rbxl/compression.hpp>
#include <rbxl/dom.hpp>
#include <rbxl/reflection.hpp>
#include "binary/chunk.hpp"   // kChunkHeaderSize, bit:: (via bitutil.hpp): used by callers
                               // that inspect the encoded chunk stream directly (see tests).
#include <string>
#include <vector>

// Turns a `Dom` back into a binary place/model's chunk stream: the inverse of
// decode.cpp. See encode.cpp for the chunk-by-chunk order this follows and
// the two reconciliation rules that keep a hand-built `Dom` valid on disk.
namespace rbxl {
namespace binary {

struct EncodeOptions {
    rbxl::Compression compression = rbxl::Compression::Zstd;
    int level = 3;
    const ReflectionDatabase* reflection = nullptr;
};

struct EncodeDiagnostics {
    std::vector<std::string> warnings;
};

Result<std::vector<uint8_t>> encode(const Dom& dom, const EncodeOptions& options = {},
                                     EncodeDiagnostics* diagnostics = nullptr);

}  // namespace binary
}  // namespace rbxl
