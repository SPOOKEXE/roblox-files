#include "xml/encode.hpp"
#include "xml/base64.hpp"
#include <rbxl/bitutil.hpp>

#include <pugixml.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// The inverse of decode.cpp: walks a `Dom` and produces the XML place/model
// format (Appendix A.3 in PLAN.md). Element names below are the deliberate
// mirror of decode.cpp's elementTypeTable: every entry in that table has a
// matching case here that writes the same element name back out, so a value
// this codec decodes from one file re-encodes under a name the same decoder
// will recognise.
namespace rbxl {
namespace xml {

namespace {

uint64_t rotl64(uint64_t v) { return (v << 1) | (v >> 63); }

// --- Leaf text formatting ---------------------------------------------

// %.9g / %.17g are the shortest fixed precisions that always round-trip an
// IEEE-754 float/double exactly; INF/-INF/NAN match the spellings
// decode.cpp's parseFloatText/parseDoubleText accept (the C library does
// not produce or accept those spellings itself).
std::string formatFloatText(float v) {
    if (std::isnan(v)) return "NAN";
    if (std::isinf(v)) return v < 0 ? "-INF" : "INF";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return buf;
}

std::string formatDoubleText(double v) {
    if (std::isnan(v)) return "NAN";
    if (std::isinf(v)) return v < 0 ? "-INF" : "INF";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

// "RBXn" for a real instance, "null" for kNoInstance -- the inverse of
// decode.cpp's resolveRef. InstanceId is dense and 0-based, so this is
// unique by construction across one Dom.
std::string referentText(InstanceId id) { return "RBX" + std::to_string(id); }
std::string refValueText(InstanceId target) {
    return target == kNoInstance ? "null" : referentText(target);
}

// --- Composite element writers, one per struct in types.hpp -------------
// Each takes the already-created property element and fills in its children;
// callers own creating that element and setting its `name` attribute.

void setFloatChild(pugi::xml_node parent, const char* name, float v) {
    parent.append_child(name).text().set(formatFloatText(v).c_str());
}
void setIntChild(pugi::xml_node parent, const char* name, int32_t v) {
    parent.append_child(name).text().set(std::to_string(v).c_str());
}
void setUIntChild(pugi::xml_node parent, const char* name, uint32_t v) {
    parent.append_child(name).text().set(std::to_string(v).c_str());
}

void writeVector2(pugi::xml_node n, const Vector2& v) {
    setFloatChild(n, "X", v.x);
    setFloatChild(n, "Y", v.y);
}
void writeVector3(pugi::xml_node n, const Vector3& v) {
    setFloatChild(n, "X", v.x);
    setFloatChild(n, "Y", v.y);
    setFloatChild(n, "Z", v.z);
}
void writeVector3int16(pugi::xml_node n, const Vector3int16& v) {
    setIntChild(n, "X", v.x);
    setIntChild(n, "Y", v.y);
    setIntChild(n, "Z", v.z);
}

void writeColor3(pugi::xml_node n, const Color3& c) {
    setFloatChild(n, "R", c.r);
    setFloatChild(n, "G", c.g);
    setFloatChild(n, "B", c.b);
}

// ARGB packed into one integer, alpha forced to FF: the inverse of
// decode.cpp's parseColor3uint8, which discards the top byte on the way in.
std::string color3uint8Text(const Color3uint8& c) {
    const uint32_t v = (0xFFu << 24) | (static_cast<uint32_t>(c.r) << 16) |
                        (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.b);
    return std::to_string(v);
}

void writeCFrame(pugi::xml_node n, const CFrame& cf) {
    writeVector3(n, cf.position);
    static const char* kNames[9] = {"R00", "R01", "R02", "R10", "R11",
                                     "R12", "R20", "R21", "R22"};
    for (int i = 0; i < 9; ++i) setFloatChild(n, kNames[i], cf.rotation[i]);
}

void writeOptionalCFrame(pugi::xml_node n, const OptionalCFrame& ocf) {
    if (ocf.hasValue) writeCFrame(n.append_child("CFrame"), ocf.value);
}

void writeUDim(pugi::xml_node n, const UDim& u) {
    setFloatChild(n, "S", u.scale);
    setIntChild(n, "O", u.offset);
}
void writeUDim2(pugi::xml_node n, const UDim2& u) {
    setFloatChild(n, "XS", u.x.scale);
    setIntChild(n, "XO", u.x.offset);
    setFloatChild(n, "YS", u.y.scale);
    setIntChild(n, "YO", u.y.offset);
}

void writeRect(pugi::xml_node n, const Rect& r) {
    writeVector2(n.append_child("min"), r.min);
    writeVector2(n.append_child("max"), r.max);
}
void writeRay(pugi::xml_node n, const Ray& r) {
    writeVector3(n.append_child("origin"), r.origin);
    writeVector3(n.append_child("direction"), r.direction);
}

void writeFaces(pugi::xml_node n, const Faces& f) { setUIntChild(n, "faces", f.bits); }
void writeAxes(pugi::xml_node n, const Axes& a) { setUIntChild(n, "axes", a.bits); }

std::string numberRangeText(const NumberRange& r) {
    return formatFloatText(r.min) + " " + formatFloatText(r.max);
}
std::string numberSequenceText(const NumberSequence& seq) {
    std::string out;
    for (const auto& kp : seq.keypoints) {
        if (!out.empty()) out += ' ';
        out += formatFloatText(kp.time) + " " + formatFloatText(kp.value) + " " +
               formatFloatText(kp.envelope);
    }
    return out;
}
std::string colorSequenceText(const ColorSequence& seq) {
    std::string out;
    for (const auto& kp : seq.keypoints) {
        if (!out.empty()) out += ' ';
        out += formatFloatText(kp.time) + " " + formatFloatText(kp.color.r) + " " +
               formatFloatText(kp.color.g) + " " + formatFloatText(kp.color.b) + " " +
               formatFloatText(kp.envelope);
    }
    return out;
}

void writePhysicalProperties(pugi::xml_node n, const PhysicalProperties& pp) {
    n.append_child("CustomPhysics").text().set(pp.custom ? "true" : "false");
    if (!pp.custom) return;
    setFloatChild(n, "Density", pp.density);
    setFloatChild(n, "Friction", pp.friction);
    setFloatChild(n, "Elasticity", pp.elasticity);
    setFloatChild(n, "FrictionWeight", pp.frictionWeight);
    setFloatChild(n, "ElasticityWeight", pp.elasticityWeight);
    if (pp.hasAcousticAbsorption) setFloatChild(n, "AcousticAbsorption", pp.acousticAbsorption);
}

// <Family>/<CachedFaceId> are themselves Content-shaped; this codec always
// writes the legacy <url> wrapper, matching what real Roblox-written files
// use for both (see the corpus grep in the task notes).
void writeContentUriLike(pugi::xml_node parent, const char* name, const std::string& value) {
    parent.append_child(name).append_child("url").text().set(value.c_str());
}

// Weight stays numeric; Style is written as its name, not its numeric code.
// CachedFaceId is omitted entirely when empty. This asymmetry is required by
// real Roblox readers, not incidental.
void writeFont(pugi::xml_node n, const Font& f) {
    writeContentUriLike(n, "Family", f.family);
    setUIntChild(n, "Weight", f.weight);
    n.append_child("Style").text().set(f.style == 1 ? "Italic" : "Normal");
    if (!f.cachedFaceId.empty()) writeContentUriLike(n, "CachedFaceId", f.cachedFaceId);
}

// XML layout is bytes 0-7 Random (u64) rotated left one bit, 8-11 Time (u32),
// 12-15 Index (u32): the inverse of decode.cpp's parseUniqueId, which rotates
// right to undo this on the way in.
// VERIFY: the one-bit left rotation on Random is taken from the same
// spec-reading as decode.cpp's parseUniqueId (see the longer note there), not
// from an observed file -- no file in temp/ contains a UniqueId, so this
// rotation direction has not been checked against a real Roblox-Studio round
// trip in either format. encode-then-decode is self-consistent by
// construction (this rotates left, parseUniqueId rotates right), which is
// all today's tests can confirm.
std::string uniqueIdHex(const UniqueId& u) {
    uint8_t bytes[16];
    bit::writeU64BE(bytes, rotl64(static_cast<uint64_t>(u.random)));
    bit::writeU32BE(bytes + 8, u.time);
    bit::writeU32BE(bytes + 12, u.index);
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

// CDATA cannot represent its own terminator; a value containing "]]>" falls
// back to ordinary escaped text rather than producing malformed XML. The
// supplied test exercises exactly this case.
void writeProtectedString(pugi::xml_node n, const std::string& value) {
    if (value.find("]]>") == std::string::npos) {
        n.append_child(pugi::node_cdata).set_value(value.c_str());
    } else {
        n.text().set(value.c_str());
    }
}

void writeContentId(pugi::xml_node n, const ContentId& c) {
    n.append_child("url").text().set(c.url.c_str());
}

// The inverse of decode.cpp's decodeContentLike: which child is written is
// chosen from the source type actually held, not from any element name.
void writeContent(pugi::xml_node n, const Content& c) {
    switch (c.sourceType) {
        case Content::SourceType::Uri:
            n.append_child("uri").text().set(c.uri.c_str());
            return;
        case Content::SourceType::Object:
            n.append_child("Ref").text().set(refValueText(c.object).c_str());
            return;
        case Content::SourceType::None:
            n.append_child("null");
            return;
    }
}

}  // namespace

// --- Property dispatch -----------------------------------------------------
// Declared in encode.hpp, not file-local, so the mapping test below can call
// it directly instead of duplicating it (see the header for the full note).

// The element name each VariantType is written under. Mirrors
// decode.cpp's elementTypeTable in the opposite direction, plus Content and
// ContentId (which that table routes around specially). Returns false for
// Nil and for the handful of VariantType alternatives with no entry in
// Appendix A.3 (Vector2int16, Region3, Region3int16, EnumItem, Bytecode);
// none of those are ever produced by this project's XML or binary decoders
// today, but a hand-built Dom can still hold one, so the caller reports it
// through diagnostics rather than writing a value the format has no name for.
bool elementNameFor(VariantType type, const char*& outName) {
    switch (type) {
        case VariantType::String: outName = "string"; return true;
        case VariantType::ProtectedString: outName = "ProtectedString"; return true;
        case VariantType::BinaryString: outName = "BinaryString"; return true;
        case VariantType::Bool: outName = "bool"; return true;
        case VariantType::Int32: outName = "int"; return true;
        case VariantType::Int64: outName = "int64"; return true;
        case VariantType::Float32: outName = "float"; return true;
        case VariantType::Float64: outName = "double"; return true;
        case VariantType::EnumValue: outName = "token"; return true;
        case VariantType::Ref: outName = "Ref"; return true;
        case VariantType::Vector2: outName = "Vector2"; return true;
        case VariantType::Vector3: outName = "Vector3"; return true;
        case VariantType::Vector3int16: outName = "Vector3int16"; return true;
        case VariantType::Color3: outName = "Color3"; return true;
        case VariantType::Color3uint8: outName = "Color3uint8"; return true;
        case VariantType::CFrame: outName = "CoordinateFrame"; return true;
        case VariantType::OptionalCFrame: outName = "OptionalCoordinateFrame"; return true;
        case VariantType::UDim: outName = "UDim"; return true;
        case VariantType::UDim2: outName = "UDim2"; return true;
        case VariantType::Rect: outName = "Rect2D"; return true;
        case VariantType::Ray: outName = "Ray"; return true;
        case VariantType::Faces: outName = "Faces"; return true;
        case VariantType::Axes: outName = "Axes"; return true;
        case VariantType::NumberRange: outName = "NumberRange"; return true;
        case VariantType::NumberSequence: outName = "NumberSequence"; return true;
        case VariantType::ColorSequence: outName = "ColorSequence"; return true;
        case VariantType::PhysicalProperties: outName = "PhysicalProperties"; return true;
        case VariantType::Font: outName = "Font"; return true;
        case VariantType::UniqueId: outName = "UniqueId"; return true;
        case VariantType::SecurityCapabilities: outName = "SecurityCapabilities"; return true;
        // Not part of the documented format (real Roblox writers use `int`
        // for BrickColor properties; see Appendix A.3), but it is the exact
        // inverse of decode.cpp's own courtesy entry for the one corpus
        // property written this way, so round-tripping this codec's own
        // output stays lossless.
        case VariantType::BrickColor: outName = "BrickColor"; return true;
        case VariantType::Content: outName = "Content"; return true;
        case VariantType::ContentId: outName = "ContentId"; return true;
        case VariantType::SharedString: outName = "SharedString"; return true;
        case VariantType::NetAssetRef: outName = "NetAssetRef"; return true;
        default: return false;
    }
}

namespace {

void writeProperty(pugi::xml_node propsNode, InstanceId ownerId, const std::string& name,
                    const Variant& value, std::map<std::string, std::string>& sharedStrings,
                    EncodeDiagnostics& diags) {
    const VariantType type = variantTypeOf(value);
    const char* elementName = nullptr;
    if (!elementNameFor(type, elementName)) {
        if (type != VariantType::Nil) {
            diags.warnings.push_back("dropped property \"" + name + "\" on instance RBX" +
                                      std::to_string(ownerId) + ": " + variantTypeName(type) +
                                      " has no XML representation");
        }
        return;
    }

    pugi::xml_node el = propsNode.append_child(elementName);
    el.append_attribute("name").set_value(name.c_str());

    switch (type) {
        case VariantType::String:
            el.text().set(std::get<std::string>(value).c_str());
            break;
        case VariantType::ProtectedString:
            writeProtectedString(el, std::get<ProtectedString>(value).value);
            break;
        case VariantType::BinaryString: {
            const auto& data = std::get<BinaryString>(value).data;
            el.text().set(base64Encode(data.data(), data.size()).c_str());
            break;
        }
        case VariantType::Bool:
            el.text().set(std::get<bool>(value) ? "true" : "false");
            break;
        case VariantType::Int32:
            el.text().set(std::to_string(std::get<int32_t>(value)).c_str());
            break;
        case VariantType::Int64:
            el.text().set(std::to_string(std::get<int64_t>(value)).c_str());
            break;
        case VariantType::Float32:
            el.text().set(formatFloatText(std::get<float>(value)).c_str());
            break;
        case VariantType::Float64:
            el.text().set(formatDoubleText(std::get<double>(value)).c_str());
            break;
        case VariantType::EnumValue:
            el.text().set(std::to_string(std::get<EnumValue>(value).value).c_str());
            break;
        case VariantType::Ref:
            el.text().set(refValueText(std::get<Ref>(value).target).c_str());
            break;
        case VariantType::Vector2:
            writeVector2(el, std::get<Vector2>(value));
            break;
        case VariantType::Vector3:
            writeVector3(el, std::get<Vector3>(value));
            break;
        case VariantType::Vector3int16:
            writeVector3int16(el, std::get<Vector3int16>(value));
            break;
        case VariantType::Color3:
            writeColor3(el, std::get<Color3>(value));
            break;
        case VariantType::Color3uint8:
            el.text().set(color3uint8Text(std::get<Color3uint8>(value)).c_str());
            break;
        case VariantType::CFrame:
            writeCFrame(el, std::get<CFrame>(value));
            break;
        case VariantType::OptionalCFrame:
            writeOptionalCFrame(el, std::get<OptionalCFrame>(value));
            break;
        case VariantType::UDim:
            writeUDim(el, std::get<UDim>(value));
            break;
        case VariantType::UDim2:
            writeUDim2(el, std::get<UDim2>(value));
            break;
        case VariantType::Rect:
            writeRect(el, std::get<Rect>(value));
            break;
        case VariantType::Ray:
            writeRay(el, std::get<Ray>(value));
            break;
        case VariantType::Faces:
            writeFaces(el, std::get<Faces>(value));
            break;
        case VariantType::Axes:
            writeAxes(el, std::get<Axes>(value));
            break;
        case VariantType::NumberRange:
            el.text().set(numberRangeText(std::get<NumberRange>(value)).c_str());
            break;
        case VariantType::NumberSequence:
            el.text().set(numberSequenceText(std::get<NumberSequence>(value)).c_str());
            break;
        case VariantType::ColorSequence:
            el.text().set(colorSequenceText(std::get<ColorSequence>(value)).c_str());
            break;
        case VariantType::PhysicalProperties:
            writePhysicalProperties(el, std::get<PhysicalProperties>(value));
            break;
        case VariantType::Font:
            writeFont(el, std::get<Font>(value));
            break;
        case VariantType::UniqueId:
            el.text().set(uniqueIdHex(std::get<UniqueId>(value)).c_str());
            break;
        case VariantType::SecurityCapabilities:
            el.text().set(std::to_string(std::get<SecurityCapabilities>(value).value).c_str());
            break;
        case VariantType::BrickColor:
            el.text().set(std::to_string(std::get<BrickColor>(value).number).c_str());
            break;
        case VariantType::Content:
            writeContent(el, std::get<Content>(value));
            break;
        case VariantType::ContentId:
            writeContentId(el, std::get<ContentId>(value));
            break;
        case VariantType::SharedString: {
            const SharedString& s = std::get<SharedString>(value);
            el.text().set(
                base64Encode(reinterpret_cast<const uint8_t*>(s.key.data()), s.key.size())
                    .c_str());
            sharedStrings.emplace(s.key, s.value);
            break;
        }
        case VariantType::NetAssetRef: {
            const NetAssetRef& s = std::get<NetAssetRef>(value);
            el.text().set(
                base64Encode(reinterpret_cast<const uint8_t*>(s.key.data()), s.key.size())
                    .c_str());
            sharedStrings.emplace(s.key, s.value);
            break;
        }
        default:
            break;   // unreachable: elementNameFor already rejected every other type
    }
}

// Recursively writes one <Item> per instance, in the Dom's own child order.
void writeItem(pugi::xml_node parent, const Dom& dom, InstanceId id,
               std::map<std::string, std::string>& sharedStrings, EncodeDiagnostics& diags) {
    pugi::xml_node item = parent.append_child("Item");
    const Instance& inst = dom.at(id);
    item.append_attribute("class").set_value(inst.className.c_str());
    item.append_attribute("referent").set_value(referentText(id).c_str());

    pugi::xml_node props = item.append_child("Properties");
    for (const auto& prop : inst.properties) {
        writeProperty(props, id, dom.names().name(prop.first), prop.second, sharedStrings, diags);
    }

    for (InstanceId child : inst.children) {
        writeItem(item, dom, child, sharedStrings, diags);
    }
}

class StringWriter : public pugi::xml_writer {
public:
    std::string result;
    void write(const void* data, size_t size) override {
        result.append(static_cast<const char*>(data), size);
    }
};

}  // namespace

Result<std::string> encode(const Dom& dom, bool pretty, EncodeDiagnostics* diagnosticsOut) {
    EncodeDiagnostics localDiags;
    EncodeDiagnostics& diags = diagnosticsOut != nullptr ? *diagnosticsOut : localDiags;

    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("roblox");
    root.append_attribute("version").set_value("4");

    for (const auto& [key, value] : dom.metadata()) {
        pugi::xml_node meta = root.append_child("Meta");
        meta.append_attribute("name").set_value(key.c_str());
        meta.text().set(value.c_str());
    }

    // Keyed by the raw (undecoded) SharedString/NetAssetRef bytes, so both
    // property types share one table on the way out, exactly as decode.cpp
    // resolves both of them against one table on the way in. std::map keeps
    // the emitted table in a stable, deterministic order.
    std::map<std::string, std::string> sharedStrings;
    for (InstanceId rootId : dom.roots()) {
        writeItem(root, dom, rootId, sharedStrings, diags);
    }

    if (!sharedStrings.empty()) {
        pugi::xml_node sst = root.append_child("SharedStrings");
        for (const auto& [key, value] : sharedStrings) {
            pugi::xml_node entry = sst.append_child("SharedString");
            entry.append_attribute("md5").set_value(
                base64Encode(reinterpret_cast<const uint8_t*>(key.data()), key.size()).c_str());
            entry.text().set(
                base64Encode(reinterpret_cast<const uint8_t*>(value.data()), value.size())
                    .c_str());
        }
    }

    // Dom::unknownChunks() is a binary-format concept (preserved PROP/other
    // chunks this codec could not interpret) with no XML representation at
    // all; drop it, but always say so rather than silently losing data.
    if (!dom.unknownChunks().empty()) {
        diags.warnings.push_back(
            std::to_string(dom.unknownChunks().size()) +
            " unknown chunk(s) from the source binary file have no XML representation "
            "and were dropped");
    }

    StringWriter writer;
    const unsigned int flags = pugi::format_no_declaration |
                                (pretty ? pugi::format_indent : pugi::format_raw);
    doc.print(writer, "\t", flags);
    return std::move(writer.result);
}

}  // namespace xml
}  // namespace rbxl
