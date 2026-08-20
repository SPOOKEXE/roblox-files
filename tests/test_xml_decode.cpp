#include <doctest.h>
#include "xml/decode.hpp"
#include <cmath>
#include <fstream>
#include <string>

using namespace rbxl;

static Dom decodeOk(const std::string& xml) {
    auto r = rbxl::xml::decode(xml.data(), xml.size());
    REQUIRE_MESSAGE(r, (r.hasValue() ? "" : r.error().toString()));
    return std::move(r.value());
}

TEST_CASE("a leading comment before the root element is tolerated") {
    Dom dom = decodeOk(
        "<!-- Saved by something --><roblox version=\"4\">"
        "<Item class=\"Part\" referent=\"0\"><Properties>"
        "<string name=\"Name\">Baseplate</string></Properties></Item></roblox>");
    REQUIRE(dom.instanceCount() == 1);
    CHECK(dom.at(0).className == "Part");
    CHECK(dom.nameOf(0) == "Baseplate");
}

TEST_CASE("nested Items become a hierarchy") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Model\" referent=\"a\"><Properties/>"
        "<Item class=\"Part\" referent=\"b\"><Properties/></Item></Item></roblox>");
    REQUIRE(dom.instanceCount() == 2);
    REQUIRE(dom.roots().size() == 1);
    InstanceId root = dom.roots()[0];
    CHECK(dom.at(root).className == "Model");
    REQUIRE(dom.at(root).children.size() == 1);
    CHECK(dom.at(dom.at(root).children[0]).className == "Part");
}

TEST_CASE("scalar type elements decode") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<bool name=\"Anchored\">true</bool>"
        "<int name=\"Count\">-7</int>"
        "<int64 name=\"Big\">9000000000</int64>"
        "<float name=\"Transparency\">0.5</float>"
        "<double name=\"Time\">1.25</double>"
        "<token name=\"Material\">256</token>"
        "</Properties></Item></roblox>");
    CHECK(std::get<bool>(*dom.getProperty(0, "Anchored")));
    CHECK(std::get<int32_t>(*dom.getProperty(0, "Count")) == -7);
    CHECK(std::get<int64_t>(*dom.getProperty(0, "Big")) == 9000000000LL);
    CHECK(std::get<float>(*dom.getProperty(0, "Transparency")) == 0.5f);
    CHECK(std::get<double>(*dom.getProperty(0, "Time")) == 1.25);
    CHECK(std::get<EnumValue>(*dom.getProperty(0, "Material")).value == 256u);
}

TEST_CASE("float special values decode") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<float name=\"A\">INF</float><float name=\"B\">-INF</float>"
        "<float name=\"C\">NAN</float></Properties></Item></roblox>");
    CHECK(std::isinf(std::get<float>(*dom.getProperty(0, "A"))));
    CHECK(std::get<float>(*dom.getProperty(0, "B")) < 0);
    CHECK(std::isnan(std::get<float>(*dom.getProperty(0, "C"))));
}

TEST_CASE("composite type elements decode") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<Vector3 name=\"Size\"><X>4</X><Y>1</Y><Z>2</Z></Vector3>"
        "<UDim2 name=\"Pos\"><XS>0.5</XS><XO>10</XO><YS>0.25</YS><YO>-4</YO></UDim2>"
        "<CoordinateFrame name=\"CFrame\"><X>1</X><Y>2</Y><Z>3</Z>"
        "<R00>1</R00><R01>0</R01><R02>0</R02>"
        "<R10>0</R10><R11>1</R11><R12>0</R12>"
        "<R20>0</R20><R21>0</R21><R22>1</R22></CoordinateFrame>"
        "<Color3uint8 name=\"Color\">4294901760</Color3uint8>"
        "<NumberRange name=\"Range\">0 0.5</NumberRange>"
        "<NumberSequence name=\"Seq\">0 0 0 1 1 0 </NumberSequence>"
        "</Properties></Item></roblox>");
    CHECK(std::get<Vector3>(*dom.getProperty(0, "Size")).x == 4.0f);
    auto udim2 = std::get<UDim2>(*dom.getProperty(0, "Pos"));
    CHECK(udim2.x.scale == 0.5f);
    CHECK(udim2.x.offset == 10);
    CHECK(udim2.y.offset == -4);
    auto cf = std::get<CFrame>(*dom.getProperty(0, "CFrame"));
    CHECK(cf.position.z == 3.0f);
    CHECK(cf.rotation[0] == 1.0f);
    // 4294901760 == 0xFFFF0000: alpha FF, red FF, green 00, blue 00.
    auto color = std::get<Color3uint8>(*dom.getProperty(0, "Color"));
    CHECK(color.r == 255); CHECK(color.g == 0); CHECK(color.b == 0);
    CHECK(std::get<NumberRange>(*dom.getProperty(0, "Range")).max == 0.5f);
    CHECK(std::get<NumberSequence>(*dom.getProperty(0, "Seq")).keypoints.size() == 2);
}

TEST_CASE("Content is distinguished from ContentId by its child element") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        // Legacy shape, written under the modern element name by third-party tools.
        "<Content name=\"AnimationId\"><url>rbxassetid://123</url></Content>"
        // Modern shape.
        "<Content name=\"Image\"><uri>rbxassetid://456</uri></Content>"
        "<Content name=\"Empty\"><null></null></Content>"
        "</Properties></Item></roblox>");
    const Variant* legacy = dom.getProperty(0, "AnimationId");
    REQUIRE(legacy);
    CHECK(variantTypeOf(*legacy) == VariantType::ContentId);
    CHECK(std::get<ContentId>(*legacy).url == "rbxassetid://123");

    const Variant* modern = dom.getProperty(0, "Image");
    REQUIRE(modern);
    CHECK(variantTypeOf(*modern) == VariantType::Content);
    CHECK(std::get<Content>(*modern).sourceType == Content::SourceType::Uri);

    CHECK(std::get<Content>(*dom.getProperty(0, "Empty")).sourceType ==
          Content::SourceType::None);
}

TEST_CASE("ProtectedString keeps whitespace and CDATA contents exactly") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Script\" referent=\"0\"><Properties>"
        "<ProtectedString name=\"Source\"><![CDATA[local x = 1\n\n  print(x)\n]]>"
        "</ProtectedString></Properties></Item></roblox>");
    CHECK(std::get<ProtectedString>(*dom.getProperty(0, "Source")).value ==
          "local x = 1\n\n  print(x)\n");
}

TEST_CASE("Ref elements resolve, and null means no value") {
    Dom dom = decodeOk(
        "<roblox version=\"4\">"
        "<Item class=\"ObjectValue\" referent=\"RBXA\"><Properties>"
        "<Ref name=\"Value\">RBXB</Ref></Properties></Item>"
        "<Item class=\"ObjectValue\" referent=\"RBXB\"><Properties>"
        "<Ref name=\"Value\">null</Ref></Properties></Item></roblox>");
    InstanceId target = std::get<Ref>(*dom.getProperty(0, "Value")).target;
    REQUIRE(dom.valid(target));
    CHECK(dom.at(target).className == "ObjectValue");
    CHECK(std::get<Ref>(*dom.getProperty(1, "Value")).target == kNoInstance);
}

TEST_CASE("SharedStrings resolve through the file's table") {
    Dom dom = decodeOk(
        "<roblox version=\"4\">"
        "<Item class=\"Part\" referent=\"0\"><Properties>"
        "<SharedString name=\"Blob\">a2V5</SharedString></Properties></Item>"
        "<SharedStrings><SharedString md5=\"a2V5\">Zm9vYmFy</SharedString></SharedStrings>"
        "</roblox>");
    CHECK(std::get<SharedString>(*dom.getProperty(0, "Blob")).value == "foobar");
}

TEST_CASE("BinaryString contents are base64-decoded") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<BinaryString name=\"Tags\">Um9qbyBpcyBjb29sIQ==</BinaryString>"
        "</Properties></Item></roblox>");
    const auto& data = std::get<BinaryString>(*dom.getProperty(0, "Tags")).data;
    CHECK(std::string(data.begin(), data.end()) == "Rojo is cool!");
}

TEST_CASE("Meta elements land in the Dom metadata") {
    Dom dom = decodeOk("<roblox version=\"4\">"
                       "<Meta name=\"ExplicitAutoJoints\">true</Meta></roblox>");
    REQUIRE(dom.metadata().size() == 1);
    CHECK(dom.metadata()[0].first == "ExplicitAutoJoints");
    CHECK(dom.metadata()[0].second == "true");
}

TEST_CASE("a missing or wrong root element is an error") {
    auto r = rbxl::xml::decode("<nope/>", 7);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Malformed);
}

TEST_CASE("corpus: the large XML place decodes") {
    std::ifstream in(std::string(RBXL_TEST_DATA_DIR) +
                     "/place 101949297449238 Build An Island.rbxlx", std::ios::binary);
    if (!in) return;   // corpus not present
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto r = rbxl::xml::decode(data.data(), data.size());
    REQUIRE_MESSAGE(r, (r.hasValue() ? "" : r.error().toString()));
    Dom& dom = r.value();
    CHECK(dom.instanceCount() > 1000);
    // This file is the project's gate on modern types; all four must appear.
    bool sawContentId = false, sawFont = false, sawSecurity = false, sawOptionalCFrame = false;
    for (InstanceId id = 0; id < dom.instanceCount(); ++id) {
        for (const auto& prop : dom.at(id).properties) {
            switch (variantTypeOf(prop.second)) {
                case VariantType::ContentId: sawContentId = true; break;
                case VariantType::Font: sawFont = true; break;
                case VariantType::SecurityCapabilities: sawSecurity = true; break;
                case VariantType::OptionalCFrame: sawOptionalCFrame = true; break;
                default: break;
            }
        }
    }
    CHECK(sawContentId);
    CHECK(sawFont);
    CHECK(sawSecurity);
    CHECK(sawOptionalCFrame);
}
