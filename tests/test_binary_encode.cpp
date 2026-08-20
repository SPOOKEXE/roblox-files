#include <doctest.h>
#include "binary/encode.hpp"
#include "binary/decode.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

// Helpers for assembling chunk payloads by hand, mirroring
// test_binary_decode.cpp's helpers of the same name.
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

// A file with one Folder instance (classId 0) and two Part instances
// (classId 1), plus one PROP chunk on Part using an unrecognised type id so
// the decoder preserves it verbatim in dom.unknownChunks() instead of
// decoding it into instance properties. Folder and Part deliberately have
// different instance counts and names, so a test can tell whether a
// preserved chunk's patched class id resolves back to the right class (Part,
// 2 instances) rather than merely to *some* valid class (e.g. Folder, 1
// instance) by accident.
static std::vector<uint8_t> buildFileWithPreservedProp() {
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 2; header.instanceCount = 3;
    writeFileHeader(file, header);

    std::vector<uint8_t> instFolder;
    putU32(instFolder, 0); putString(instFolder, "Folder"); instFolder.push_back(0);
    putU32(instFolder, 1); putRefArray(instFolder, {0});
    REQUIRE(writeChunk(file, "INST", instFolder, Compression::None, 0));

    std::vector<uint8_t> instPart;
    putU32(instPart, 1); putString(instPart, "Part"); instPart.push_back(0);
    putU32(instPart, 2); putRefArray(instPart, {1, 2});
    REQUIRE(writeChunk(file, "INST", instPart, Compression::None, 0));

    std::vector<uint8_t> prop;
    putU32(prop, 1); putString(prop, "Mystery"); prop.push_back(0x7F);   // unknown type id
    prop.insert(prop.end(), {0xDE, 0xAD, 0xBE, 0xEF});                   // opaque payload
    REQUIRE(writeChunk(file, "PROP", prop, Compression::None, 0));

    std::vector<uint8_t> prnt;
    prnt.push_back(0); putU32(prnt, 3);
    putRefArray(prnt, {0, 1, 2});
    putRefArray(prnt, {-1, -1, -1});
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));

    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));
    return file;
}

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

TEST_CASE("preserved PROP chunk survives re-encoding with a patched class id") {
    auto file = buildFileWithPreservedProp();
    auto decoded = decode(file.data(), file.size());
    REQUIRE(decoded);
    REQUIRE(decoded.value().unknownChunks().size() == 1);
    const RawChunk& original = decoded.value().unknownChunks()[0];
    REQUIRE(std::memcmp(original.name, "PROP", 4) == 0);
    CHECK(original.className == "Part");
    CHECK(original.instanceCount == 2);

    auto encoded = encode(decoded.value());
    REQUIRE(encoded);

    auto reDecoded = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(reDecoded);
    REQUIRE(reDecoded.value().unknownChunks().size() == 1);
    const RawChunk& survived = reDecoded.value().unknownChunks()[0];
    CHECK(std::memcmp(survived.name, "PROP", 4) == 0);
    // If the class id had been patched to the wrong value (e.g. left
    // pointing at Folder, classId 0), this decode would have attributed the
    // chunk to the wrong class and/or the wrong instance count.
    CHECK(survived.className == "Part");
    CHECK(survived.instanceCount == 2);
    // Everything past the 4-byte class id -- the property name, the unknown
    // type id, and the opaque value bytes -- must be untouched.
    REQUIRE(survived.data.size() == original.data.size());
    CHECK(std::equal(survived.data.begin() + 4, survived.data.end(), original.data.begin() + 4));
}

TEST_CASE("preserved PROP chunk is dropped with a warning when the instance count changes") {
    auto file = buildFileWithPreservedProp();
    auto decoded = decode(file.data(), file.size());
    REQUIRE(decoded);
    Dom& dom = decoded.value();
    REQUIRE(dom.unknownChunks().size() == 1);

    dom.create("Part");   // now 3 Part instances; the raw chunk was captured against 2

    EncodeDiagnostics diags;
    auto encoded = encode(dom, {}, &diags);
    REQUIRE(encoded);

    bool sawWarning = false;
    for (const std::string& w : diags.warnings) {
        if (w.find("Part") != std::string::npos) sawWarning = true;
    }
    CHECK(sawWarning);

    auto reDecoded = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(reDecoded);
    CHECK(reDecoded.value().unknownChunks().empty());
}

TEST_CASE("Int32/Int64 mismatch is coerced losslessly without a warning") {
    Dom dom;
    auto a = dom.create("IntValue");
    dom.setProperty(a, "Value", int64_t(42));   // first instance picks the target type: Int64
    auto b = dom.create("IntValue");
    dom.setProperty(b, "Value", int32_t(7));    // second instance: Int32, must coerce to Int64

    EncodeDiagnostics diags;
    auto encoded = encode(dom, {}, &diags);
    REQUIRE(encoded);
    CHECK(diags.warnings.empty());

    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(std::get<int64_t>(*back.value().getProperty(0, "Value")) == 42);
    CHECK(std::get<int64_t>(*back.value().getProperty(1, "Value")) == 7);
}

TEST_CASE("Color3uint8/Color3 mismatch is coerced losslessly without a warning") {
    Dom dom;
    auto a = dom.create("Part");
    dom.setProperty(a, "Tint", Color3{1.0f, 0.5f, 0.0f});   // first instance: target type Color3
    auto b = dom.create("Part");
    dom.setProperty(b, "Tint", Color3uint8{255, 128, 0});   // Color3uint8, must coerce

    EncodeDiagnostics diags;
    auto encoded = encode(dom, {}, &diags);
    REQUIRE(encoded);
    CHECK(diags.warnings.empty());

    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    Color3 c0 = std::get<Color3>(*back.value().getProperty(0, "Tint"));
    Color3 c1 = std::get<Color3>(*back.value().getProperty(1, "Tint"));
    CHECK(c0.r == 1.0f);
    CHECK(c1.r == doctest::Approx(255.0f / 255.0f));
    CHECK(c1.g == doctest::Approx(128.0f / 255.0f));
    CHECK(c1.b == 0.0f);
}

TEST_CASE("incompatible property type mismatch is replaced with the default and warns") {
    Dom dom;
    auto a = dom.create("Part");
    dom.setProperty(a, "Label", std::string("hello"));   // first instance: target type String
    auto b = dom.create("Part");
    dom.setProperty(b, "Label", 3.5f);                    // Float32: not losslessly coercible

    EncodeDiagnostics diags;
    auto encoded = encode(dom, {}, &diags);
    REQUIRE(encoded);
    CHECK_FALSE(diags.warnings.empty());

    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(std::get<std::string>(*back.value().getProperty(0, "Label")) == "hello");
    CHECK(std::get<std::string>(*back.value().getProperty(1, "Label")) == "");   // default
}

TEST_CASE("shared strings whose value/key bytes could collide under a naive delimiter still dedupe correctly") {
    Dom dom;
    // Chosen so that `value + '\x1f' + key` produces byte-identical strings
    // for both pairs, even though (key, value) as a whole differs:
    //   "X"    + '\x1f' + "Y\x1fZ" == "X\x1fY\x1fZ"
    //   "X\x1fY" + '\x1f' + "Z"    == "X\x1fY\x1fZ"
    SharedString first{std::string("Y\x1fZ"), std::string("X")};        // key="Y\x1fZ", value="X"
    SharedString second{std::string("Z"), std::string("X\x1fY")};       // key="Z", value="X\x1fY"

    auto a = dom.create("StringValue");
    dom.setProperty(a, "Payload", first);
    auto b = dom.create("StringValue");
    dom.setProperty(b, "Payload", second);

    auto encoded = encode(dom);
    REQUIRE(encoded);

    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(std::get<SharedString>(*back.value().getProperty(0, "Payload")).value == "X");
    CHECK(std::get<SharedString>(*back.value().getProperty(1, "Payload")).value == "X\x1fY");
}
