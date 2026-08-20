#include <doctest.h>
#include <rbxl/blob.hpp>
#include <rbxl/bitutil.hpp>
#include <rbxl/rbxl.hpp>
#include <set>
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

TEST_CASE("a four-zero-byte blob (explicit count=0) parses to an empty map") {
    // Real Roblox files always use a zero-length buffer for "no attributes"
    // (see the corpus test below), never this four-byte "count = 0" form,
    // but parseAttributes still accepts it: a declared count of zero simply
    // yields no records. serializeAttributes then normalises this away and
    // reports zero bytes rather than reproducing the four-byte input; see
    // the comment on serializeAttributes for why that one-sided exception
    // is deliberate and harmless.
    std::vector<uint8_t> data{0x00, 0x00, 0x00, 0x00};
    auto r = parseAttributes(data);
    REQUIRE(r);
    CHECK(r.value().empty());
    CHECK(serializeAttributes(r.value()).empty());
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
    // 205,426 of the 208,347 AttributesSerialize blobs in this file are
    // byte-identical empty payloads; re-parsing and re-serialising each one
    // individually would just recheck "empty round-trips to empty" outright
    // 205k times over, which is a smoke test wearing a corpus test's
    // clothes. Walking every instance still proves the total blob count
    // (the actual corpus gate, and cheap to check), but the expensive
    // parse-and-round-trip assertion runs once per DISTINCT payload, which
    // has identical defect-detection power since duplicate bytes cannot
    // reach any code path the first copy did not.
    const std::string path =
        std::string(RBXL_TEST_DATA_DIR) + "/place 101949297449238 Build An Island.rbxlx";
    auto loaded = loadFile(path);
    REQUIRE(loaded);
    const Dom& dom = loaded.value();

    std::size_t attributeBlobCount = 0;
    std::size_t materialColorsCount = 0;
    std::set<std::vector<uint8_t>> distinctAttributePayloads;

    for (InstanceId id = 0; id < dom.instanceCount(); ++id) {
        if (const Variant* attrsProp = dom.getProperty(id, "AttributesSerialize")) {
            const BinaryString* bs = std::get_if<BinaryString>(attrsProp);
            REQUIRE(bs);
            ++attributeBlobCount;
            distinctAttributePayloads.insert(bs->data);
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

    // distinctAttributePayloads naturally includes exactly one empty entry
    // (the zero-length "no attributes" payload) alongside the 1,047 distinct
    // non-empty ones, so no separate representative-empty case is needed.
    for (const auto& payload : distinctAttributePayloads) {
        CAPTURE(payload.size());
        auto parsed = parseAttributes(payload);
        REQUIRE(parsed);
        CHECK(serializeAttributes(parsed.value()) == payload);
    }
}
