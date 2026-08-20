#include <doctest.h>
#include "binary/chunk.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

static std::vector<uint8_t> bytes(const char* s, size_t n) {
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(s),
                                reinterpret_cast<const uint8_t*>(s) + n);
}

TEST_CASE("file header round-trips") {
    std::vector<uint8_t> out;
    FileHeader in; in.classCount = 3; in.instanceCount = 17;
    writeFileHeader(out, in);
    REQUIRE(out.size() == kFileHeaderSize);
    CHECK(std::memcmp(out.data(), "<roblox!", 8) == 0);
    CHECK(out[8] == 0x89); CHECK(out[9] == 0xff); CHECK(out[10] == 0x0d);
    CHECK(out[11] == 0x0a); CHECK(out[12] == 0x1a); CHECK(out[13] == 0x0a);

    auto parsed = readFileHeader(out.data(), out.size());
    REQUIRE(parsed);
    CHECK(parsed.value().classCount == 3);
    CHECK(parsed.value().instanceCount == 17);
}

TEST_CASE("file header rejects bad input") {
    auto tooShort = readFileHeader(reinterpret_cast<const uint8_t*>("<roblox!"), 8);
    REQUIRE_FALSE(tooShort);
    CHECK(tooShort.error().code == ErrorCode::Truncated);

    std::vector<uint8_t> wrong(kFileHeaderSize, 0);
    std::memcpy(wrong.data(), "<xml----", 8);
    auto badMagic = readFileHeader(wrong.data(), wrong.size());
    REQUIRE_FALSE(badMagic);
    CHECK(badMagic.error().code == ErrorCode::BadMagic);
}

TEST_CASE("uncompressed chunks round-trip") {
    std::vector<uint8_t> file;
    auto payload = bytes("</roblox>", 9);
    REQUIRE(writeChunk(file, "END\0", payload, Compression::None, 0));
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE(chunk);
    CHECK(std::memcmp(chunk.value().name, "END\0", 4) == 0);
    CHECK(chunk.value().data == payload);
    CHECK(cursor == file.size());
}

TEST_CASE("lz4 chunks round-trip") {
    std::vector<uint8_t> payload(8192, 0x5A);
    std::vector<uint8_t> file;
    REQUIRE(writeChunk(file, "PROP", payload, Compression::Lz4, 0));
    // Compression must actually have happened.
    CHECK(file.size() < payload.size());
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE(chunk);
    CHECK(chunk.value().data == payload);
}

TEST_CASE("zstd chunks round-trip and are detected by magic") {
    std::vector<uint8_t> payload(8192, 0x3C);
    std::vector<uint8_t> file;
    REQUIRE(writeChunk(file, "PROP", payload, Compression::Zstd, 3));
    // The compressed body starts right after the 16-byte chunk header.
    CHECK(file[kChunkHeaderSize + 0] == 0x28);
    CHECK(file[kChunkHeaderSize + 1] == 0xb5);
    CHECK(file[kChunkHeaderSize + 2] == 0x2f);
    CHECK(file[kChunkHeaderSize + 3] == 0xfd);
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE(chunk);
    CHECK(chunk.value().data == payload);
}

TEST_CASE("truncated chunk payloads are reported, not read past") {
    std::vector<uint8_t> payload(64, 1);
    std::vector<uint8_t> file;
    REQUIRE(writeChunk(file, "PROP", payload, Compression::None, 0));
    file.resize(file.size() - 8);
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE_FALSE(chunk);
    CHECK(chunk.error().code == ErrorCode::Truncated);
}

TEST_CASE("a declared uncompressed length that is absurd is rejected") {
    // Guards against a malicious or corrupt file forcing a huge allocation.
    std::vector<uint8_t> file(kChunkHeaderSize, 0);
    std::memcpy(file.data(), "PROP", 4);
    bit::writeU32LE(file.data() + 4, 4);           // compressed length
    bit::writeU32LE(file.data() + 8, 0xFFFFFFFFu); // uncompressed length
    file.insert(file.end(), {0x01, 0x02, 0x03, 0x04});
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    CHECK_FALSE(chunk);
}

#include <fstream>

static std::vector<uint8_t> readFileOrEmpty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

TEST_CASE("corpus: real place files parse their header and chunk stream") {
    const char* names[] = {"Bladeborne Assets.rbxl", "Bladeborne Floor 0.rbxl",
                           "Bladeborne Floor 1.rbxl", "FusionCore.rbxl", "RaceAPet.rbxl"};
    for (const char* name : names) {
        auto data = readFileOrEmpty(std::string(RBXL_TEST_DATA_DIR) + "/" + name);
        if (data.empty()) continue;   // corpus not present; not a failure
        CAPTURE(name);
        auto header = readFileHeader(data.data(), data.size());
        REQUIRE(header);
        CHECK(header.value().instanceCount > 0);

        size_t cursor = kFileHeaderSize;
        size_t instChunks = 0, propChunks = 0;
        bool sawEnd = false;
        while (cursor < data.size()) {
            auto chunk = readChunk(data.data(), data.size(), cursor);
            REQUIRE(chunk);
            if (std::memcmp(chunk.value().name, "INST", 4) == 0) ++instChunks;
            if (std::memcmp(chunk.value().name, "PROP", 4) == 0) ++propChunks;
            if (std::memcmp(chunk.value().name, "END\0", 4) == 0) { sawEnd = true; break; }
        }
        CHECK(sawEnd);
        CHECK(instChunks == header.value().classCount);
        CHECK(propChunks > 0);
    }
}
