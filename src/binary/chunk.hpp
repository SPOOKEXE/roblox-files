#pragma once
#include <rbxl/result.hpp>
#include <rbxl/compression.hpp>
#include <rbxl/bitutil.hpp>
#include <cstdint>
#include <cstddef>
#include <vector>

// The binary container's outer framing: the 32-byte file header and the
// 16-byte chunk headers that follow it. One chunk body may be stored raw,
// LZ4-compressed, or Zstd-compressed; the compressed body itself says which,
// so this layer sniffs it rather than trusting a field. See the file header
// and chunk header tables in the task brief for the normative byte layout.
namespace rbxl {
namespace binary {

constexpr size_t kFileHeaderSize = 32;
constexpr size_t kChunkHeaderSize = 16;

// Caps the uncompressed length a chunk header is allowed to declare, checked
// before any allocation sized by that field. Keeps a corrupt or malicious
// length from forcing a multi-gigabyte allocation.
constexpr size_t kMaxChunkSize = 512u * 1024u * 1024u;

struct FileHeader {
    uint32_t classCount = 0;
    uint32_t instanceCount = 0;
};

struct Chunk {
    char name[4] = {0, 0, 0, 0};
    std::vector<uint8_t> data;   // decompressed payload
};

// Parses the 32-byte file header at the start of a binary place/model.
// Fails with Truncated if `size` is too small, BadMagic if the leading 8
// bytes or 6-byte signature do not match, BadVersion if the version field
// is not 0, or Malformed if the class/instance counts read negative as i32.
Result<FileHeader> readFileHeader(const uint8_t* data, size_t size);

// Appends a 32-byte file header for `header` to `out`.
void writeFileHeader(std::vector<uint8_t>& out, const FileHeader& header);

// Reads one chunk starting at `cursor`, advancing it past the chunk on
// success. Decompresses the body according to the sniffing rule: a
// compressed length of 0 means the payload is stored raw; otherwise the
// body's first four bytes select Zstd (`28 b5 2f fd`) or LZ4 block format.
Result<Chunk> readChunk(const uint8_t* data, size_t size, size_t& cursor);

// Appends one chunk (header plus body) to `out`. With Compression::None the
// payload is stored raw. Otherwise it is compressed at `level`, falling back
// to raw storage if the compressed form is not smaller than the input.
Status writeChunk(std::vector<uint8_t>& out, const char name[4],
                   const std::vector<uint8_t>& payload, Compression compression, int level);

}  // namespace binary
}  // namespace rbxl
