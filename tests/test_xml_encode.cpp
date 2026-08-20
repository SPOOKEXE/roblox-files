#include <doctest.h>
#include "xml/encode.hpp"
#include "xml/decode.hpp"

using namespace rbxl;

static Dom roundTrip(const Dom& in) {
    auto text = rbxl::xml::encode(in);
    REQUIRE(text);
    auto out = rbxl::xml::decode(text.value().data(), text.value().size());
    REQUIRE_MESSAGE(out, (out.hasValue() ? "" : out.error().toString()));
    return std::move(out.value());
}

TEST_CASE("encoded XML has the required root and version") {
    Dom dom;
    dom.create("Part");
    auto text = rbxl::xml::encode(dom);
    REQUIRE(text);
    CHECK(text.value().find("<roblox version=\"4\"") != std::string::npos);
}

TEST_CASE("hierarchy and names survive a round-trip") {
    Dom dom;
    auto model = dom.create("Model");
    dom.setProperty(model, "Name", std::string("Rig"));
    auto part = dom.create("Part");
    dom.setProperty(part, "Name", std::string("Head"));
    dom.setParent(part, model);

    Dom back = roundTrip(dom);
    REQUIRE(back.roots().size() == 1);
    CHECK(back.nameOf(back.roots()[0]) == "Rig");
    REQUIRE(back.at(back.roots()[0]).children.size() == 1);
}

TEST_CASE("every value type survives a round-trip") {
    Dom dom;
    auto id = dom.create("Everything");
    auto target = dom.create("Folder");   // referent target for the Ref property below
    dom.setProperty(id, "S", std::string("text"));
    dom.setProperty(id, "B", true);
    dom.setProperty(id, "I32", int32_t{-9});
    dom.setProperty(id, "I64", int64_t{-9000000000LL});
    dom.setProperty(id, "F32", 0.125f);
    dom.setProperty(id, "F64", 0.1);
    dom.setProperty(id, "V2", Vector2{1, 2});
    dom.setProperty(id, "V3", Vector3{1, 2, 3});
    dom.setProperty(id, "V3i", Vector3int16{-1, 2, -3});
    dom.setProperty(id, "C3", Color3{0.25f, 0.5f, 0.75f});
    dom.setProperty(id, "C3u8", Color3uint8{1, 2, 3});
    dom.setProperty(id, "UD", UDim{0.5f, 7});
    dom.setProperty(id, "UD2", UDim2{{0.5f, 7}, {0.25f, -7}});
    dom.setProperty(id, "R", Rect{{0, 1}, {2, 3}});
    dom.setProperty(id, "Ray_", Ray{{1, 2, 3}, {4, 5, 6}});
    dom.setProperty(id, "NR", NumberRange{0.0f, 1.0f});
    dom.setProperty(id, "Fc", Faces{0x26});
    dom.setProperty(id, "Ax", Axes{0x05});
    dom.setProperty(id, "En", EnumValue{256});
    dom.setProperty(id, "Sec", SecurityCapabilities{0x0102030405060708ull});
    CFrame cf; cf.position = {1, 2, 3};
    dom.setProperty(id, "CF", cf);
    OptionalCFrame ocf; ocf.hasValue = true; ocf.value = cf;
    dom.setProperty(id, "OCF", ocf);
    NumberSequence ns; ns.keypoints = {{0, 0, 0}, {1, 1, 0.5f}};
    dom.setProperty(id, "NS", ns);
    ColorSequence cs; cs.keypoints = {{0, Color3{1, 1, 1}, 0}, {1, Color3{0, 0, 0}, 0}};
    dom.setProperty(id, "CS", cs);
    PhysicalProperties pp; pp.custom = true; pp.density = 0.7f; pp.friction = 0.3f;
    dom.setProperty(id, "PP", pp);
    Font font; font.family = "rbxasset://fonts/families/Arial.json";
    font.weight = 700; font.style = 1;
    dom.setProperty(id, "Ft", font);
    dom.setProperty(id, "CId", ContentId{"rbxassetid://1"});
    Content content; content.sourceType = Content::SourceType::Uri; content.uri = "rbxassetid://2";
    dom.setProperty(id, "Cn", content);
    dom.setProperty(id, "PS", ProtectedString{"local x = 1\n"});
    dom.setProperty(id, "BS", BinaryString{{0x00, 0xFF, 0x10}});
    dom.setProperty(id, "Rf", Ref{target});
    dom.setProperty(id, "UId", UniqueId{42, 1000, -7000000000000LL});
    dom.setProperty(id, "BC", BrickColor{1000});
    dom.setProperty(id, "SS", SharedString{"sskey", "shared string payload"});
    dom.setProperty(id, "NAR", NetAssetRef{"narkey", "net asset payload"});

    Dom back = roundTrip(dom);
    for (const auto& prop : dom.at(id).properties) {
        const std::string& name = dom.names().name(prop.first);
        CAPTURE(name);
        const Variant* got = back.getProperty(0, name);
        REQUIRE(got != nullptr);
        CHECK(variantTypeOf(*got) == variantTypeOf(prop.second));
        CHECK(variantEqual(*got, prop.second));
    }

    // Ref is checked again explicitly: variantEqual alone would also pass if
    // both sides collapsed to kNoInstance, so pin that the referent actually
    // resolved to the other instance rather than going missing.
    const Variant* backRef = back.getProperty(0, "Rf");
    REQUIRE(backRef != nullptr);
    const InstanceId resolved = std::get<Ref>(*backRef).target;
    CHECK(resolved != kNoInstance);
    REQUIRE(back.valid(resolved));
    CHECK(back.at(resolved).className == "Folder");
}

TEST_CASE("Font writes Style as a name and omits an empty CachedFaceId") {
    Dom dom;
    auto id = dom.create("TextLabel");
    Font f; f.family = "rbxasset://fonts/families/Arial.json"; f.weight = 700; f.style = 1;
    dom.setProperty(id, "FontFace", f);
    auto text = rbxl::xml::encode(dom);
    REQUIRE(text);
    CHECK(text.value().find("<Style>Italic</Style>") != std::string::npos);
    CHECK(text.value().find("<Weight>700</Weight>") != std::string::npos);
    CHECK(text.value().find("CachedFaceId") == std::string::npos);
}

TEST_CASE("ProtectedString is written as CDATA") {
    Dom dom;
    auto id = dom.create("Script");
    dom.setProperty(id, "Source", ProtectedString{"if a < b and c > d then end"});
    auto text = rbxl::xml::encode(dom);
    REQUIRE(text);
    CHECK(text.value().find("<![CDATA[") != std::string::npos);
    Dom back = roundTrip(dom);
    CHECK(std::get<ProtectedString>(*back.getProperty(0, "Source")).value ==
          "if a < b and c > d then end");
}

TEST_CASE("a ProtectedString containing the CDATA terminator still round-trips") {
    Dom dom;
    auto id = dom.create("Script");
    dom.setProperty(id, "Source", ProtectedString{"local s = \"]]>\""});
    Dom back = roundTrip(dom);
    CHECK(std::get<ProtectedString>(*back.getProperty(0, "Source")).value ==
          "local s = \"]]>\"");
}

TEST_CASE("floats round-trip exactly through their text form") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "A", 0.1f);
    dom.setProperty(id, "B", 3.4028235e38f);
    dom.setProperty(id, "C", 1.2345678901234567);
    Dom back = roundTrip(dom);
    CHECK(std::get<float>(*back.getProperty(0, "A")) == 0.1f);
    CHECK(std::get<float>(*back.getProperty(0, "B")) == 3.4028235e38f);
    CHECK(std::get<double>(*back.getProperty(0, "C")) == 1.2345678901234567);
}

// Guards against decode.cpp's elementTypeTable() and encode.cpp's
// elementNameFor()/writeProperty() switch silently drifting apart: three
// hand-maintained copies of the same name-to-type mapping that agree today
// but have nothing structurally forcing them to keep agreeing. This walks
// every VariantType, asks the encoder for the element name it writes (if
// any), and checks that name resolves back to the same type through the
// decoder's own table -- so a future edit to one side that the other
// doesn't match fails here instead of reaching a real file undetected.
TEST_CASE("the encoder's element names round-trip through the decoder's own table") {
    // No Appendix A.3 entry, and never produced by either decoder today (see
    // elementNameFor's comment in encode.cpp); pinned here so a type quietly
    // gaining or losing representability is itself a test failure.
    static const VariantType kNoXmlRepresentation[] = {
        VariantType::Nil,       VariantType::Vector2int16, VariantType::Region3,
        VariantType::Region3int16, VariantType::EnumItem,  VariantType::Bytecode,
    };
    auto hasNoRepresentation = [](VariantType t) {
        for (VariantType n : kNoXmlRepresentation) {
            if (n == t) return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < std::variant_size_v<Variant>; ++i) {
        const VariantType type = static_cast<VariantType>(i);
        CAPTURE(variantTypeName(type));

        const char* elementName = nullptr;
        const bool hasName = rbxl::xml::elementNameFor(type, elementName);

        if (hasNoRepresentation(type)) {
            CHECK_FALSE(hasName);
            continue;
        }
        REQUIRE(hasName);

        // Content and ContentId are the one deliberate exception: decode.cpp
        // disambiguates them structurally (decodeContentLike), by which
        // child element is present, not by an elementTypeTable() lookup on
        // their own element name, so they are legitimately absent from that
        // table. Their round-trip correctness is covered separately by the
        // "every value type" case above and by test_xml_decode.cpp's own
        // "Content is distinguished from ContentId" case.
        if (type == VariantType::Content || type == VariantType::ContentId) continue;

        const auto& table = rbxl::xml::elementTypeTable();
        auto it = table.find(elementName);
        REQUIRE(it != table.end());
        CHECK(it->second == type);
    }
}
