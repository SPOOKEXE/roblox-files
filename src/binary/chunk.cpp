#include "binary/chunk.hpp"

#include <cstring>
#include <limits>

#include <lz4.h>
#include <zstd.h>

namespace rbxl {
namespace binary {

namespace {

constexpr uint8_t kMagic[8] = {'<', 'r', 'o', 'b', 'l', 'o', 'x', '!'};
constexpr uint8_t kSignature[6] = {0x89, 0xff, 0x0d, 0x0a, 0x1a, 0x0a};
constexpr uint8_t kZstdMagic[4] = {0x28, 0xb5, 0x2f, 0xfd};
constexpr uint32_t kMaxSignedU32 = 0x7fffffffu;

bool looksLikeZstd(const uint8_t* body, size_t length) {
    return length >= 4 && std::memcmp(body, kZstdMagic, 4) == 0;
}

}  // namespace

Result<FileHeader> readFileHeader(const uint8_t* data, size_t size) {
    if (size < kFileHeaderSize) {
        return makeError(ErrorCode::Truncated, "file header shorter than 32 bytes", size);
    }
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) {
        return makeError(ErrorCode::BadMagic, "missing <roblox! magic", 0);
    }
    if (std::memcmp(data + 8, kSignature, sizeof(kSignature)) != 0) {
        return makeError(ErrorCode::BadMagic, "bad binary container signature", 8);
    }
    const uint16_t version = bit::readU16LE(data + 14);
    if (version != 0) {
        return makeError(ErrorCode::BadVersion, "unsupported binary container version", 14);
    }
    const uint32_t classCount = bit::readU32LE(data + 16);
    const uint32_t instanceCount = bit::readU32LE(data + 20);
    if (classCount > kMaxSignedU32) {
        return makeError(ErrorCode::Malformed, "class count reads negative as i32", 16);
    }
    if (instanceCount > kMaxSignedU32) {
        return makeError(ErrorCode::Malformed, "instance count reads negative as i32", 20);
    }

    FileHeader header;
    header.classCount = classCount;
    header.instanceCount = instanceCount;
    return header;
}

void writeFileHeader(std::vector<uint8_t>& out, const FileHeader& header) {
    const size_t base = out.size();
    out.resize(base + kFileHeaderSize, 0);
    uint8_t* p = out.data() + base;
    std::memcpy(p, kMagic, sizeof(kMagic));
    std::memcpy(p + 8, kSignature, sizeof(kSignature));
    bit::writeU16LE(p + 14, 0);
    bit::writeU32LE(p + 16, header.classCount);
    bit::writeU32LE(p + 20, header.instanceCount);
    // Bytes 24..31 (reserved) are already zero from the resize above.
}

Result<Chunk> readChunk(const uint8_t* data, size_t size, size_t& cursor) {
    if (cursor > size || size - cursor < kChunkHeaderSize) {
        return makeError(ErrorCode::Truncated, "chunk header shorter than 16 bytes", cursor);
    }
    const uint8_t* headerPtr = data + cursor;
    const uint32_t compressedLength = bit::readU32LE(headerPtr + 4);
    const uint32_t uncompressedLength = bit::readU32LE(headerPtr + 8);

    // Checked before any allocation sized by this field: a corrupt file
    // cannot force a giant buffer.
    if (uncompressedLength > kMaxChunkSize) {
        return makeError(ErrorCode::Malformed, "chunk uncompressed length exceeds cap", cursor + 8);
    }

    const size_t bodyOffset = cursor + kChunkHeaderSize;
    const size_t bodyLength = compressedLength == 0 ? uncompressedLength : compressedLength;
    if (bodyLength > size - bodyOffset) {
        return makeError(ErrorCode::Truncated, "chunk body runs past end of buffer", bodyOffset);
    }
    const uint8_t* body = data + bodyOffset;

    Chunk chunk;
    std::memcpy(chunk.name, headerPtr, 4);

    if (compressedLength == 0) {
        // Stored raw: never sniffed, the declared length is the whole story.
        chunk.data.assign(body, body + uncompressedLength);
    } else {
        chunk.data.resize(uncompressedLength);
        if (looksLikeZstd(body, compressedLength)) {
            const size_t written = ZSTD_decompress(chunk.data.data(), uncompressedLength, body, compressedLength);
            if (ZSTD_isError(written) || written != uncompressedLength) {
                return makeError(ErrorCode::Compression, "zstd decompression did not produce the declared length", bodyOffset);
            }
        } else {
            if (compressedLength > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
                uncompressedLength > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
                return makeError(ErrorCode::Compression, "lz4 chunk exceeds int range", bodyOffset);
            }
            const int written = LZ4_decompress_safe(reinterpret_cast<const char*>(body),
                                                      reinterpret_cast<char*>(chunk.data.data()),
                                                      static_cast<int>(compressedLength),
                                                      static_cast<int>(uncompressedLength));
            if (written < 0 || static_cast<uint32_t>(written) != uncompressedLength) {
                return makeError(ErrorCode::Compression, "lz4 decompression did not produce the declared length", bodyOffset);
            }
        }
    }

    cursor = bodyOffset + bodyLength;
    return chunk;
}

Status writeChunk(std::vector<uint8_t>& out, const char name[4],
                   const std::vector<uint8_t>& payload, Compression compression, int level) {
    std::vector<uint8_t> compressed;
    bool storeCompressed = false;

    if (compression == Compression::Lz4 && !payload.empty() &&
        payload.size() <= static_cast<size_t>(std::numeric_limits<int>::max())) {
        const int bound = LZ4_compressBound(static_cast<int>(payload.size()));
        if (bound > 0) {
            compressed.resize(static_cast<size_t>(bound));
            const int written = LZ4_compress_default(reinterpret_cast<const char*>(payload.data()),
                                                       reinterpret_cast<char*>(compressed.data()),
                                                       static_cast<int>(payload.size()), bound);
            if (written > 0 && static_cast<size_t>(written) < payload.size()) {
                compressed.resize(static_cast<size_t>(written));
                storeCompressed = true;
            }
        }
    } else if (compression == Compression::Zstd && !payload.empty()) {
        const size_t bound = ZSTD_compressBound(payload.size());
        if (!ZSTD_isError(bound)) {
            compressed.resize(bound);
            const size_t written = ZSTD_compress(compressed.data(), bound, payload.data(), payload.size(), level);
            if (!ZSTD_isError(written) && written < payload.size()) {
                compressed.resize(written);
                storeCompressed = true;
            }
        }
    }
    // Compression::None, an empty payload, or a compressed form that did not
    // shrink the input all fall through and are stored raw below.

    const size_t base = out.size();
    out.resize(base + kChunkHeaderSize);
    uint8_t* headerPtr = out.data() + base;
    std::memcpy(headerPtr, name, 4);
    bit::writeU32LE(headerPtr + 4, storeCompressed ? static_cast<uint32_t>(compressed.size()) : 0);
    bit::writeU32LE(headerPtr + 8, static_cast<uint32_t>(payload.size()));
    bit::writeU32LE(headerPtr + 12, 0);

    if (storeCompressed) {
        out.insert(out.end(), compressed.begin(), compressed.end());
    } else {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return Status();
}

}  // namespace binary
}  // namespace rbxl
