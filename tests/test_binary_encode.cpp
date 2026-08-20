#include <doctest.h>
#include "binary/encode.hpp"
#include "binary/decode.hpp"
#include <cstring>
#include <fstream>
#include <string>

using namespace rbxl;
using namespace rbxl::binary;

static Dom buildSample() {
    Dom dom;
    auto model = dom.create("Model");
    dom.setProperty(model, "Name", std::string("Rig"));
    auto part = dom.create("Part");
    dom.setProperty(part, "Name", std::string("Head"));
    dom.setProperty(part, "Size", Vector3{2, 1, 1});
    dom.setProperty(part, "Anchored", true);
    dom.setParent(part, model);
    return dom;
}

TEST_CASE("encoded output is a well-formed file that decodes back") {
    Dom dom = buildSample();
    auto encoded = encode(dom);
    REQUIRE(encoded);
    CHECK(std::memcmp(encoded.value().data(), "<roblox!", 8) == 0);
    // The END magic must be the last nine bytes and uncompressed.
    CHECK(std::memcmp(encoded.value().data() + encoded.value().size() - 9, "</roblox>", 9) == 0);

    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    Dom& d = back.value();
    REQUIRE(d.instanceCount() == 2);
    REQUIRE(d.roots().size() == 1);
    InstanceId model = d.roots()[0];
    CHECK(d.at(model).className == "Model");
    CHECK(d.nameOf(model) == "Rig");
    REQUIRE(d.at(model).children.size() == 1);
    InstanceId part = d.at(model).children[0];
    CHECK(d.nameOf(part) == "Head");
    CHECK(std::get<Vector3>(*d.getProperty(part, "Size")).y == 1.0f);
    CHECK(std::get<bool>(*d.getProperty(part, "Anchored")));
}

TEST_CASE("all three compression modes produce decodable files") {
    Dom dom = buildSample();
    for (auto mode : {Compression::None, Compression::Lz4, Compression::Zstd}) {
        EncodeOptions options; options.compression = mode;
        auto encoded = encode(dom, options);
        REQUIRE(encoded);
        auto back = decode(encoded.value().data(), encoded.value().size());
        REQUIRE(back);
        CHECK(back.value().instanceCount() == 2);
    }
}

TEST_CASE("Ref properties survive re-encoding") {
    Dom dom;
    auto holder = dom.create("ObjectValue");
    auto target = dom.create("Part");
    dom.setProperty(holder, "Value", Ref{target});
    auto encoded = encode(dom);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    const Variant* v = back.value().getProperty(0, "Value");
    REQUIRE(v != nullptr);
    CHECK(back.value().at(std::get<Ref>(*v).target).className == "Part");
}

TEST_CASE("shared strings are pooled and deduplicated") {
    Dom dom;
    SharedString shared{std::string(16, '\0'), "a large repeated payload"};
    for (int i = 0; i < 3; ++i) {
        auto id = dom.create("MeshPart");
        dom.setProperty(id, "PhysicalConfigData", shared);
    }
    auto encoded = encode(dom);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    for (InstanceId id = 0; id < 3; ++id) {
        const Variant* v = back.value().getProperty(id, "PhysicalConfigData");
        REQUIRE(v != nullptr);
        CHECK(std::get<SharedString>(*v).value == "a large repeated payload");
    }
}

TEST_CASE("services keep their service marker through a round-trip") {
    Dom dom;
    auto ws = dom.create("Workspace");
    dom.at(ws).isService = true;
    auto encoded = encode(dom);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(back.value().at(0).isService);
}

TEST_CASE("instances of one class with differing properties are reconciled") {
    Dom dom;
    auto a = dom.create("Part");
    dom.setProperty(a, "Transparency", 0.5f);
    auto b = dom.create("Part");        // no Transparency at all

    EncodeDiagnostics diags;
    auto encoded = encode(dom, {}, &diags);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    // Both instances must now carry the property; the filled one gets the default.
    CHECK(std::get<float>(*back.value().getProperty(0, "Transparency")) == 0.5f);
    CHECK(std::get<float>(*back.value().getProperty(1, "Transparency")) == 0.0f);
}

TEST_CASE("PRNT is written in depth-first post-order") {
    Dom dom;
    auto root = dom.create("Folder");
    auto mid = dom.create("Folder");
    auto leaf = dom.create("Folder");
    dom.setParent(mid, root);
    dom.setParent(leaf, mid);
    auto encoded = encode(dom, {Compression::None, 0, nullptr});
    REQUIRE(encoded);
    // Find the PRNT chunk and confirm the leaf's referent comes first.
    // (Locating it by scanning for the literal name is sufficient here.)
    const auto& bytes = encoded.value();
    size_t at = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())
                    .find("PRNT");
    REQUIRE(at != std::string::npos);
    const uint8_t* payload = bytes.data() + at + kChunkHeaderSize;
    CHECK(payload[0] == 0);                        // version
    CHECK(bit::readU32LE(payload + 1) == 3u);      // instance count
    // First child referent, after deinterleaving a 3-element i32 array.
    uint8_t flat[12];
    bit::deinterleave(payload + 5, flat, 3, 4);
    CHECK(bit::zigzagDecode32(bit::readU32BE(flat)) == static_cast<int32_t>(leaf));
}

TEST_CASE("corpus: decode then encode then decode is stable") {
    const char* names[] = {"Bladeborne Assets.rbxl", "Bladeborne Floor 0.rbxl",
                           "FusionCore.rbxl"};
    for (const char* name : names) {
        std::ifstream in(std::string(RBXL_TEST_DATA_DIR) + "/" + name, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        CAPTURE(name);
        auto first = decode(data.data(), data.size());
        REQUIRE(first);
        auto encoded = encode(first.value());
        REQUIRE(encoded);
        auto second = decode(encoded.value().data(), encoded.value().size());
        REQUIRE(second);
        CHECK(second.value().instanceCount() == first.value().instanceCount());
        CHECK(second.value().roots().size() == first.value().roots().size());
    }
}
