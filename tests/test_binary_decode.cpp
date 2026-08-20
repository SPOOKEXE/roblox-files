#include <doctest.h>
#include "binary/decode.hpp"
#include "binary/chunk.hpp"
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

// Helpers for assembling chunk payloads by hand.
static void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.resize(v.size() + 4);
    bit::writeU32LE(v.data() + v.size() - 4, x);
}
static void putString(std::vector<uint8_t>& v, const std::string& s) {
    putU32(v, static_cast<uint32_t>(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}
static void putRefArray(std::vector<uint8_t>& v, const std::vector<int32_t>& refs) {
    std::vector<uint8_t> flat(refs.size() * 4), woven(refs.size() * 4);
    int32_t prev = 0;
    for (size_t i = 0; i < refs.size(); ++i) {
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(refs[i] - prev));
        prev = refs[i];
    }
    bit::interleave(flat.data(), woven.data(), refs.size(), 4);
    v.insert(v.end(), woven.begin(), woven.end());
}

// A file with one Folder named "Root" and one Part named "Child" parented to it.
static std::vector<uint8_t> buildMinimalFile() {
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 2; header.instanceCount = 2;
    writeFileHeader(file, header);

    std::vector<uint8_t> inst0;
    putU32(inst0, 0); putString(inst0, "Folder"); inst0.push_back(0);
    putU32(inst0, 1); putRefArray(inst0, {0});
    REQUIRE(writeChunk(file, "INST", inst0, Compression::None, 0));

    std::vector<uint8_t> inst1;
    putU32(inst1, 1); putString(inst1, "Part"); inst1.push_back(0);
    putU32(inst1, 1); putRefArray(inst1, {1});
    REQUIRE(writeChunk(file, "INST", inst1, Compression::None, 0));

    std::vector<uint8_t> prop0;
    putU32(prop0, 0); putString(prop0, "Name"); prop0.push_back(0x01);
    putString(prop0, "Root");
    REQUIRE(writeChunk(file, "PROP", prop0, Compression::None, 0));

    std::vector<uint8_t> prop1;
    putU32(prop1, 1); putString(prop1, "Name"); prop1.push_back(0x01);
    putString(prop1, "Child");
    REQUIRE(writeChunk(file, "PROP", prop1, Compression::None, 0));

    std::vector<uint8_t> prnt;
    prnt.push_back(0); putU32(prnt, 2);
    putRefArray(prnt, {1, 0});     // children
    putRefArray(prnt, {0, -1});    // parents
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));

    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));
    return file;
}

TEST_CASE("decoder builds instances, properties, and hierarchy") {
    auto file = buildMinimalFile();
    auto result = decode(file.data(), file.size());
    REQUIRE(result);
    Dom& dom = result.value();

    REQUIRE(dom.instanceCount() == 2);
    REQUIRE(dom.roots().size() == 1);
    InstanceId root = dom.roots()[0];
    CHECK(dom.at(root).className == "Folder");
    CHECK(dom.nameOf(root) == "Root");
    REQUIRE(dom.at(root).children.size() == 1);
    InstanceId child = dom.at(root).children[0];
    CHECK(dom.at(child).className == "Part");
    CHECK(dom.nameOf(child) == "Child");
}

TEST_CASE("service markers set isService") {
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 1; header.instanceCount = 1;
    writeFileHeader(file, header);
    std::vector<uint8_t> inst;
    putU32(inst, 0); putString(inst, "Workspace"); inst.push_back(1);   // service
    putU32(inst, 1); putRefArray(inst, {0}); inst.push_back(1);          // marker
    REQUIRE(writeChunk(file, "INST", inst, Compression::None, 0));
    std::vector<uint8_t> prnt; prnt.push_back(0); putU32(prnt, 1);
    putRefArray(prnt, {0}); putRefArray(prnt, {-1});
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));
    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));

    auto result = decode(file.data(), file.size());
    REQUIRE(result);
    CHECK(result.value().at(result.value().roots()[0]).isService);
}

TEST_CASE("unknown property type ids are preserved verbatim") {
    auto file = buildMinimalFile();
    // Rewrite the second PROP chunk's type id byte to an unallocated value.
    // Locate it by scanning for the "Name" string that follows the class id.
    bool patched = false;
    for (size_t i = 0; i + 12 < file.size(); ++i) {
        if (std::memcmp(file.data() + i, "Name", 4) == 0 && file[i + 4] == 0x01) {
            file[i + 4] = 0x7F;   // unknown type id
            patched = true;
            break;
        }
    }
    REQUIRE(patched);

    auto result = decode(file.data(), file.size());
    REQUIRE(result);          // must not fail
    Dom& dom = result.value();
    REQUIRE(dom.unknownChunks().size() == 1);
    CHECK(std::memcmp(dom.unknownChunks()[0].name, "PROP", 4) == 0);
    CHECK_FALSE(dom.unknownChunks()[0].className.empty());
}

TEST_CASE("unrecognised chunk names are preserved verbatim") {
    auto file = buildMinimalFile();
    // Insert a chunk with a name no decoder knows, just before END.
    std::vector<uint8_t> prefix(file.begin(), file.end() - (16 + 9));
    std::vector<uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
    REQUIRE(writeChunk(prefix, "XXXX", payload, Compression::None, 0));
    prefix.insert(prefix.end(), file.end() - (16 + 9), file.end());

    auto result = decode(prefix.data(), prefix.size());
    REQUIRE(result);
    auto& unknown = result.value().unknownChunks();
    REQUIRE(unknown.size() == 1);
    CHECK(std::memcmp(unknown[0].name, "XXXX", 4) == 0);
    CHECK(unknown[0].data == payload);
}

TEST_CASE("Ref properties resolve to instance ids after all INST chunks") {
    // ObjectValue.Value pointing forward at an instance declared later.
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 2; header.instanceCount = 2;
    writeFileHeader(file, header);
    std::vector<uint8_t> inst0;
    putU32(inst0, 0); putString(inst0, "ObjectValue"); inst0.push_back(0);
    putU32(inst0, 1); putRefArray(inst0, {0});
    REQUIRE(writeChunk(file, "INST", inst0, Compression::None, 0));
    std::vector<uint8_t> inst1;
    putU32(inst1, 1); putString(inst1, "Part"); inst1.push_back(0);
    putU32(inst1, 1); putRefArray(inst1, {1});
    REQUIRE(writeChunk(file, "INST", inst1, Compression::None, 0));
    std::vector<uint8_t> prop;
    putU32(prop, 0); putString(prop, "Value"); prop.push_back(0x13);
    putRefArray(prop, {1});
    REQUIRE(writeChunk(file, "PROP", prop, Compression::None, 0));
    std::vector<uint8_t> prnt; prnt.push_back(0); putU32(prnt, 2);
    putRefArray(prnt, {0, 1}); putRefArray(prnt, {-1, -1});
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));
    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));

    auto result = decode(file.data(), file.size());
    REQUIRE(result);
    Dom& dom = result.value();
    const Variant* v = dom.getProperty(0, "Value");
    REQUIRE(v != nullptr);
    InstanceId target = std::get<Ref>(*v).target;
    REQUIRE(dom.valid(target));
    CHECK(dom.at(target).className == "Part");
}

TEST_CASE("corpus: real places decode fully") {
    struct Expect { const char* file; uint32_t classes; uint32_t instances; };
    const Expect cases[] = {
        {"Bladeborne Assets.rbxl", 111, 9280},
        {"Bladeborne Floor 0.rbxl", 144, 6475},
        {"Bladeborne Floor 1.rbxl", 150, 152408},
        {"FusionCore.rbxl", 143, 104184},
        {"RaceAPet.rbxl", 162, 734657},
    };
    for (const auto& c : cases) {
        std::ifstream in(std::string(RBXL_TEST_DATA_DIR) + "/" + c.file, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        CAPTURE(c.file);
        auto result = decode(data.data(), data.size());
        REQUIRE_MESSAGE(result, (result.hasValue() ? "" : result.error().toString()));
        CHECK(result.value().instanceCount() == c.instances);
        CHECK(result.value().roots().size() > 0);
    }
}
