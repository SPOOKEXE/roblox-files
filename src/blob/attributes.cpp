#include <rbxl/blob.hpp>
#include <rbxl/bitutil.hpp>
#include <rbxl/types.hpp>
#include <utility>

// Instance.Attributes ("AttributesSerialize"). See blob.hpp for the layout
// summary; this file is the only place that needs to know the byte-level
// detail. Unlike the binary chunk format, values here are stored one
// attribute at a time (not column-major), and every multi-byte field is
// plain little-endian with no zigzag or Roblox float rotation.
namespace rbxl {
namespace blob {

namespace {

// The attribute blob's own type id table. Deliberately a different set of
// numbers from binary::TypeId; do not conflate the two.
enum class AttrType : uint8_t {
    String = 0x02,
    Bool = 0x03,
    Int32 = 0x04,
    Float32 = 0x05,
    Float64 = 0x06,
    UDim = 0x09,
    UDim2 = 0x0A,
    BrickColor = 0x0E,
    Color3 = 0x0F,
    Vector2 = 0x10,
    Vector3 = 0x11,
    CFrame = 0x14,
    EnumItem = 0x15,
    NumberSequence = 0x17,
    ColorSequence = 0x19,
    NumberRange = 0x1B,
    Rect = 0x1C,
    Font = 0x21,
};

// Bounds-checked cursor over an untrusted attribute blob. Every read reports
// ErrorCode::Truncated rather than indexing past the buffer; a declared
// count is always checked against what actually remains before it is used
// to size a loop or a reserve().
class Cursor {
public:
    Cursor(const uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::size_t remaining() const { return size_ - pos_; }
    std::size_t position() const { return pos_; }

    Result<const uint8_t*> take(std::size_t n) {
        if (n > remaining()) {
            return makeError(ErrorCode::Truncated, "attribute blob read past end of buffer", pos_);
        }
        const uint8_t* p = data_ + pos_;
        pos_ += n;
        return p;
    }

    Result<uint32_t> takeU32() {
        RBXL_TRY(b, take(4));
        return bit::readU32LE(b);
    }
    Result<int32_t> takeI32() {
        RBXL_TRY(b, take(4));
        return static_cast<int32_t>(bit::readU32LE(b));
    }
    Result<float> takeF32() {
        RBXL_TRY(b, take(4));
        return bit::readF32LE(b);
    }
    Result<double> takeF64() {
        RBXL_TRY(b, take(8));
        return bit::readF64LE(b);
    }
    Result<std::string> takeString() {
        RBXL_TRY(len, takeU32());
        RBXL_TRY(bytes, take(len));
        return std::string(reinterpret_cast<const char*>(bytes), len);
    }

    // Reads a u32 array-length field, checked against what could possibly
    // still fit given each element is `elemSize` bytes. Guards both a
    // Truncated-shaped attack and a `count * elemSize` overflow, the same
    // way binary::readInterleaved guards its own count*width product.
    Result<uint32_t> takeCountFor(std::size_t elemSize) {
        RBXL_TRY(count, takeU32());
        if (elemSize != 0 &&
            static_cast<std::size_t>(count) > remaining() / elemSize) {
            return makeError(ErrorCode::Truncated, "declared count exceeds remaining buffer", pos_);
        }
        return count;
    }

private:
    const uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// --- Reading composite values -----------------------------------------------

Result<UDim> readUDim(Cursor& c) {
    RBXL_TRY(scale, c.takeF32());
    RBXL_TRY(offset, c.takeI32());
    return UDim{scale, offset};
}

Result<UDim2> readUDim2(Cursor& c) {
    RBXL_TRY(x, readUDim(c));
    RBXL_TRY(y, readUDim(c));
    return UDim2{x, y};
}

Result<Vector2> readVector2(Cursor& c) {
    RBXL_TRY(x, c.takeF32());
    RBXL_TRY(y, c.takeF32());
    return Vector2{x, y};
}

Result<Vector3> readVector3(Cursor& c) {
    RBXL_TRY(x, c.takeF32());
    RBXL_TRY(y, c.takeF32());
    RBXL_TRY(z, c.takeF32());
    return Vector3{x, y, z};
}

Result<Color3> readColor3(Cursor& c) {
    RBXL_TRY(r, c.takeF32());
    RBXL_TRY(g, c.takeF32());
    RBXL_TRY(b, c.takeF32());
    return Color3{r, g, b};
}

Result<BrickColor> readBrickColor(Cursor& c) {
    RBXL_TRY(v, c.takeU32());
    return BrickColor{v};
}

Result<CFrame> readCFrame(Cursor& c) {
    RBXL_TRY(pos, readVector3(c));
    CFrame cf;
    cf.position = pos;
    for (int i = 0; i < 9; ++i) {
        RBXL_TRY(f, c.takeF32());
        cf.rotation[i] = f;
    }
    return cf;
}

Result<EnumItem> readEnumItemValue(Cursor& c) {
    RBXL_TRY(enumType, c.takeString());
    RBXL_TRY(value, c.takeU32());
    return EnumItem{enumType, value};
}

Result<NumberSequence> readNumberSequence(Cursor& c) {
    RBXL_TRY(count, c.takeCountFor(12));   // time, value, envelope: 3 x f32
    NumberSequence seq;
    seq.keypoints.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RBXL_TRY(time, c.takeF32());
        RBXL_TRY(value, c.takeF32());
        RBXL_TRY(envelope, c.takeF32());
        seq.keypoints.push_back(NumberSequenceKeypoint{time, value, envelope});
    }
    return seq;
}

Result<ColorSequence> readColorSequence(Cursor& c) {
    RBXL_TRY(count, c.takeCountFor(20));   // time, r, g, b, envelope: 5 x f32
    ColorSequence seq;
    seq.keypoints.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RBXL_TRY(time, c.takeF32());
        RBXL_TRY(color, readColor3(c));
        RBXL_TRY(envelope, c.takeF32());
        seq.keypoints.push_back(ColorSequenceKeypoint{time, color, envelope});
    }
    return seq;
}

Result<NumberRange> readNumberRange(Cursor& c) {
    RBXL_TRY(lo, c.takeF32());
    RBXL_TRY(hi, c.takeF32());
    return NumberRange{lo, hi};
}

Result<Rect> readRect(Cursor& c) {
    RBXL_TRY(min, readVector2(c));
    RBXL_TRY(max, readVector2(c));
    return Rect{min, max};
}

Result<Font> readFont(Cursor& c) {
    RBXL_TRY(family, c.takeString());
    RBXL_TRY(weightBytes, c.take(2));
    RBXL_TRY(styleByte, c.take(1));
    RBXL_TRY(cachedFaceId, c.takeString());
    Font f;
    f.family = family;
    f.weight = bit::readU16LE(weightBytes);
    f.style = *styleByte;
    f.cachedFaceId = cachedFaceId;
    return f;
}

Result<Variant> readValue(Cursor& c, uint8_t typeByte) {
    switch (static_cast<AttrType>(typeByte)) {
        case AttrType::String: { RBXL_TRY(s, c.takeString()); return Variant(std::move(s)); }
        case AttrType::Bool: { RBXL_TRY(b, c.take(1)); return Variant(*b != 0); }
        case AttrType::Int32: { RBXL_TRY(n, c.takeI32()); return Variant(n); }
        case AttrType::Float32: { RBXL_TRY(f, c.takeF32()); return Variant(f); }
        case AttrType::Float64: { RBXL_TRY(d, c.takeF64()); return Variant(d); }
        case AttrType::UDim: { RBXL_TRY(u, readUDim(c)); return Variant(u); }
        case AttrType::UDim2: { RBXL_TRY(u, readUDim2(c)); return Variant(u); }
        case AttrType::BrickColor: { RBXL_TRY(bc, readBrickColor(c)); return Variant(bc); }
        case AttrType::Color3: { RBXL_TRY(col, readColor3(c)); return Variant(col); }
        case AttrType::Vector2: { RBXL_TRY(v, readVector2(c)); return Variant(v); }
        case AttrType::Vector3: { RBXL_TRY(v, readVector3(c)); return Variant(v); }
        case AttrType::CFrame: { RBXL_TRY(cf, readCFrame(c)); return Variant(cf); }
        case AttrType::EnumItem: { RBXL_TRY(e, readEnumItemValue(c)); return Variant(e); }
        case AttrType::NumberSequence: { RBXL_TRY(ns, readNumberSequence(c)); return Variant(ns); }
        case AttrType::ColorSequence: { RBXL_TRY(cs, readColorSequence(c)); return Variant(cs); }
        case AttrType::NumberRange: { RBXL_TRY(nr, readNumberRange(c)); return Variant(nr); }
        case AttrType::Rect: { RBXL_TRY(r, readRect(c)); return Variant(r); }
        case AttrType::Font: { RBXL_TRY(f, readFont(c)); return Variant(f); }
    }
    return makeError(ErrorCode::UnsupportedType, "unknown attribute type id", c.position());
}

// --- Writing -----------------------------------------------------------

void appendU16(std::vector<uint8_t>& out, uint16_t v) {
    const std::size_t base = out.size();
    out.resize(base + 2);
    bit::writeU16LE(out.data() + base, v);
}
void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    const std::size_t base = out.size();
    out.resize(base + 4);
    bit::writeU32LE(out.data() + base, v);
}
void appendF32(std::vector<uint8_t>& out, float v) {
    const std::size_t base = out.size();
    out.resize(base + 4);
    bit::writeF32LE(out.data() + base, v);
}
void appendF64(std::vector<uint8_t>& out, double v) {
    const std::size_t base = out.size();
    out.resize(base + 8);
    bit::writeF64LE(out.data() + base, v);
}
void appendString(std::vector<uint8_t>& out, const std::string& s) {
    appendU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}
void appendUDim(std::vector<uint8_t>& out, const UDim& u) {
    appendF32(out, u.scale);
    appendU32(out, static_cast<uint32_t>(u.offset));
}
void appendVector2(std::vector<uint8_t>& out, const Vector2& v) {
    appendF32(out, v.x);
    appendF32(out, v.y);
}
void appendVector3(std::vector<uint8_t>& out, const Vector3& v) {
    appendF32(out, v.x);
    appendF32(out, v.y);
    appendF32(out, v.z);
}
void appendColor3(std::vector<uint8_t>& out, const Color3& c) {
    appendF32(out, c.r);
    appendF32(out, c.g);
    appendF32(out, c.b);
}

// Appends `value`'s attribute-blob encoding (type byte, then payload) to
// `out`. Returns false, appending nothing, when `value` holds a Variant
// alternative the attribute format has no id for; serializeAttributes drops
// such an entry entirely rather than emit a corrupt record, since this API
// has no error channel back to the caller.
bool appendValue(const Variant& value, std::vector<uint8_t>& out) {
    switch (variantTypeOf(value)) {
        case VariantType::String:
            out.push_back(static_cast<uint8_t>(AttrType::String));
            appendString(out, std::get<std::string>(value));
            return true;
        case VariantType::Bool:
            out.push_back(static_cast<uint8_t>(AttrType::Bool));
            out.push_back(std::get<bool>(value) ? 0x01 : 0x00);
            return true;
        case VariantType::Int32:
            out.push_back(static_cast<uint8_t>(AttrType::Int32));
            appendU32(out, static_cast<uint32_t>(std::get<int32_t>(value)));
            return true;
        case VariantType::Float32:
            out.push_back(static_cast<uint8_t>(AttrType::Float32));
            appendF32(out, std::get<float>(value));
            return true;
        case VariantType::Float64:
            out.push_back(static_cast<uint8_t>(AttrType::Float64));
            appendF64(out, std::get<double>(value));
            return true;
        case VariantType::UDim:
            out.push_back(static_cast<uint8_t>(AttrType::UDim));
            appendUDim(out, std::get<UDim>(value));
            return true;
        case VariantType::UDim2: {
            out.push_back(static_cast<uint8_t>(AttrType::UDim2));
            const UDim2& u2 = std::get<UDim2>(value);
            appendUDim(out, u2.x);
            appendUDim(out, u2.y);
            return true;
        }
        case VariantType::BrickColor:
            out.push_back(static_cast<uint8_t>(AttrType::BrickColor));
            appendU32(out, std::get<BrickColor>(value).number);
            return true;
        case VariantType::Color3:
            out.push_back(static_cast<uint8_t>(AttrType::Color3));
            appendColor3(out, std::get<Color3>(value));
            return true;
        case VariantType::Vector2:
            out.push_back(static_cast<uint8_t>(AttrType::Vector2));
            appendVector2(out, std::get<Vector2>(value));
            return true;
        case VariantType::Vector3:
            out.push_back(static_cast<uint8_t>(AttrType::Vector3));
            appendVector3(out, std::get<Vector3>(value));
            return true;
        case VariantType::CFrame: {
            out.push_back(static_cast<uint8_t>(AttrType::CFrame));
            const CFrame& cf = std::get<CFrame>(value);
            appendVector3(out, cf.position);
            for (float f : cf.rotation) appendF32(out, f);
            return true;
        }
        case VariantType::EnumItem: {
            out.push_back(static_cast<uint8_t>(AttrType::EnumItem));
            const EnumItem& e = std::get<EnumItem>(value);
            appendString(out, e.enumType);
            appendU32(out, e.value);
            return true;
        }
        case VariantType::NumberSequence: {
            out.push_back(static_cast<uint8_t>(AttrType::NumberSequence));
            const NumberSequence& ns = std::get<NumberSequence>(value);
            appendU32(out, static_cast<uint32_t>(ns.keypoints.size()));
            for (const auto& kp : ns.keypoints) {
                appendF32(out, kp.time);
                appendF32(out, kp.value);
                appendF32(out, kp.envelope);
            }
            return true;
        }
        case VariantType::ColorSequence: {
            out.push_back(static_cast<uint8_t>(AttrType::ColorSequence));
            const ColorSequence& cs = std::get<ColorSequence>(value);
            appendU32(out, static_cast<uint32_t>(cs.keypoints.size()));
            for (const auto& kp : cs.keypoints) {
                appendF32(out, kp.time);
                appendColor3(out, kp.color);
                appendF32(out, kp.envelope);
            }
            return true;
        }
        case VariantType::NumberRange: {
            out.push_back(static_cast<uint8_t>(AttrType::NumberRange));
            const NumberRange& nr = std::get<NumberRange>(value);
            appendF32(out, nr.min);
            appendF32(out, nr.max);
            return true;
        }
        case VariantType::Rect: {
            out.push_back(static_cast<uint8_t>(AttrType::Rect));
            const Rect& r = std::get<Rect>(value);
            appendVector2(out, r.min);
            appendVector2(out, r.max);
            return true;
        }
        case VariantType::Font: {
            out.push_back(static_cast<uint8_t>(AttrType::Font));
            const Font& f = std::get<Font>(value);
            appendString(out, f.family);
            appendU16(out, f.weight);
            out.push_back(f.style);
            appendString(out, f.cachedFaceId);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

Result<AttributeMap> parseAttributes(const std::vector<uint8_t>& data) {
    // A completely empty buffer is how Roblox represents "no attributes";
    // it never writes out a 4-byte zero count for this case (confirmed
    // against 208k+ AttributesSerialize properties in a real place file).
    if (data.empty()) {
        return AttributeMap{};
    }

    Cursor c(data.data(), data.size());
    // A record's smallest possible footprint is a zero-length name (4 bytes)
    // plus a type byte (1 byte); bounding the declared count against that
    // keeps a crafted huge count from driving an oversized reserve().
    RBXL_TRY(count, c.takeCountFor(5));

    AttributeMap out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        RBXL_TRY(name, c.takeString());
        RBXL_TRY(typeByte, c.take(1));
        RBXL_TRY(value, readValue(c, *typeByte));
        out.emplace_back(std::move(name), std::move(value));
    }
    if (c.remaining() != 0) {
        return makeError(ErrorCode::Malformed, "trailing bytes after last attribute", c.position());
    }
    return out;
}

std::vector<uint8_t> serializeAttributes(const AttributeMap& attributes) {
    std::vector<uint8_t> body;
    uint32_t count = 0;
    for (const auto& [name, value] : attributes) {
        std::vector<uint8_t> record;
        appendString(record, name);
        if (!appendValue(value, record)) {
            continue;   // no attribute-blob encoding for this Variant alternative
        }
        body.insert(body.end(), record.begin(), record.end());
        ++count;
    }
    if (count == 0) {
        return {};   // matches the empty-buffer convention parseAttributes reads
    }
    std::vector<uint8_t> out;
    appendU32(out, count);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace blob
}  // namespace rbxl
