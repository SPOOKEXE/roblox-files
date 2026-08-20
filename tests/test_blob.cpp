#include <doctest.h>
#include <rbxl/blob.hpp>
#include <rbxl/bitutil.hpp>
#include <rbxl/rbxl.hpp>
#include <string>

using namespace rbxl;
using namespace rbxl::blob;

TEST_CASE("tags are null-separated names") {
    // Spec example: Hello, from, Rojo
    std::vector<uint8_t> data{'H','e','l','l','o', 0, 'f','r','o','m', 0, 'R','o','j','o'};
    auto r = parseTags(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 3);
    CHECK(r.value()[0] == "Hello");
    CHECK(r.value()[2] == "Rojo");
    CHECK(serializeTags(r.value()) == data);
}

TEST_CASE("an empty tag blob yields no tags") {
    auto r = parseTags({});
    REQUIRE(r);
    CHECK(r.value().empty());
}

TEST_CASE("material colors are 23 RGB triples") {
    std::vector<uint8_t> data(69, 0);
    data[0] = 10; data[1] = 20; data[2] = 30;
    auto r = parseMaterialColors(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 23);
    CHECK(r.value()[0].r == 10);
    CHECK(r.value()[0].b == 30);
    CHECK(serializeMaterialColors(r.value()) == data);
    CHECK_FALSE(parseMaterialColors(std::vector<uint8_t>(68, 0)));
}

TEST_CASE("attributes parse little-endian with their own type ids") {
    std::vector<uint8_t> data;
    auto putU32 = [&](uint32_t v) {
        data.resize(data.size() + 4);
        bit::writeU32LE(data.data() + data.size() - 4, v);
    };
    auto putStr = [&](const std::string& s) {
        putU32(static_cast<uint32_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    };
    putU32(2);
    putStr("Health"); data.push_back(0x05);            // Float32
    data.resize(data.size() + 4);
    bit::writeF32LE(data.data() + data.size() - 4, 100.0f);
    putStr("Boss"); data.push_back(0x03);              // Bool
    data.push_back(0x01);

    auto r = parseAttributes(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0].first == "Health");
    CHECK(std::get<float>(r.value()[0].second) == 100.0f);
    CHECK(r.value()[1].first == "Boss");
    CHECK(std::get<bool>(r.value()[1].second));
    CHECK(serializeAttributes(r.value()) == data);
}

TEST_CASE("a truncated attribute blob is an error, not a crash") {
    std::vector<uint8_t> data{0x05, 0, 0, 0};   // claims 5 attributes, has none
    CHECK_FALSE(parseAttributes(data));
}

// --- Real-data vectors, hand-decoded and confirmed against the corpus place ---

TEST_CASE("real vector: single String attribute Material=Rock") {
    std::vector<uint8_t> data{
        0x01, 0x00, 0x00, 0x00,                                     // count = 1
        0x08, 0x00, 0x00, 0x00,                                     // name length = 8
        'M','a','t','e','r','i','a','l',                            // name
        0x02,                                                       // type = String
        0x04, 0x00, 0x00, 0x00,                                     // value length = 4
        'R','o','c','k',                                            // value
    };
    REQUIRE(data.size() == 25);
    auto r = parseAttributes(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].first == "Material");
    CHECK(std::get<std::string>(r.value()[0].second) == "Rock");
    CHECK(serializeAttributes(r.value()) == data);
}

TEST_CASE("real vector: single Bool attribute Toggle=false") {
    std::vector<uint8_t> data{
        0x01, 0x00, 0x00, 0x00,             // count = 1
        0x06, 0x00, 0x00, 0x00,             // name length = 6
        'T','o','g','g','l','e',            // name
        0x03,                               // type = Bool
        0x00,                               // value = false
    };
    REQUIRE(data.size() == 16);
    auto r = parseAttributes(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].first == "Toggle");
    CHECK_FALSE(std::get<bool>(r.value()[0].second));
    CHECK(serializeAttributes(r.value()) == data);
}

TEST_CASE("real vector: single Bool attribute Toggle=true") {
    std::vector<uint8_t> data{
        0x01, 0x00, 0x00, 0x00,             // count = 1
        0x06, 0x00, 0x00, 0x00,             // name length = 6
        'T','o','g','g','l','e',            // name
        0x03,                               // type = Bool
        0x01,                               // value = true
    };
    REQUIRE(data.size() == 16);
    auto r = parseAttributes(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].first == "Toggle");
    CHECK(std::get<bool>(r.value()[0].second));
    CHECK(serializeAttributes(r.value()) == data);
}

// --- Corpus gate: every AttributesSerialize blob in the real place file ------

TEST_CASE("corpus: every AttributesSerialize blob in the real place round-trips") {
    const std::string path =
        std::string(RBXL_TEST_DATA_DIR) + "/place 101949297449238 Build An Island.rbxlx";
    auto loaded = loadFile(path);
    REQUIRE(loaded);
    const Dom& dom = loaded.value();

    std::size_t attributeBlobCount = 0;
    std::size_t materialColorsCount = 0;

    for (InstanceId id = 0; id < dom.instanceCount(); ++id) {
        if (const Variant* attrsProp = dom.getProperty(id, "AttributesSerialize")) {
            const BinaryString* bs = std::get_if<BinaryString>(attrsProp);
            REQUIRE(bs);
            ++attributeBlobCount;

            auto parsed = parseAttributes(bs->data);
            CAPTURE(id);
            REQUIRE(parsed);
            CHECK(serializeAttributes(parsed.value()) == bs->data);
        }

        if (const Variant* mcProp = dom.getProperty(id, "MaterialColors")) {
            const BinaryString* bs = std::get_if<BinaryString>(mcProp);
            REQUIRE(bs);
            ++materialColorsCount;

            CHECK(bs->data.size() == 69);
            auto parsed = parseMaterialColors(bs->data);
            REQUIRE(parsed);
            CHECK(parsed.value().size() == 23);
            CHECK(serializeMaterialColors(parsed.value()) == bs->data);
        }
    }

    CHECK(attributeBlobCount == 208347);
    CHECK(materialColorsCount == 1);
}
