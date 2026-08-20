#include "xml/decode.hpp"
#include "xml/base64.hpp"
#include <rbxl/bitutil.hpp>

#include <pugixml.hpp>

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Decodes the XML place/model format (Appendix A.3 in PLAN.md) into a Dom.
// Two passes over <Item> elements, as the appendix's referent rule requires:
// pass 1 creates every instance and records referent-string -> InstanceId so
// a <Ref> can resolve even when it points to an instance declared later in
// the file; pass 2 then decodes every <Properties> element, including Refs,
// against the now-complete referent table.
namespace rbxl {
namespace xml {

namespace {

uint64_t rotr64(uint64_t v) { return (v >> 1) | (v << 63); }

// --- Leaf text parsing -------------------------------------------------
// pugixml's child_value()/attribute value accessors never return null, so
// none of these need to guard against it.

bool parseBoolText(const char* text) {
    return std::strcmp(text, "true") == 0;
}

int32_t parseInt32Text(const char* text) {
    return static_cast<int32_t>(std::strtoll(text, nullptr, 10));
}

int64_t parseInt64Text(const char* text) {
    return static_cast<int64_t>(std::strtoll(text, nullptr, 10));
}

uint32_t parseUInt32Text(const char* text) {
    return static_cast<uint32_t>(std::strtoull(text, nullptr, 10));
}

uint64_t parseUInt64Text(const char* text) {
    return static_cast<uint64_t>(std::strtoull(text, nullptr, 10));
}

// std::strtof/strtod do not accept Roblox's spellings for the non-finite
// values, so those are matched explicitly before falling back to the C
// library for everything else.
float parseFloatText(const char* text) {
    if (std::strcmp(text, "INF") == 0) return std::numeric_limits<float>::infinity();
    if (std::strcmp(text, "-INF") == 0) return -std::numeric_limits<float>::infinity();
    if (std::strcmp(text, "NAN") == 0) return std::numeric_limits<float>::quiet_NaN();
    return std::strtof(text, nullptr);
}

double parseDoubleText(const char* text) {
    if (std::strcmp(text, "INF") == 0) return std::numeric_limits<double>::infinity();
    if (std::strcmp(text, "-INF") == 0) return -std::numeric_limits<double>::infinity();
    if (std::strcmp(text, "NAN") == 0) return std::numeric_limits<double>::quiet_NaN();
    return std::strtod(text, nullptr);
}

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// Tokenizes a whitespace-separated run of floats (NumberRange/Sequence/
// ColorSequence text content) without allocating per token.
std::vector<float> parseFloatList(const char* text) {
    std::vector<float> out;
    const char* p = text;
    while (*p) {
        while (*p && isAsciiSpace(*p)) ++p;
        if (!*p) break;
        if (std::strncmp(p, "-INF", 4) == 0) {
            out.push_back(-std::numeric_limits<float>::infinity());
            p += 4;
            continue;
        }
        if (std::strncmp(p, "INF", 3) == 0) {
            out.push_back(std::numeric_limits<float>::infinity());
            p += 3;
            continue;
        }
        if (std::strncmp(p, "NAN", 3) == 0) {
            out.push_back(std::numeric_limits<float>::quiet_NaN());
            p += 3;
            continue;
        }
        char* end = nullptr;
        float v = std::strtof(p, &end);
        if (end == p) break;   // malformed token; stop rather than loop forever
        out.push_back(v);
        p = end;
    }
    return out;
}

// 16 raw bytes from a hex string (UniqueId), ignoring embedded whitespace.
// False on anything but exactly 32 significant hex digits.
bool parseHex16(const char* text, uint8_t out[16]) {
    size_t n = 0;
    int hi = -1;
    for (const char* p = text; *p; ++p) {
        if (isAsciiSpace(*p)) continue;
        int v;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else return false;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= 16) return false;
            out[n++] = static_cast<uint8_t>((hi << 4) | v);
            hi = -1;
        }
    }
    return hi < 0 && n == 16;
}

InstanceId resolveRef(const char* text,
                       const std::unordered_map<std::string, InstanceId>& referents) {
    if (std::strcmp(text, "null") == 0) return kNoInstance;
    auto it = referents.find(text);
    return it == referents.end() ? kNoInstance : it->second;
}

// --- Composite element parsing -------------------------------------------

Vector2 parseVector2(const pugi::xml_node& n) {
    return Vector2{parseFloatText(n.child_value("X")), parseFloatText(n.child_value("Y"))};
}

Vector3 parseVector3(const pugi::xml_node& n) {
    return Vector3{parseFloatText(n.child_value("X")), parseFloatText(n.child_value("Y")),
                    parseFloatText(n.child_value("Z"))};
}

Vector3int16 parseVector3int16(const pugi::xml_node& n) {
    Vector3int16 v;
    v.x = static_cast<int16_t>(parseInt32Text(n.child_value("X")));
    v.y = static_cast<int16_t>(parseInt32Text(n.child_value("Y")));
    v.z = static_cast<int16_t>(parseInt32Text(n.child_value("Z")));
    return v;
}

Color3 parseColor3(const pugi::xml_node& n) {
    return Color3{parseFloatText(n.child_value("R")), parseFloatText(n.child_value("G")),
                   parseFloatText(n.child_value("B"))};
}

// ARGB packed into one integer; alpha (the top byte) is unused by this format.
Color3uint8 parseColor3uint8(const pugi::xml_node& n) {
    const uint32_t v = parseUInt32Text(n.child_value());
    Color3uint8 c;
    c.r = static_cast<uint8_t>((v >> 16) & 0xFF);
    c.g = static_cast<uint8_t>((v >> 8) & 0xFF);
    c.b = static_cast<uint8_t>(v & 0xFF);
    return c;
}

CFrame parseCFrame(const pugi::xml_node& n) {
    CFrame cf;
    cf.position = parseVector3(n);
    static const char* kNames[9] = {"R00", "R01", "R02", "R10", "R11",
                                     "R12", "R20", "R21", "R22"};
    for (int i = 0; i < 9; ++i) cf.rotation[i] = parseFloatText(n.child_value(kNames[i]));
    return cf;
}

OptionalCFrame parseOptionalCFrame(const pugi::xml_node& n) {
    OptionalCFrame ocf;
    if (pugi::xml_node cfNode = n.child("CFrame")) {
        ocf.hasValue = true;
        ocf.value = parseCFrame(cfNode);
    }
    return ocf;
}

UDim parseUDim(const pugi::xml_node& n) {
    return UDim{parseFloatText(n.child_value("S")), parseInt32Text(n.child_value("O"))};
}

UDim2 parseUDim2(const pugi::xml_node& n) {
    UDim2 u;
    u.x.scale = parseFloatText(n.child_value("XS"));
    u.x.offset = parseInt32Text(n.child_value("XO"));
    u.y.scale = parseFloatText(n.child_value("YS"));
    u.y.offset = parseInt32Text(n.child_value("YO"));
    return u;
}

Rect parseRect(const pugi::xml_node& n) {
    Rect r;
    r.min = parseVector2(n.child("min"));
    r.max = parseVector2(n.child("max"));
    return r;
}

Ray parseRay(const pugi::xml_node& n) {
    Ray r;
    r.origin = parseVector3(n.child("origin"));
    r.direction = parseVector3(n.child("direction"));
    return r;
}

NumberRange parseNumberRange(const pugi::xml_node& n) {
    auto vals = parseFloatList(n.child_value());
    NumberRange r;
    if (vals.size() >= 1) r.min = vals[0];
    if (vals.size() >= 2) r.max = vals[1];
    return r;
}

NumberSequence parseNumberSequence(const pugi::xml_node& n) {
    auto vals = parseFloatList(n.child_value());
    NumberSequence seq;
    for (size_t i = 0; i + 3 <= vals.size(); i += 3) {
        seq.keypoints.push_back(NumberSequenceKeypoint{vals[i], vals[i + 1], vals[i + 2]});
    }
    return seq;
}

ColorSequence parseColorSequence(const pugi::xml_node& n) {
    auto vals = parseFloatList(n.child_value());
    ColorSequence seq;
    for (size_t i = 0; i + 5 <= vals.size(); i += 5) {
        ColorSequenceKeypoint kp;
        kp.time = vals[i];
        kp.color = Color3{vals[i + 1], vals[i + 2], vals[i + 3]};
        kp.envelope = vals[i + 4];
        seq.keypoints.push_back(kp);
    }
    return seq;
}

PhysicalProperties parsePhysicalProperties(const pugi::xml_node& n) {
    PhysicalProperties pp;
    pp.custom = parseBoolText(n.child_value("CustomPhysics"));
    if (pp.custom) {
        pp.density = parseFloatText(n.child_value("Density"));
        pp.friction = parseFloatText(n.child_value("Friction"));
        pp.elasticity = parseFloatText(n.child_value("Elasticity"));
        pp.frictionWeight = parseFloatText(n.child_value("FrictionWeight"));
        pp.elasticityWeight = parseFloatText(n.child_value("ElasticityWeight"));
        if (pugi::xml_node aa = n.child("AcousticAbsorption")) {
            pp.hasAcousticAbsorption = true;
            pp.acousticAbsorption = parseFloatText(aa.child_value());
        }
    }
    return pp;
}

// <Family>/<CachedFaceId> are themselves Content-shaped (a <url> or <uri>
// wrapper); a bare-text fallback tolerates a non-conforming writer.
std::string parseContentUriLike(const pugi::xml_node& n) {
    if (pugi::xml_node url = n.child("url")) return url.child_value();
    if (pugi::xml_node uri = n.child("uri")) return uri.child_value();
    return n.child_value();
}

Font parseFont(const pugi::xml_node& n) {
    Font f;
    if (pugi::xml_node fam = n.child("Family")) f.family = parseContentUriLike(fam);
    f.weight = static_cast<uint16_t>(parseUInt32Text(n.child_value("Weight")));
    f.style = std::strcmp(n.child_value("Style"), "Italic") == 0 ? 1 : 0;
    if (pugi::xml_node face = n.child("CachedFaceId")) f.cachedFaceId = parseContentUriLike(face);
    return f;
}

// XML layout is bytes 0-7 Random (u64), 8-11 Time (u32), 12-15 Index (u32):
// the reverse field order from binary, per Appendix A.3.
UniqueId parseUniqueId(const pugi::xml_node& n) {
    UniqueId u;
    uint8_t bytes[16];
    if (parseHex16(n.child_value(), bytes)) {
        // VERIFY: the spec states Random is left-circular rotated by one bit
        // in XML relative to binary. This treats the raw XML bytes as the
        // canonical UniqueId::random value rotated left by one bit, so
        // decoding rotates right to undo it -- the same direction
        // binary/valuecodec_struct.cpp's decodeUniqueId uses for the binary
        // format's own (separately specified) rotation. No file in temp/
        // contains a UniqueId, so this has not been checked against a real
        // Roblox-Studio-written file; confirm by round-tripping one through
        // Studio in both formats and comparing.
        u.random = static_cast<int64_t>(rotr64(bit::readU64BE(bytes)));
        u.time = bit::readU32BE(bytes + 8);
        u.index = bit::readU32BE(bytes + 12);
    }
    return u;
}

// The modern Content type and the legacy ContentId type are the same
// Variant-shaped decision (Content or ContentId), taken purely from which
// child element is present, never from the enclosing element's name --
// third-party writers use the `Content` element name for the legacy shape.
Variant decodeContentLike(const pugi::xml_node& n,
                           const std::unordered_map<std::string, InstanceId>& referents) {
    if (pugi::xml_node url = n.child("url")) {
        return ContentId{url.child_value()};
    }
    if (pugi::xml_node uri = n.child("uri")) {
        Content c;
        c.sourceType = Content::SourceType::Uri;
        c.uri = uri.child_value();
        return c;
    }
    if (pugi::xml_node ref = n.child("Ref")) {
        Content c;
        c.sourceType = Content::SourceType::Object;
        c.object = resolveRef(ref.child_value(), referents);
        return c;
    }
    // `null`, legacy `binary`/`hash`, or no recognised child at all: an
    // empty ContentId when the element name is ContentId (binary/hash are
    // discarded per the appendix), otherwise an empty Content.
    if (std::strcmp(n.name(), "ContentId") == 0) return ContentId{};
    return Content{};
}

// --- Property dispatch -----------------------------------------------------

const std::unordered_map<std::string_view, VariantType>& elementTypeTable() {
    static const std::unordered_map<std::string_view, VariantType> kTable = {
        {"string", VariantType::String},
        {"ProtectedString", VariantType::ProtectedString},
        {"BinaryString", VariantType::BinaryString},
        {"bool", VariantType::Bool},
        {"int", VariantType::Int32},
        {"int64", VariantType::Int64},
        {"float", VariantType::Float32},
        {"double", VariantType::Float64},
        {"token", VariantType::EnumValue},
        {"Ref", VariantType::Ref},
        {"Vector2", VariantType::Vector2},
        {"Vector3", VariantType::Vector3},
        {"Vector3int16", VariantType::Vector3int16},
        {"Color3", VariantType::Color3},
        {"Color3uint8", VariantType::Color3uint8},
        {"CoordinateFrame", VariantType::CFrame},
        {"OptionalCoordinateFrame", VariantType::OptionalCFrame},
        {"UDim", VariantType::UDim},
        {"UDim2", VariantType::UDim2},
        {"Rect2D", VariantType::Rect},
        {"Ray", VariantType::Ray},
        {"Faces", VariantType::Faces},
        {"Axes", VariantType::Axes},
        {"NumberRange", VariantType::NumberRange},
        {"NumberSequence", VariantType::NumberSequence},
        {"ColorSequence", VariantType::ColorSequence},
        {"PhysicalProperties", VariantType::PhysicalProperties},
        {"Font", VariantType::Font},
        {"UniqueId", VariantType::UniqueId},
        {"SecurityCapabilities", VariantType::SecurityCapabilities},
        {"SharedString", VariantType::SharedString},
        {"NetAssetRef", VariantType::NetAssetRef},
        // Not part of the documented format (real Roblox writers use `int`
        // for BrickColor properties, per the appendix), but the corpus place
        // has at least one property written this way by a third-party tool;
        // decoding it costs nothing and loses no data that would otherwise
        // survive.
        {"BrickColor", VariantType::BrickColor},
    };
    return kTable;
}

Variant decodeByType(VariantType type, const pugi::xml_node& n,
                      const std::unordered_map<std::string, InstanceId>& referents,
                      const std::unordered_map<std::string, std::string>& sharedStrings) {
    switch (type) {
        case VariantType::String: return std::string(n.child_value());
        case VariantType::ProtectedString: return ProtectedString{n.child_value()};
        case VariantType::BinaryString: {
            auto decoded = base64Decode(n.child_value());
            return BinaryString{decoded ? std::move(decoded.value()) : std::vector<uint8_t>{}};
        }
        case VariantType::Bool: return parseBoolText(n.child_value());
        case VariantType::Int32: return parseInt32Text(n.child_value());
        case VariantType::Int64: return parseInt64Text(n.child_value());
        case VariantType::Float32: return parseFloatText(n.child_value());
        case VariantType::Float64: return parseDoubleText(n.child_value());
        case VariantType::EnumValue: return EnumValue{parseUInt32Text(n.child_value())};
        case VariantType::Ref: return Ref{resolveRef(n.child_value(), referents)};
        case VariantType::Vector2: return parseVector2(n);
        case VariantType::Vector3: return parseVector3(n);
        case VariantType::Vector3int16: return parseVector3int16(n);
        case VariantType::Color3: return parseColor3(n);
        case VariantType::Color3uint8: return parseColor3uint8(n);
        case VariantType::CFrame: return parseCFrame(n);
        case VariantType::OptionalCFrame: return parseOptionalCFrame(n);
        case VariantType::UDim: return parseUDim(n);
        case VariantType::UDim2: return parseUDim2(n);
        case VariantType::Rect: return parseRect(n);
        case VariantType::Ray: return parseRay(n);
        case VariantType::Faces: return Faces{static_cast<uint8_t>(parseUInt32Text(n.child_value("faces")))};
        case VariantType::Axes: return Axes{static_cast<uint8_t>(parseUInt32Text(n.child_value("axes")))};
        case VariantType::NumberRange: return parseNumberRange(n);
        case VariantType::NumberSequence: return parseNumberSequence(n);
        case VariantType::ColorSequence: return parseColorSequence(n);
        case VariantType::PhysicalProperties: return parsePhysicalProperties(n);
        case VariantType::Font: return parseFont(n);
        case VariantType::UniqueId: return parseUniqueId(n);
        case VariantType::SecurityCapabilities: return SecurityCapabilities{parseUInt64Text(n.child_value())};
        case VariantType::BrickColor: return BrickColor{parseUInt32Text(n.child_value())};
        case VariantType::SharedString:
        case VariantType::NetAssetRef: {
            auto keyBytes = base64Decode(n.child_value());
            std::string key = keyBytes ? std::string(keyBytes.value().begin(), keyBytes.value().end())
                                        : std::string();
            std::string value;
            auto it = sharedStrings.find(key);
            if (it != sharedStrings.end()) value = it->second;
            if (type == VariantType::SharedString) return SharedString{std::move(key), std::move(value)};
            return NetAssetRef{std::move(key), std::move(value)};
        }
        default: return std::monostate{};
    }
}

// Returns false (leaving `out` untouched) for an element name this decoder
// does not recognise, so the caller can simply skip the property rather than
// storing a placeholder Nil value.
bool decodePropertyValue(std::string_view elementName, const pugi::xml_node& n,
                          const std::unordered_map<std::string, InstanceId>& referents,
                          const std::unordered_map<std::string, std::string>& sharedStrings,
                          Variant& out) {
    if (elementName == "Content" || elementName == "ContentId") {
        out = decodeContentLike(n, referents);
        return true;
    }
    const auto& table = elementTypeTable();
    auto it = table.find(elementName);
    if (it == table.end()) return false;
    out = decodeByType(it->second, n, referents, sharedStrings);
    return true;
}

// --- Structure: instances, hierarchy, referents -----------------------

// Recursively creates one instance per <Item>, wires it into `dom`'s
// hierarchy, and records its referent string. Properties are deferred to a
// second pass (see decode()) so a <Ref> can point forward.
void collectItems(const pugi::xml_node& itemNode, InstanceId parent, Dom& dom,
                   std::unordered_map<std::string, InstanceId>& referents,
                   std::vector<std::pair<InstanceId, pugi::xml_node>>& items) {
    InstanceId id = dom.create(itemNode.attribute("class").value());
    if (parent != kNoInstance) dom.setParent(id, parent);

    const char* referent = itemNode.attribute("referent").value();
    if (*referent) referents.emplace(referent, id);

    items.emplace_back(id, itemNode);

    for (pugi::xml_node child = itemNode.child("Item"); child; child = child.next_sibling("Item")) {
        collectItems(child, id, dom, referents, items);
    }
}

}  // namespace

Result<Dom> decode(const char* data, size_t size) {
    // pugixml's load_buffer_inplace_own parses without an extra internal
    // copy, but it must own its buffer to do so; this malloc'd copy (freed
    // by pugixml, which uses malloc/free as its default allocator) is the
    // only copy this function makes of a 153 MB place.
    void* buffer = nullptr;
    if (size > 0) {
        buffer = std::malloc(size);
        if (!buffer) return makeError(ErrorCode::Io, "out of memory reading XML buffer");
        std::memcpy(buffer, data, size);
    }

    pugi::xml_document doc;
    const unsigned int options = pugi::parse_default | pugi::parse_ws_pcdata_single;
    pugi::xml_parse_result parseResult = doc.load_buffer_inplace_own(buffer, size, options);
    if (!parseResult) {
        return makeError(ErrorCode::XmlParse, parseResult.description(),
                          static_cast<size_t>(parseResult.offset));
    }

    // A leading comment or processing instruction is legal XML and does not
    // stop child("roblox") from finding the real root: pugixml compares
    // node names, and neither node type has one, so they are transparently
    // skipped. parse_default does not even add them to the tree.
    pugi::xml_node root = doc.child("roblox");
    if (!root) {
        return makeError(ErrorCode::Malformed, "missing <roblox> root element");
    }

    Dom dom;
    std::unordered_map<std::string, InstanceId> referents;
    std::vector<std::pair<InstanceId, pugi::xml_node>> items;
    std::unordered_map<std::string, std::string> sharedStrings;

    for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {
        const std::string_view name = child.name();
        if (name == "Item") {
            collectItems(child, kNoInstance, dom, referents, items);
        } else if (name == "Meta") {
            dom.metadata().emplace_back(child.attribute("name").value(), child.child_value());
        } else if (name == "SharedStrings") {
            for (pugi::xml_node ss = child.child("SharedString"); ss;
                 ss = ss.next_sibling("SharedString")) {
                auto keyBytes = base64Decode(ss.attribute("md5").value());
                if (!keyBytes) continue;   // malformed table entry; skip it
                std::string key(keyBytes.value().begin(), keyBytes.value().end());
                std::string value;
                auto valueBytes = base64Decode(ss.child_value());
                if (valueBytes) value.assign(valueBytes.value().begin(), valueBytes.value().end());
                sharedStrings.emplace(std::move(key), std::move(value));
            }
        }
        // `External` is legacy and ignorable; anything else is skipped.
    }

    for (auto& [id, itemNode] : items) {
        pugi::xml_node props = itemNode.child("Properties");
        if (!props) continue;
        for (pugi::xml_node prop = props.first_child(); prop; prop = prop.next_sibling()) {
            if (prop.type() != pugi::node_element) continue;
            const char* propName = prop.attribute("name").value();
            if (!*propName) continue;
            Variant value;
            if (decodePropertyValue(prop.name(), prop, referents, sharedStrings, value)) {
                dom.setProperty(id, propName, std::move(value));
            }
        }
    }

    return dom;
}

}  // namespace xml
}  // namespace rbxl
