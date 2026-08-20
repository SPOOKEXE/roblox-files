#include "binary/valuecodec.hpp"

namespace rbxl {
namespace binary {

namespace {

// Every type id Roblox's binary format defines, whether or not this codec
// implements it. `0x0f` and `0x11` are reserved and never observed on disk.
bool isDefinedTypeId(uint8_t id) {
    switch (static_cast<TypeId>(id)) {
        case TypeId::String:
        case TypeId::Bool:
        case TypeId::Int32:
        case TypeId::Float32:
        case TypeId::Float64:
        case TypeId::UDim:
        case TypeId::UDim2:
        case TypeId::Ray:
        case TypeId::Faces:
        case TypeId::Axes:
        case TypeId::BrickColor:
        case TypeId::Color3:
        case TypeId::Vector2:
        case TypeId::Vector3:
        case TypeId::CFrame:
        case TypeId::Enum:
        case TypeId::Referent:
        case TypeId::Vector3int16:
        case TypeId::NumberSequence:
        case TypeId::ColorSequence:
        case TypeId::NumberRange:
        case TypeId::Rect:
        case TypeId::PhysicalProperties:
        case TypeId::Color3uint8:
        case TypeId::Int64:
        case TypeId::SharedString:
        case TypeId::Bytecode:
        case TypeId::OptionalCFrame:
        case TypeId::UniqueId:
        case TypeId::Font:
        case TypeId::SecurityCapabilities:
        case TypeId::Content:
            return true;
    }
    return false;
}

// --- String / Bytecode: u32 LE length prefix + bytes, in sequence ---------

Result<std::vector<Variant>> decodeStringLike(const uint8_t* data, size_t size, size_t count,
                                               bool asBytecode) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(lenBytes, c.take(4));
        const uint32_t length = bit::readU32LE(lenBytes);
        RBXL_TRY(bytes, c.take(length));
        if (asBytecode) {
            out.push_back(Bytecode{std::vector<uint8_t>(bytes, bytes + length)});
        } else {
            out.push_back(std::string(reinterpret_cast<const char*>(bytes), length));
        }
    }
    return out;
}

Status encodeStringLike(const std::vector<Variant>& values, std::vector<uint8_t>& out,
                         bool asBytecode) {
    for (const auto& v : values) {
        const uint8_t* bytes = nullptr;
        size_t length = 0;
        const std::string* s = nullptr;
        const Bytecode* b = nullptr;
        if (asBytecode) {
            b = std::get_if<Bytecode>(&v);
            if (!b) return makeError(ErrorCode::InvalidArgument, "expected Bytecode value");
            bytes = b->data.data();
            length = b->data.size();
        } else {
            s = std::get_if<std::string>(&v);
            if (!s) return makeError(ErrorCode::InvalidArgument, "expected String value");
            bytes = reinterpret_cast<const uint8_t*>(s->data());
            length = s->size();
        }
        uint8_t lenBytes[4];
        bit::writeU32LE(lenBytes, static_cast<uint32_t>(length));
        out.insert(out.end(), lenBytes, lenBytes + 4);
        out.insert(out.end(), bytes, bytes + length);
    }
    return Status();
}

// --- Bool: one byte per value ----------------------------------------------

Result<std::vector<Variant>> decodeBool(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(b, c.take(1));
        out.push_back(*b != 0);
    }
    return out;
}

Status encodeBool(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const bool* b = std::get_if<bool>(&v);
        if (!b) return makeError(ErrorCode::InvalidArgument, "expected Bool value");
        out.push_back(*b ? 0x01 : 0x00);
    }
    return Status();
}

// --- Int32: zigzag, big-endian, interleaved width 4 ------------------------

Result<std::vector<Variant>> decodeInt32(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(bit::zigzagDecode32(bit::readU32BE(flat.data() + i * 4)));
    }
    return out;
}

Status encodeInt32(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        const int32_t* n = std::get_if<int32_t>(&values[i]);
        if (!n) return makeError(ErrorCode::InvalidArgument, "expected Int32 value");
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(*n));
    }
    writeInterleaved(flat, values.size(), 4, out);
    return Status();
}

// --- Float32: Roblox float layout, big-endian, interleaved width 4 --------

Result<std::vector<Variant>> decodeFloat32(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(bit::decodeRobloxFloat(bit::readU32BE(flat.data() + i * 4)));
    }
    return out;
}

Status encodeFloat32(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        const float* f = std::get_if<float>(&values[i]);
        if (!f) return makeError(ErrorCode::InvalidArgument, "expected Float32 value");
        bit::writeU32BE(flat.data() + i * 4, bit::encodeRobloxFloat(*f));
    }
    writeInterleaved(flat, values.size(), 4, out);
    return Status();
}

// --- Float64: plain little-endian IEEE doubles, in sequence ---------------

Result<std::vector<Variant>> decodeFloat64(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(bytes, c.take(8));
        out.push_back(bit::readF64LE(bytes));
    }
    return out;
}

Status encodeFloat64(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const double* d = std::get_if<double>(&v);
        if (!d) return makeError(ErrorCode::InvalidArgument, "expected Float64 value");
        const size_t base = out.size();
        out.resize(base + 8);
        bit::writeF64LE(out.data() + base, *d);
    }
    return Status();
}

// --- Faces / Axes: one byte bitfield per value, in sequence ----------------

Result<std::vector<Variant>> decodeFaces(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(b, c.take(1));
        out.push_back(Faces{*b});
    }
    return out;
}

Status encodeFaces(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const Faces* f = std::get_if<Faces>(&v);
        if (!f) return makeError(ErrorCode::InvalidArgument, "expected Faces value");
        out.push_back(f->bits);
    }
    return Status();
}

Result<std::vector<Variant>> decodeAxes(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(b, c.take(1));
        out.push_back(Axes{*b});
    }
    return out;
}

Status encodeAxes(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const Axes* a = std::get_if<Axes>(&v);
        if (!a) return makeError(ErrorCode::InvalidArgument, "expected Axes value");
        out.push_back(a->bits);
    }
    return Status();
}

// --- BrickColor / Enum: untransformed big-endian u32, interleaved width 4 --

Result<std::vector<Variant>> decodeBrickColor(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(BrickColor{bit::readU32BE(flat.data() + i * 4)});
    }
    return out;
}

Status encodeBrickColor(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        const BrickColor* bc = std::get_if<BrickColor>(&values[i]);
        if (!bc) return makeError(ErrorCode::InvalidArgument, "expected BrickColor value");
        bit::writeU32BE(flat.data() + i * 4, bc->number);
    }
    writeInterleaved(flat, values.size(), 4, out);
    return Status();
}

Result<std::vector<Variant>> decodeEnum(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(EnumValue{bit::readU32BE(flat.data() + i * 4)});
    }
    return out;
}

Status encodeEnum(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        const EnumValue* e = std::get_if<EnumValue>(&values[i]);
        if (!e) return makeError(ErrorCode::InvalidArgument, "expected EnumValue value");
        bit::writeU32BE(flat.data() + i * 4, e->value);
    }
    writeInterleaved(flat, values.size(), 4, out);
    return Status();
}

// --- Referent: Int32 encoding, interleaved, then accumulated; -1 is null --

Result<std::vector<Variant>> decodeReferent(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(raw, readReferentDeltaArray(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const int32_t signedValue = static_cast<int32_t>(raw[i]);
        if (signedValue == -1) {
            out.push_back(Ref{kNoInstance});
        } else {
            out.push_back(Ref{static_cast<InstanceId>(signedValue)});
        }
    }
    return out;
}

Status encodeReferent(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint32_t> raw(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const Ref* r = std::get_if<Ref>(&values[i]);
        if (!r) return makeError(ErrorCode::InvalidArgument, "expected Ref value");
        raw[i] = r->target;
    }
    writeReferentDeltaArray(raw, out);
    return Status();
}

// --- Vector3int16: three little-endian i16, in sequence --------------------

Result<std::vector<Variant>> decodeVector3int16(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(bytes, c.take(6));
        Vector3int16 v;
        v.x = static_cast<int16_t>(bit::readU16LE(bytes));
        v.y = static_cast<int16_t>(bit::readU16LE(bytes + 2));
        v.z = static_cast<int16_t>(bit::readU16LE(bytes + 4));
        out.push_back(v);
    }
    return out;
}

Status encodeVector3int16(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& val : values) {
        const Vector3int16* v = std::get_if<Vector3int16>(&val);
        if (!v) return makeError(ErrorCode::InvalidArgument, "expected Vector3int16 value");
        const size_t base = out.size();
        out.resize(base + 6);
        bit::writeU16LE(out.data() + base, static_cast<uint16_t>(v->x));
        bit::writeU16LE(out.data() + base + 2, static_cast<uint16_t>(v->y));
        bit::writeU16LE(out.data() + base + 4, static_cast<uint16_t>(v->z));
    }
    return Status();
}

// --- Color3uint8: three separate byte arrays, R then G then B --------------

Result<std::vector<Variant>> decodeColor3uint8(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(r, c.take(count));
    RBXL_TRY(g, c.take(count));
    RBXL_TRY(b, c.take(count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(Color3uint8{r[i], g[i], b[i]});
    }
    return out;
}

Status encodeColor3uint8(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> r(values.size()), g(values.size()), b(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const Color3uint8* c = std::get_if<Color3uint8>(&values[i]);
        if (!c) return makeError(ErrorCode::InvalidArgument, "expected Color3uint8 value");
        r[i] = c->r; g[i] = c->g; b[i] = c->b;
    }
    out.insert(out.end(), r.begin(), r.end());
    out.insert(out.end(), g.begin(), g.end());
    out.insert(out.end(), b.begin(), b.end());
    return Status();
}

// --- Int64 / SecurityCapabilities: zigzag 64-bit, big-endian, width 8 -----

Result<std::vector<Variant>> decodeInt64(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 8));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(bit::zigzagDecode64(bit::readU64BE(flat.data() + i * 8)));
    }
    return out;
}

Status encodeInt64(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 8);
    for (size_t i = 0; i < values.size(); ++i) {
        const int64_t* n = std::get_if<int64_t>(&values[i]);
        if (!n) return makeError(ErrorCode::InvalidArgument, "expected Int64 value");
        bit::writeU64BE(flat.data() + i * 8, bit::zigzagEncode64(*n));
    }
    writeInterleaved(flat, values.size(), 8, out);
    return Status();
}

Result<std::vector<Variant>> decodeSecurityCapabilities(const uint8_t* data, size_t size,
                                                          size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 8));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const int64_t signedValue = bit::zigzagDecode64(bit::readU64BE(flat.data() + i * 8));
        out.push_back(SecurityCapabilities{static_cast<uint64_t>(signedValue)});
    }
    return out;
}

Status encodeSecurityCapabilities(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 8);
    for (size_t i = 0; i < values.size(); ++i) {
        const SecurityCapabilities* sc = std::get_if<SecurityCapabilities>(&values[i]);
        if (!sc) return makeError(ErrorCode::InvalidArgument, "expected SecurityCapabilities value");
        const int64_t signedValue = static_cast<int64_t>(sc->value);
        bit::writeU64BE(flat.data() + i * 8, bit::zigzagEncode64(signedValue));
    }
    writeInterleaved(flat, values.size(), 8, out);
    return Status();
}

// --- SharedString: big-endian u32 index into the SSTR table, width 4 ------

Result<std::vector<Variant>> decodeSharedString(const uint8_t* data, size_t size, size_t count,
                                                 const CodecContext& ctx) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t index = bit::readU32BE(flat.data() + i * 4);
        if (!ctx.sharedStrings || index >= ctx.sharedStrings->size()) {
            return makeError(ErrorCode::Malformed, "shared string index out of range", i * 4);
        }
        out.push_back((*ctx.sharedStrings)[index]);
    }
    return out;
}

Status encodeSharedString(const std::vector<Variant>& values, std::vector<uint8_t>& out,
                           const CodecContext& ctx) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        const SharedString* s = std::get_if<SharedString>(&values[i]);
        if (!s) return makeError(ErrorCode::InvalidArgument, "expected SharedString value");
        if (!ctx.sharedStrings) {
            return makeError(ErrorCode::InvalidArgument, "no shared string table to encode against");
        }
        size_t index = ctx.sharedStrings->size();
        for (size_t j = 0; j < ctx.sharedStrings->size(); ++j) {
            if ((*ctx.sharedStrings)[j].value == s->value && (*ctx.sharedStrings)[j].key == s->key) {
                index = j;
                break;
            }
        }
        if (index == ctx.sharedStrings->size()) {
            return makeError(ErrorCode::InvalidArgument, "shared string not present in table");
        }
        bit::writeU32BE(flat.data() + i * 4, static_cast<uint32_t>(index));
    }
    writeInterleaved(flat, values.size(), 4, out);
    return Status();
}

}  // namespace

Result<std::vector<Variant>> decodeValueArray(TypeId type, const uint8_t* data, size_t size,
                                               size_t count, const CodecContext& ctx) {
    switch (type) {
        case TypeId::String: return decodeStringLike(data, size, count, false);
        case TypeId::Bool: return decodeBool(data, size, count);
        case TypeId::Int32: return decodeInt32(data, size, count);
        case TypeId::Float32: return decodeFloat32(data, size, count);
        case TypeId::Float64: return decodeFloat64(data, size, count);
        case TypeId::Faces: return decodeFaces(data, size, count);
        case TypeId::Axes: return decodeAxes(data, size, count);
        case TypeId::BrickColor: return decodeBrickColor(data, size, count);
        case TypeId::Enum: return decodeEnum(data, size, count);
        case TypeId::Referent: return decodeReferent(data, size, count);
        case TypeId::Vector3int16: return decodeVector3int16(data, size, count);
        case TypeId::Color3uint8: return decodeColor3uint8(data, size, count);
        case TypeId::Int64: return decodeInt64(data, size, count);
        case TypeId::SharedString: return decodeSharedString(data, size, count, ctx);
        case TypeId::Bytecode: return decodeStringLike(data, size, count, true);
        case TypeId::SecurityCapabilities: return decodeSecurityCapabilities(data, size, count);
        default:
            return detail::decodeStructValueArray(type, data, size, count, ctx);
    }
}

Status encodeValueArray(TypeId type, const std::vector<Variant>& values,
                         std::vector<uint8_t>& out, const CodecContext& ctx) {
    switch (type) {
        case TypeId::String: return encodeStringLike(values, out, false);
        case TypeId::Bool: return encodeBool(values, out);
        case TypeId::Int32: return encodeInt32(values, out);
        case TypeId::Float32: return encodeFloat32(values, out);
        case TypeId::Float64: return encodeFloat64(values, out);
        case TypeId::Faces: return encodeFaces(values, out);
        case TypeId::Axes: return encodeAxes(values, out);
        case TypeId::BrickColor: return encodeBrickColor(values, out);
        case TypeId::Enum: return encodeEnum(values, out);
        case TypeId::Referent: return encodeReferent(values, out);
        case TypeId::Vector3int16: return encodeVector3int16(values, out);
        case TypeId::Color3uint8: return encodeColor3uint8(values, out);
        case TypeId::Int64: return encodeInt64(values, out);
        case TypeId::SharedString: return encodeSharedString(values, out, ctx);
        case TypeId::Bytecode: return encodeStringLike(values, out, true);
        case TypeId::SecurityCapabilities: return encodeSecurityCapabilities(values, out);
        default:
            return detail::encodeStructValueArray(type, values, out, ctx);
    }
}

bool isKnownTypeId(uint8_t id) {
    return isDefinedTypeId(id);
}

}  // namespace binary
}  // namespace rbxl
