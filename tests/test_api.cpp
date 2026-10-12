#include <doctest.h>
#include <rbxl/rbxl.hpp>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace rbxl;

TEST_CASE("format detection recognises both containers") {
    const char binary[] = "<roblox!\x89\xff\x0d\x0a\x1a\x0a";
    auto b = detectFormat(reinterpret_cast<const uint8_t*>(binary), sizeof(binary) - 1);
    REQUIRE(b);
    CHECK(b.value() == Format::Binary);

    const char xml[] = "<roblox version=\"4\"></roblox>";
    auto x = detectFormat(reinterpret_cast<const uint8_t*>(xml), sizeof(xml) - 1);
    REQUIRE(x);
    CHECK(x.value() == Format::Xml);
}

TEST_CASE("format detection skips a BOM, whitespace, comments, and a declaration") {
    const char xml[] = "\xEF\xBB\xBF  <?xml version=\"1.0\"?>\n<!-- note -->\n"
                       "<roblox version=\"4\"></roblox>";
    auto r = detectFormat(reinterpret_cast<const uint8_t*>(xml), sizeof(xml) - 1);
    REQUIRE(r);
    CHECK(r.value() == Format::Xml);
}

TEST_CASE("format detection rejects unrelated data") {
    const char junk[] = "PK\x03\x04 this is a zip";
    auto r = detectFormat(reinterpret_cast<const uint8_t*>(junk), sizeof(junk) - 1);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::BadMagic);
}

TEST_CASE("extension mapping covers all four suffixes") {
    CHECK(formatFromExtension("a.rbxl").value() == Format::Binary);
    CHECK(formatFromExtension("a.rbxm").value() == Format::Binary);
    CHECK(formatFromExtension("a.rbxlx").value() == Format::Xml);
    CHECK(formatFromExtension("a.rbxmx").value() == Format::Xml);
    CHECK(formatFromExtension("A.RBXMX").value() == Format::Xml);
    CHECK_FALSE(formatFromExtension("a.txt"));
}

TEST_CASE("save then load round-trips through every format on disk") {
    Dom dom;
    auto model = dom.create("Model");
    dom.setProperty(model, "Name", std::string("Sample"));
    auto part = dom.create("Part");
    dom.setProperty(part, "Size", Vector3{4, 1, 2});
    dom.setParent(part, model);

    for (const char* suffix : {".rbxl", ".rbxm", ".rbxlx", ".rbxmx"}) {
        std::string path = std::string("rbxl_api_test") + suffix;
        CAPTURE(path);
        REQUIRE(saveFile(dom, path));
        auto loaded = loadFile(path);
        REQUIRE_MESSAGE(loaded, (loaded.hasValue() ? "" : loaded.error().toString()));
        CHECK(loaded.value().instanceCount() == 2);
        CHECK(loaded.value().nameOf(loaded.value().roots()[0]) == "Sample");
        std::remove(path.c_str());
    }
}

TEST_CASE("cross-format conversion preserves the tree") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Converted"));
    dom.setProperty(id, "Size", Vector3{1, 2, 3});

    SaveOptions toXml; toXml.format = Format::Xml;
    auto xmlBytes = saveBuffer(dom, toXml);
    REQUIRE(xmlBytes);
    auto viaXml = loadBuffer(xmlBytes.value().data(), xmlBytes.value().size());
    REQUIRE(viaXml);

    SaveOptions toBinary; toBinary.format = Format::Binary;
    auto binBytes = saveBuffer(viaXml.value(), toBinary);
    REQUIRE(binBytes);
    auto viaBinary = loadBuffer(binBytes.value().data(), binBytes.value().size());
    REQUIRE(viaBinary);

    CHECK(viaBinary.value().nameOf(0) == "Converted");
    CHECK(std::get<Vector3>(*viaBinary.value().getProperty(0, "Size")).z == 3.0f);
}

TEST_CASE("a missing file reports Io rather than crashing") {
    auto r = loadFile("definitely-not-here.rbxl");
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Io);
}

// Reads a file's raw bytes back and reports which container it sniffs as, so
// tests can check what saveFile actually wrote on disk rather than relying
// on loadFile's own (equally content-based) sniffing to mask a bug.
static Format writtenFormatOf(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    auto r = detectFormat(bytes.data(), bytes.size());
    REQUIRE(r);
    return r.value();
}

TEST_CASE("saveFile still infers format from the extension when an unrelated option changes") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Unrelated"));

    SaveOptions options;
    options.level = 5;   // touches compression level only, not format
    std::string path = "rbxl_api_test_unrelated.rbxmx";
    REQUIRE(saveFile(dom, path, options));
    CHECK(writtenFormatOf(path) == Format::Xml);
    std::remove(path.c_str());
}

TEST_CASE("saveFile honours an explicit format over a mismatched extension") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Explicit"));

    SaveOptions options;
    options.format = Format::Binary;
    std::string path = "rbxl_api_test_explicit.rbxmx";
    REQUIRE(saveFile(dom, path, options));
    CHECK(writtenFormatOf(path) == Format::Binary);
    std::remove(path.c_str());
}

TEST_CASE("saveFile falls back to Binary and warns on an unrecognised extension") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Fallback"));

    std::string path = "rbxl_api_test_fallback.txt";
    Diagnostics diagnostics;
    REQUIRE(saveFile(dom, path, {}, &diagnostics));
    CHECK(writtenFormatOf(path) == Format::Binary);
    CHECK(diagnostics.warnings.size() == 1);
    std::remove(path.c_str());
}

TEST_CASE("binary XML binary conversion preserves string bytes and script spacing") {
    Dom dom;
    auto first = dom.create("Folder");
    auto second = dom.create("Folder");
    dom.setProperty(first, "Name", std::string("First"));
    dom.setProperty(second, "Name", std::string("Second"));
    const std::string blob("\x01\0\xff payload", 11);
    SUBCASE("text value precedes binary value") {
        dom.setProperty(first, "AttributesSerialize", std::string());
        dom.setProperty(second, "AttributesSerialize", blob);
    }
    SUBCASE("binary value precedes text value") {
        dom.setProperty(first, "AttributesSerialize", blob);
        dom.setProperty(second, "AttributesSerialize", std::string());
    }
    auto script = dom.create("ModuleScript");
    dom.setProperty(script, "Name", std::string("Rules"));
    const std::string source = "\nlocal Module = {}\n\nfunction Module.CanAct()\n\treturn true\nend\n\nreturn Module\n";
    dom.setProperty(script, "Source", ProtectedString{source});

    auto binary = saveBuffer(dom, {});
    REQUIRE(binary);
    auto loaded = loadBuffer(binary.value().data(), binary.value().size());
    REQUIRE(loaded);
    SaveOptions xmlOptions;
    xmlOptions.format = Format::Xml;
    auto xml = saveBuffer(loaded.value(), xmlOptions);
    REQUIRE(xml);
    const std::string xmlText(xml.value().begin(), xml.value().end());
    CHECK(xmlText.find("<BinaryString name=\"AttributesSerialize\"") != std::string::npos);
    CHECK(xmlText.find("&#01;") == std::string::npos);
    auto fromXml = loadBuffer(xml.value().data(), xml.value().size());
    REQUIRE(fromXml);
    Diagnostics diagnostics;
    auto backBytes = saveBuffer(fromXml.value(), {}, &diagnostics);
    REQUIRE(backBytes);
    CHECK(diagnostics.warnings.empty());
    auto back = loadBuffer(backBytes.value().data(), backBytes.value().size());
    REQUIRE(back);
    for (InstanceId id : {first, second, script}) {
        InstanceId restored = kNoInstance;
        for (InstanceId candidate = 0; candidate < back.value().instanceCount(); ++candidate) {
            if (back.value().nameOf(candidate) == dom.nameOf(id)) restored = candidate;
        }
        REQUIRE(restored != kNoInstance);
        const char* property = id == script ? "Source" : "AttributesSerialize";
        const auto* expected = loaded.value().getProperty(id, property);
        const auto* actual = back.value().getProperty(restored, property);
        REQUIRE(expected != nullptr);
        REQUIRE(actual != nullptr);
        CHECK(std::get<std::string>(*actual) == std::get<std::string>(*expected));
    }
}
