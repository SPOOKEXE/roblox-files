#include "binary/valuecodec.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// --- CFrame's 24 special rotation matrices ----------------------------------
//
// A CFrame's rotation is usually one of 24 axis-aligned orientations (the
// rotational symmetries of a cube), and the binary format spends one byte on
// an id for those instead of a 36-byte matrix. The table below is transcribed
// verbatim (as exact -1/0/1 basis vectors, never through std::sin/std::cos)
// from rbx-dom's `Matrix3::from_basic_rotation_id`, the reference
// implementation this project's PLAN.md cites for this table. Each entry's
// three rows come from that source's `x`, `y`, `z` fields directly: for a
// CFrame, that struct's `x`/`y`/`z` are rows R0/R1/R2 of the row-major
// rotation matrix (confirmed against rbx_binary's (de)serializer, which reads
// and writes them as nine sequential little-endian floats with no
// transposition), so each row here is copied straight from that source with
// no algebraic transformation of its own that could introduce a sign error.
//
// These are placed at global scope (not inside rbxl::binary) with external,
// non-anonymous linkage for the three accessor functions below, because
// Task 16's test reaches them via a plain `extern` declaration at file scope.

namespace {

struct CFrameRotation {
    std::uint8_t id;
    float matrix[9];  // row-major: R00 R01 R02 R10 R11 R12 R20 R21 R22
};

constexpr std::size_t kCFrameRotationCount = 24;

constexpr std::array<CFrameRotation, kCFrameRotationCount> kCFrameRotations{{
    {0x02, {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    {0x03, {1, 0, 0, 0, 0, -1, 0, 1, 0}},
    {0x05, {1, 0, 0, 0, -1, 0, 0, 0, -1}},
    {0x06, {1, 0, 0, 0, 0, 1, 0, -1, 0}},
    {0x07, {0, 1, 0, 1, 0, 0, 0, 0, -1}},
    {0x09, {0, 0, 1, 1, 0, 0, 0, 1, 0}},
    {0x0a, {0, -1, 0, 1, 0, 0, 0, 0, 1}},
    {0x0c, {0, 0, -1, 1, 0, 0, 0, -1, 0}},
    {0x0d, {0, 1, 0, 0, 0, 1, 1, 0, 0}},
    {0x0e, {0, 0, -1, 0, 1, 0, 1, 0, 0}},
    {0x10, {0, -1, 0, 0, 0, -1, 1, 0, 0}},
    {0x11, {0, 0, 1, 0, -1, 0, 1, 0, 0}},
    {0x14, {-1, 0, 0, 0, 1, 0, 0, 0, -1}},
    {0x15, {-1, 0, 0, 0, 0, 1, 0, 1, 0}},
    {0x17, {-1, 0, 0, 0, -1, 0, 0, 0, 1}},
    {0x18, {-1, 0, 0, 0, 0, -1, 0, -1, 0}},
    {0x19, {0, 1, 0, -1, 0, 0, 0, 0, 1}},
    {0x1b, {0, 0, -1, -1, 0, 0, 0, 1, 0}},
    {0x1c, {0, -1, 0, -1, 0, 0, 0, 0, -1}},
    {0x1e, {0, 0, 1, -1, 0, 0, 0, -1, 0}},
    {0x1f, {0, 1, 0, 0, 0, -1, -1, 0, 0}},
    {0x20, {0, 0, 1, 0, 1, 0, -1, 0, 0}},
    {0x22, {0, -1, 0, 0, 0, 1, -1, 0, 0}},
    {0x23, {0, 0, -1, 0, -1, 0, -1, 0, 0}},
}};

// Exact element-wise match: intentionally not a tolerance comparison. A
// near-miss (e.g. a stray 6.1e-17 from computing this table with std::sin)
// must fall back to writing a full matrix, not silently claim the wrong id.
std::uint8_t findRotationId(const float rotation[9]) {
    for (const auto& entry : kCFrameRotations) {
        bool match = true;
        for (int k = 0; k < 9; ++k) {
            if (entry.matrix[k] != rotation[k]) { match = false; break; }
        }
        if (match) return entry.id;
    }
    return 0x00;
}

}  // namespace

const float* cframeRotationMatrix(std::uint8_t id) {
    for (const auto& entry : kCFrameRotations) {
        if (entry.id == id) return entry.matrix;
    }
    return nullptr;
}

const std::uint8_t* cframeRotationIds() {
    static const std::array<std::uint8_t, kCFrameRotationCount> ids = [] {
        std::array<std::uint8_t, kCFrameRotationCount> out{};
        for (std::size_t i = 0; i < kCFrameRotations.size(); ++i) out[i] = kCFrameRotations[i].id;
        return out;
    }();
    return ids.data();
}

std::size_t cframeRotationIdCount() { return kCFrameRotationCount; }

namespace rbxl {
namespace binary {

namespace {

// --- Shared component-array helpers -----------------------------------------
//
// Every "structure of arrays" composite type (UDim, UDim2, Color3, Vector2,
// Vector3, Rect, and CFrame's position) is built from these two pairs rather
// than hand-rolling readInterleaved/writeInterleaved per type. Float32 uses
// the Roblox float layout (matching Task 6's scalar Float32 codec); Int32
// uses zigzag (matching Task 6's scalar Int32 codec). Both are big-endian and
// interleaved at width 4, same as every other interleaved array in this file.

Result<std::vector<float>> readFloat32Array(Cursor& c, size_t count) {
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<float> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = bit::decodeRobloxFloat(bit::readU32BE(flat.data() + i * 4));
    }
    return out;
}

void writeFloat32Array(const std::vector<float>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        bit::writeU32BE(flat.data() + i * 4, bit::encodeRobloxFloat(values[i]));
    }
    writeInterleaved(flat, values.size(), 4, out);
}

Result<std::vector<int32_t>> readInt32Array(Cursor& c, size_t count) {
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<int32_t> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = bit::zigzagDecode32(bit::readU32BE(flat.data() + i * 4));
    }
    return out;
}

void writeInt32Array(const std::vector<int32_t>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(values[i]));
    }
    writeInterleaved(flat, values.size(), 4, out);
}

// Rejects a file-derived element count if the bytes it would need cannot
// possibly still be in the buffer, before anything gets sized (reserved or
// resized) by it. Applies to counts read from inside the byte stream itself
// (a NumberSequence/ColorSequence keypoint count, one of Content's three
// section counts) as opposed to the `count` parameter every decode function
// receives, which is the caller's responsibility.
Status requireCountFits(const Cursor& c, size_t count, size_t minBytesPerElement,
                         const char* what) {
    if (minBytesPerElement != 0 && count > c.remaining() / minBytesPerElement) {
        return makeError(ErrorCode::Malformed, what, c.position());
    }
    return Status();
}

// --- UDim (0x06): Scale array, then Offset array ----------------------------

Result<std::vector<Variant>> decodeUDim(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(scales, readFloat32Array(c, count));
    RBXL_TRY(offsets, readInt32Array(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) out.push_back(UDim{scales[i], offsets[i]});
    return out;
}

Status encodeUDim(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<float> scales(values.size());
    std::vector<int32_t> offsets(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const UDim* u = std::get_if<UDim>(&values[i]);
        if (!u) return makeError(ErrorCode::InvalidArgument, "expected UDim value");
        scales[i] = u->scale;
        offsets[i] = u->offset;
    }
    writeFloat32Array(scales, out);
    writeInt32Array(offsets, out);
    return Status();
}

// --- UDim2 (0x07): X.Scale, Y.Scale, X.Offset, Y.Offset ---------------------

Result<std::vector<Variant>> decodeUDim2(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(xScale, readFloat32Array(c, count));
    RBXL_TRY(yScale, readFloat32Array(c, count));
    RBXL_TRY(xOffset, readInt32Array(c, count));
    RBXL_TRY(yOffset, readInt32Array(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(UDim2{UDim{xScale[i], xOffset[i]}, UDim{yScale[i], yOffset[i]}});
    }
    return out;
}

Status encodeUDim2(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<float> xScale(values.size()), yScale(values.size());
    std::vector<int32_t> xOffset(values.size()), yOffset(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const UDim2* u = std::get_if<UDim2>(&values[i]);
        if (!u) return makeError(ErrorCode::InvalidArgument, "expected UDim2 value");
        xScale[i] = u->x.scale; xOffset[i] = u->x.offset;
        yScale[i] = u->y.scale; yOffset[i] = u->y.offset;
    }
    writeFloat32Array(xScale, out);
    writeFloat32Array(yScale, out);
    writeInt32Array(xOffset, out);
    writeInt32Array(yOffset, out);
    return Status();
}

// --- Ray (0x08): six plain little-endian floats, in sequence ---------------

Result<std::vector<Variant>> decodeRay(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(bytes, c.take(24));
        Ray r;
        r.origin.x = bit::readF32LE(bytes);
        r.origin.y = bit::readF32LE(bytes + 4);
        r.origin.z = bit::readF32LE(bytes + 8);
        r.direction.x = bit::readF32LE(bytes + 12);
        r.direction.y = bit::readF32LE(bytes + 16);
        r.direction.z = bit::readF32LE(bytes + 20);
        out.push_back(r);
    }
    return out;
}

Status encodeRay(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const Ray* r = std::get_if<Ray>(&v);
        if (!r) return makeError(ErrorCode::InvalidArgument, "expected Ray value");
        const size_t base = out.size();
        out.resize(base + 24);
        bit::writeF32LE(out.data() + base, r->origin.x);
        bit::writeF32LE(out.data() + base + 4, r->origin.y);
        bit::writeF32LE(out.data() + base + 8, r->origin.z);
        bit::writeF32LE(out.data() + base + 12, r->direction.x);
        bit::writeF32LE(out.data() + base + 16, r->direction.y);
        bit::writeF32LE(out.data() + base + 20, r->direction.z);
    }
    return Status();
}

// --- Color3 (0x0c): R, G, B component arrays --------------------------------

Result<std::vector<Variant>> decodeColor3(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(r, readFloat32Array(c, count));
    RBXL_TRY(g, readFloat32Array(c, count));
    RBXL_TRY(b, readFloat32Array(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) out.push_back(Color3{r[i], g[i], b[i]});
    return out;
}

Status encodeColor3(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<float> r(values.size()), g(values.size()), b(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const Color3* c = std::get_if<Color3>(&values[i]);
        if (!c) return makeError(ErrorCode::InvalidArgument, "expected Color3 value");
        r[i] = c->r; g[i] = c->g; b[i] = c->b;
    }
    writeFloat32Array(r, out);
    writeFloat32Array(g, out);
    writeFloat32Array(b, out);
    return Status();
}

// --- Vector2 (0x0d): X, Y component arrays ----------------------------------

Result<std::vector<Variant>> decodeVector2(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(x, readFloat32Array(c, count));
    RBXL_TRY(y, readFloat32Array(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) out.push_back(Vector2{x[i], y[i]});
    return out;
}

Status encodeVector2(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<float> x(values.size()), y(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const Vector2* v = std::get_if<Vector2>(&values[i]);
        if (!v) return makeError(ErrorCode::InvalidArgument, "expected Vector2 value");
        x[i] = v->x; y[i] = v->y;
    }
    writeFloat32Array(x, out);
    writeFloat32Array(y, out);
    return Status();
}

// --- Vector3 (0x0e): X, Y, Z component arrays -------------------------------

Result<std::vector<Variant>> decodeVector3(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(x, readFloat32Array(c, count));
    RBXL_TRY(y, readFloat32Array(c, count));
    RBXL_TRY(z, readFloat32Array(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) out.push_back(Vector3{x[i], y[i], z[i]});
    return out;
}

Status encodeVector3(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<float> x(values.size()), y(values.size()), z(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const Vector3* v = std::get_if<Vector3>(&values[i]);
        if (!v) return makeError(ErrorCode::InvalidArgument, "expected Vector3 value");
        x[i] = v->x; y[i] = v->y; z[i] = v->z;
    }
    writeFloat32Array(x, out);
    writeFloat32Array(y, out);
    writeFloat32Array(z, out);
    return Status();
}

// --- CFrame (0x10): id/matrix bytes for all values, then a position array --
//
// Shared by TypeId::CFrame directly and by OptionalCFrame's embedded CFrame
// array, since both use exactly this layout.

Result<std::vector<CFrame>> decodeCFrameArray(Cursor& c, size_t count) {
    std::vector<CFrame> frames(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(idByte, c.take(1));
        const uint8_t id = *idByte;
        if (id == 0x00) {
            // The matrix here is plain IEEE-754 little-endian, not the
            // Roblox float format the position array below uses.
            RBXL_TRY(bytes, c.take(36));
            for (int k = 0; k < 9; ++k) frames[i].rotation[k] = bit::readF32LE(bytes + k * 4);
        } else {
            const float* m = cframeRotationMatrix(id);
            if (!m) {
                return makeError(ErrorCode::Malformed, "invalid CFrame rotation id", c.position());
            }
            for (int k = 0; k < 9; ++k) frames[i].rotation[k] = m[k];
        }
    }
    RBXL_TRY(xs, readFloat32Array(c, count));
    RBXL_TRY(ys, readFloat32Array(c, count));
    RBXL_TRY(zs, readFloat32Array(c, count));
    for (size_t i = 0; i < count; ++i) frames[i].position = Vector3{xs[i], ys[i], zs[i]};
    return frames;
}

void encodeCFrameArray(const std::vector<CFrame>& frames, std::vector<uint8_t>& out) {
    for (const auto& f : frames) {
        const uint8_t id = findRotationId(f.rotation);
        out.push_back(id);
        if (id == 0x00) {
            const size_t base = out.size();
            out.resize(base + 36);
            for (int k = 0; k < 9; ++k) bit::writeF32LE(out.data() + base + k * 4, f.rotation[k]);
        }
    }
    std::vector<float> xs(frames.size()), ys(frames.size()), zs(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        xs[i] = frames[i].position.x;
        ys[i] = frames[i].position.y;
        zs[i] = frames[i].position.z;
    }
    writeFloat32Array(xs, out);
    writeFloat32Array(ys, out);
    writeFloat32Array(zs, out);
}

Result<std::vector<Variant>> decodeCFrame(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(frames, decodeCFrameArray(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (const auto& f : frames) out.push_back(f);
    return out;
}

Status encodeCFrame(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<CFrame> frames(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const CFrame* f = std::get_if<CFrame>(&values[i]);
        if (!f) return makeError(ErrorCode::InvalidArgument, "expected CFrame value");
        frames[i] = *f;
    }
    encodeCFrameArray(frames, out);
    return Status();
}

// --- NumberSequence (0x15): u32 count + count keypoint triples, per value --

Result<std::vector<Variant>> decodeNumberSequence(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(lenBytes, c.take(4));
        const uint32_t keypointCount = bit::readU32LE(lenBytes);
        RBXL_TRY_VOID(requireCountFits(c, keypointCount, 12,
                                        "NumberSequence keypoint count exceeds remaining data"));
        NumberSequence seq;
        seq.keypoints.reserve(keypointCount);
        for (uint32_t k = 0; k < keypointCount; ++k) {
            RBXL_TRY(bytes, c.take(12));
            NumberSequenceKeypoint kp;
            kp.time = bit::readF32LE(bytes);
            kp.value = bit::readF32LE(bytes + 4);
            kp.envelope = bit::readF32LE(bytes + 8);
            seq.keypoints.push_back(kp);
        }
        out.push_back(std::move(seq));
    }
    return out;
}

Status encodeNumberSequence(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const NumberSequence* s = std::get_if<NumberSequence>(&v);
        if (!s) return makeError(ErrorCode::InvalidArgument, "expected NumberSequence value");
        uint8_t lenBytes[4];
        bit::writeU32LE(lenBytes, static_cast<uint32_t>(s->keypoints.size()));
        out.insert(out.end(), lenBytes, lenBytes + 4);
        for (const auto& kp : s->keypoints) {
            const size_t base = out.size();
            out.resize(base + 12);
            bit::writeF32LE(out.data() + base, kp.time);
            bit::writeF32LE(out.data() + base + 4, kp.value);
            bit::writeF32LE(out.data() + base + 8, kp.envelope);
        }
    }
    return Status();
}

// --- ColorSequence (0x16): u32 count + count keypoint quintuples, per value

Result<std::vector<Variant>> decodeColorSequence(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(lenBytes, c.take(4));
        const uint32_t keypointCount = bit::readU32LE(lenBytes);
        RBXL_TRY_VOID(requireCountFits(c, keypointCount, 20,
                                        "ColorSequence keypoint count exceeds remaining data"));
        ColorSequence seq;
        seq.keypoints.reserve(keypointCount);
        for (uint32_t k = 0; k < keypointCount; ++k) {
            RBXL_TRY(bytes, c.take(20));
            ColorSequenceKeypoint kp;
            kp.time = bit::readF32LE(bytes);
            kp.color.r = bit::readF32LE(bytes + 4);
            kp.color.g = bit::readF32LE(bytes + 8);
            kp.color.b = bit::readF32LE(bytes + 12);
            kp.envelope = bit::readF32LE(bytes + 16);
            seq.keypoints.push_back(kp);
        }
        out.push_back(std::move(seq));
    }
    return out;
}

Status encodeColorSequence(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const ColorSequence* s = std::get_if<ColorSequence>(&v);
        if (!s) return makeError(ErrorCode::InvalidArgument, "expected ColorSequence value");
        uint8_t lenBytes[4];
        bit::writeU32LE(lenBytes, static_cast<uint32_t>(s->keypoints.size()));
        out.insert(out.end(), lenBytes, lenBytes + 4);
        for (const auto& kp : s->keypoints) {
            const size_t base = out.size();
            out.resize(base + 20);
            bit::writeF32LE(out.data() + base, kp.time);
            bit::writeF32LE(out.data() + base + 4, kp.color.r);
            bit::writeF32LE(out.data() + base + 8, kp.color.g);
            bit::writeF32LE(out.data() + base + 12, kp.color.b);
            bit::writeF32LE(out.data() + base + 16, kp.envelope);
        }
    }
    return Status();
}

// --- NumberRange (0x17): two plain little-endian floats, in sequence -------

Result<std::vector<Variant>> decodeNumberRange(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(bytes, c.take(8));
        out.push_back(NumberRange{bit::readF32LE(bytes), bit::readF32LE(bytes + 4)});
    }
    return out;
}

Status encodeNumberRange(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const NumberRange* r = std::get_if<NumberRange>(&v);
        if (!r) return makeError(ErrorCode::InvalidArgument, "expected NumberRange value");
        const size_t base = out.size();
        out.resize(base + 8);
        bit::writeF32LE(out.data() + base, r->min);
        bit::writeF32LE(out.data() + base + 4, r->max);
    }
    return Status();
}

// --- Rect (0x18): Min.X, Min.Y, Max.X, Max.Y component arrays --------------

Result<std::vector<Variant>> decodeRect(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(minX, readFloat32Array(c, count));
    RBXL_TRY(minY, readFloat32Array(c, count));
    RBXL_TRY(maxX, readFloat32Array(c, count));
    RBXL_TRY(maxY, readFloat32Array(c, count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(Rect{Vector2{minX[i], minY[i]}, Vector2{maxX[i], maxY[i]}});
    }
    return out;
}

Status encodeRect(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<float> minX(values.size()), minY(values.size());
    std::vector<float> maxX(values.size()), maxY(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const Rect* r = std::get_if<Rect>(&values[i]);
        if (!r) return makeError(ErrorCode::InvalidArgument, "expected Rect value");
        minX[i] = r->min.x; minY[i] = r->min.y;
        maxX[i] = r->max.x; maxY[i] = r->max.y;
    }
    writeFloat32Array(minX, out);
    writeFloat32Array(minY, out);
    writeFloat32Array(maxX, out);
    writeFloat32Array(maxY, out);
    return Status();
}

// --- PhysicalProperties (0x19): u8 bitfield + 0/5/6 plain floats, per value

Result<std::vector<Variant>> decodePhysicalProperties(const uint8_t* data, size_t size,
                                                        size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(bits, c.take(1));
        PhysicalProperties p;
        p.custom = (*bits & 0x01) != 0;
        p.hasAcousticAbsorption = (*bits & 0x02) != 0;
        if (p.custom) {
            RBXL_TRY(bytes, c.take(20));
            p.density = bit::readF32LE(bytes);
            p.friction = bit::readF32LE(bytes + 4);
            p.elasticity = bit::readF32LE(bytes + 8);
            p.frictionWeight = bit::readF32LE(bytes + 12);
            p.elasticityWeight = bit::readF32LE(bytes + 16);
            if (p.hasAcousticAbsorption) {
                RBXL_TRY(abytes, c.take(4));
                p.acousticAbsorption = bit::readF32LE(abytes);
            } else {
                p.acousticAbsorption = 1.0f;
            }
        }
        // custom == false: no floats follow regardless of the acoustic bit.
        out.push_back(p);
    }
    return out;
}

Status encodePhysicalProperties(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const PhysicalProperties* p = std::get_if<PhysicalProperties>(&v);
        if (!p) return makeError(ErrorCode::InvalidArgument, "expected PhysicalProperties value");
        const uint8_t bits =
            (p->custom ? 0x01 : 0x00) | (p->hasAcousticAbsorption ? 0x02 : 0x00);
        out.push_back(bits);
        if (p->custom) {
            const size_t base = out.size();
            out.resize(base + 20);
            bit::writeF32LE(out.data() + base, p->density);
            bit::writeF32LE(out.data() + base + 4, p->friction);
            bit::writeF32LE(out.data() + base + 8, p->elasticity);
            bit::writeF32LE(out.data() + base + 12, p->frictionWeight);
            bit::writeF32LE(out.data() + base + 16, p->elasticityWeight);
            if (p->hasAcousticAbsorption) {
                const size_t base2 = out.size();
                out.resize(base2 + 4);
                bit::writeF32LE(out.data() + base2, p->acousticAbsorption);
            }
        }
    }
    return Status();
}

// --- OptionalCFrame (0x1e): 0x10, CFrame array, 0x02, Bool array -----------

Result<std::vector<Variant>> decodeOptionalCFrame(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(marker1, c.take(1));
    if (*marker1 != static_cast<uint8_t>(TypeId::CFrame)) {
        return makeError(ErrorCode::Malformed, "OptionalCFrame missing inner CFrame type marker",
                          c.position());
    }
    RBXL_TRY(frames, decodeCFrameArray(c, count));
    RBXL_TRY(marker2, c.take(1));
    if (*marker2 != static_cast<uint8_t>(TypeId::Bool)) {
        return makeError(ErrorCode::Malformed, "OptionalCFrame missing inner Bool type marker",
                          c.position());
    }
    RBXL_TRY(boolBytes, c.take(count));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        OptionalCFrame v;
        v.hasValue = boolBytes[i] != 0;
        v.value = frames[i];
        out.push_back(v);
    }
    return out;
}

Status encodeOptionalCFrame(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<CFrame> frames(values.size());
    std::vector<uint8_t> bools(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        const OptionalCFrame* o = std::get_if<OptionalCFrame>(&values[i]);
        if (!o) return makeError(ErrorCode::InvalidArgument, "expected OptionalCFrame value");
        if (o->hasValue) {
            frames[i] = o->value;
            bools[i] = 0x01;
        } else {
            frames[i] = CFrame{};  // identity: absent values still occupy a slot
            bools[i] = 0x00;
        }
    }
    out.push_back(static_cast<uint8_t>(TypeId::CFrame));
    encodeCFrameArray(frames, out);
    out.push_back(static_cast<uint8_t>(TypeId::Bool));
    out.insert(out.end(), bools.begin(), bools.end());
    return Status();
}

// --- UniqueId (0x1f): 16-byte records, interleaved at width 16 -------------

uint64_t rotr64(uint64_t v) { return (v >> 1) | (v << 63); }
uint64_t rotl64(uint64_t v) { return (v << 1) | (v >> 63); }

Result<std::vector<Variant>> decodeUniqueId(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(flat, readInterleaved(c, count, 16));
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* rec = flat.data() + i * 16;
        UniqueId u;
        u.index = bit::readU32BE(rec);
        u.time = bit::readU32BE(rec + 4);
        u.random = static_cast<int64_t>(rotr64(bit::readU64BE(rec + 8)));
        out.push_back(u);
    }
    return out;
}

Status encodeUniqueId(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<uint8_t> flat(values.size() * 16);
    for (size_t i = 0; i < values.size(); ++i) {
        const UniqueId* u = std::get_if<UniqueId>(&values[i]);
        if (!u) return makeError(ErrorCode::InvalidArgument, "expected UniqueId value");
        uint8_t* rec = flat.data() + i * 16;
        bit::writeU32BE(rec, u->index);
        bit::writeU32BE(rec + 4, u->time);
        bit::writeU64BE(rec + 8, rotl64(static_cast<uint64_t>(u->random)));
    }
    writeInterleaved(flat, values.size(), 16, out);
    return Status();
}

// --- Font (0x20): family String, LE u16 weight, u8 style, cachedFaceId -----

Result<std::vector<Variant>> decodeFont(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    std::vector<Variant> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        RBXL_TRY(familyLenBytes, c.take(4));
        const uint32_t familyLen = bit::readU32LE(familyLenBytes);
        RBXL_TRY(familyBytes, c.take(familyLen));
        RBXL_TRY(weightBytes, c.take(2));
        RBXL_TRY(styleByte, c.take(1));
        RBXL_TRY(faceLenBytes, c.take(4));
        const uint32_t faceLen = bit::readU32LE(faceLenBytes);
        RBXL_TRY(faceBytes, c.take(faceLen));
        Font f;
        f.family.assign(reinterpret_cast<const char*>(familyBytes), familyLen);
        f.weight = bit::readU16LE(weightBytes);
        f.style = *styleByte;
        f.cachedFaceId.assign(reinterpret_cast<const char*>(faceBytes), faceLen);
        out.push_back(std::move(f));
    }
    return out;
}

Status encodeFont(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    for (const auto& v : values) {
        const Font* f = std::get_if<Font>(&v);
        if (!f) return makeError(ErrorCode::InvalidArgument, "expected Font value");
        uint8_t lenBytes[4];
        bit::writeU32LE(lenBytes, static_cast<uint32_t>(f->family.size()));
        out.insert(out.end(), lenBytes, lenBytes + 4);
        out.insert(out.end(), f->family.begin(), f->family.end());
        uint8_t weightBytes[2];
        bit::writeU16LE(weightBytes, f->weight);
        out.insert(out.end(), weightBytes, weightBytes + 2);
        out.push_back(f->style);
        bit::writeU32LE(lenBytes, static_cast<uint32_t>(f->cachedFaceId.size()));
        out.insert(out.end(), lenBytes, lenBytes + 4);
        out.insert(out.end(), f->cachedFaceId.begin(), f->cachedFaceId.end());
    }
    return Status();
}

// --- Content (0x22): source-type array, then counted URI/object/external --
//
// Object and external-object referents are accumulated the same way plain
// Referent (0x13) values are: zigzag, big-endian, interleaved width 4, then
// summed. `readInt32Array`/`writeInt32Array` above already give us the
// zigzag transform (unaccumulated) for the source-type array itself; the
// referent sections need the additional running-sum step, so that part is
// factored out separately rather than duplicated inline.

Result<std::vector<uint32_t>> readReferentArray(Cursor& c, size_t count) {
    RBXL_TRY(flat, readInterleaved(c, count, 4));
    std::vector<uint32_t> raw(count);
    for (size_t i = 0; i < count; ++i) {
        raw[i] = static_cast<uint32_t>(bit::zigzagDecode32(bit::readU32BE(flat.data() + i * 4)));
        if (i > 0) raw[i] += raw[i - 1];
    }
    return raw;
}

void writeReferentArray(std::vector<uint32_t> raw, std::vector<uint8_t>& out) {
    for (size_t i = raw.size(); i-- > 1;) raw[i] -= raw[i - 1];
    std::vector<uint8_t> flat(raw.size() * 4);
    for (size_t i = 0; i < raw.size(); ++i) {
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(static_cast<int32_t>(raw[i])));
    }
    writeInterleaved(flat, raw.size(), 4, out);
}

Result<std::vector<Variant>> decodeContent(const uint8_t* data, size_t size, size_t count) {
    Cursor c(data, size);
    RBXL_TRY(sourceTypes, readInt32Array(c, count));

    RBXL_TRY(uriCountBytes, c.take(4));
    const uint32_t uriCount = bit::readU32LE(uriCountBytes);
    RBXL_TRY_VOID(requireCountFits(c, uriCount, 4, "Content URI count exceeds remaining data"));
    std::vector<std::string> uris;
    uris.reserve(uriCount);
    for (uint32_t i = 0; i < uriCount; ++i) {
        RBXL_TRY(lenBytes, c.take(4));
        const uint32_t len = bit::readU32LE(lenBytes);
        RBXL_TRY(bytes, c.take(len));
        uris.emplace_back(reinterpret_cast<const char*>(bytes), len);
    }

    RBXL_TRY(objectCountBytes, c.take(4));
    const uint32_t objectCount = bit::readU32LE(objectCountBytes);
    RBXL_TRY_VOID(
        requireCountFits(c, objectCount, 4, "Content object count exceeds remaining data"));
    RBXL_TRY(objects, readReferentArray(c, objectCount));

    RBXL_TRY(externalCountBytes, c.take(4));
    const uint32_t externalCount = bit::readU32LE(externalCountBytes);
    RBXL_TRY_VOID(requireCountFits(c, externalCount, 4,
                                    "Content external object count exceeds remaining data"));
    // ExternalObjectRefs cannot be meaningful once separated from their
    // origin file: decode just enough to skip past them, then discard.
    RBXL_TRY(discardedExternal, readReferentArray(c, externalCount));
    (void)discardedExternal;

    std::vector<Variant> out;
    out.reserve(count);
    size_t uriIndex = 0, objectIndex = 0;
    for (size_t i = 0; i < count; ++i) {
        Content value;
        switch (sourceTypes[i]) {
            case 0:
                value.sourceType = Content::SourceType::None;
                break;
            case 1:
                if (uriIndex >= uris.size()) {
                    return makeError(ErrorCode::Malformed, "Content URI section ran out of entries");
                }
                value.sourceType = Content::SourceType::Uri;
                value.uri = uris[uriIndex++];
                break;
            case 2: {
                if (objectIndex >= objects.size()) {
                    return makeError(ErrorCode::Malformed,
                                      "Content object section ran out of entries");
                }
                value.sourceType = Content::SourceType::Object;
                const uint32_t raw = objects[objectIndex++];
                value.object = raw;  // kNoInstance's bit pattern already equals raw -1
                break;
            }
            default:
                return makeError(ErrorCode::Malformed, "unknown Content source type");
        }
        out.push_back(std::move(value));
    }
    return out;
}

Status encodeContent(const std::vector<Variant>& values, std::vector<uint8_t>& out) {
    std::vector<int32_t> sourceTypes(values.size());
    std::vector<std::string> uris;
    std::vector<uint32_t> objects;
    for (size_t i = 0; i < values.size(); ++i) {
        const Content* c = std::get_if<Content>(&values[i]);
        if (!c) return makeError(ErrorCode::InvalidArgument, "expected Content value");
        switch (c->sourceType) {
            case Content::SourceType::None:
                sourceTypes[i] = 0;
                break;
            case Content::SourceType::Uri:
                sourceTypes[i] = 1;
                uris.push_back(c->uri);
                break;
            case Content::SourceType::Object:
                sourceTypes[i] = 2;
                objects.push_back(c->object);
                break;
        }
    }
    writeInt32Array(sourceTypes, out);

    uint8_t lenBytes[4];
    bit::writeU32LE(lenBytes, static_cast<uint32_t>(uris.size()));
    out.insert(out.end(), lenBytes, lenBytes + 4);
    for (const auto& uri : uris) {
        bit::writeU32LE(lenBytes, static_cast<uint32_t>(uri.size()));
        out.insert(out.end(), lenBytes, lenBytes + 4);
        out.insert(out.end(), uri.begin(), uri.end());
    }

    bit::writeU32LE(lenBytes, static_cast<uint32_t>(objects.size()));
    out.insert(out.end(), lenBytes, lenBytes + 4);
    writeReferentArray(objects, out);

    // ExternalObjectRefs cannot be meaningful across files: always empty.
    bit::writeU32LE(lenBytes, 0);
    out.insert(out.end(), lenBytes, lenBytes + 4);
    return Status();
}

}  // namespace

namespace detail {

Result<std::vector<Variant>> decodeStructValueArray(TypeId type, const uint8_t* data, size_t size,
                                                      size_t count, const CodecContext&) {
    switch (type) {
        case TypeId::UDim: return decodeUDim(data, size, count);
        case TypeId::UDim2: return decodeUDim2(data, size, count);
        case TypeId::Ray: return decodeRay(data, size, count);
        case TypeId::Color3: return decodeColor3(data, size, count);
        case TypeId::Vector2: return decodeVector2(data, size, count);
        case TypeId::Vector3: return decodeVector3(data, size, count);
        case TypeId::CFrame: return decodeCFrame(data, size, count);
        case TypeId::NumberSequence: return decodeNumberSequence(data, size, count);
        case TypeId::ColorSequence: return decodeColorSequence(data, size, count);
        case TypeId::NumberRange: return decodeNumberRange(data, size, count);
        case TypeId::Rect: return decodeRect(data, size, count);
        case TypeId::PhysicalProperties: return decodePhysicalProperties(data, size, count);
        case TypeId::OptionalCFrame: return decodeOptionalCFrame(data, size, count);
        case TypeId::UniqueId: return decodeUniqueId(data, size, count);
        case TypeId::Font: return decodeFont(data, size, count);
        case TypeId::Content: return decodeContent(data, size, count);
        default:
            return makeError(ErrorCode::UnsupportedType,
                              "value array type not implemented by the struct codec");
    }
}

Status encodeStructValueArray(TypeId type, const std::vector<Variant>& values,
                               std::vector<uint8_t>& out, const CodecContext&) {
    switch (type) {
        case TypeId::UDim: return encodeUDim(values, out);
        case TypeId::UDim2: return encodeUDim2(values, out);
        case TypeId::Ray: return encodeRay(values, out);
        case TypeId::Color3: return encodeColor3(values, out);
        case TypeId::Vector2: return encodeVector2(values, out);
        case TypeId::Vector3: return encodeVector3(values, out);
        case TypeId::CFrame: return encodeCFrame(values, out);
        case TypeId::NumberSequence: return encodeNumberSequence(values, out);
        case TypeId::ColorSequence: return encodeColorSequence(values, out);
        case TypeId::NumberRange: return encodeNumberRange(values, out);
        case TypeId::Rect: return encodeRect(values, out);
        case TypeId::PhysicalProperties: return encodePhysicalProperties(values, out);
        case TypeId::OptionalCFrame: return encodeOptionalCFrame(values, out);
        case TypeId::UniqueId: return encodeUniqueId(values, out);
        case TypeId::Font: return encodeFont(values, out);
        case TypeId::Content: return encodeContent(values, out);
        default:
            return makeError(ErrorCode::UnsupportedType,
                              "value array type not implemented by the struct codec");
    }
}

}  // namespace detail
}  // namespace binary
}  // namespace rbxl
