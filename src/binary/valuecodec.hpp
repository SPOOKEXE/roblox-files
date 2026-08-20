#pragma once
#include <rbxl/result.hpp>
#include <rbxl/bitutil.hpp>
#include <rbxl/types.hpp>
#include <rbxl/variant.hpp>
#include <cstdint>
#include <cstddef>
#include <vector>

// Column-oriented property array codecs. A PROP chunk holds one property for
// every instance of a class, and the values are transformed and interleaved
// as a batch rather than stored one after another. See Appendix A.1 in
// PLAN.md for the normative id/encoding table.
//
// This header carries the complete type id space (including the composite
// ids a later task implements) so both halves of the codec share one
// dispatch surface. `valuecodec_scalar.cpp` implements the scalar and
// byte-array types; ids outside that set report ErrorCode::UnsupportedType
// until the composite codec fills them in.
namespace rbxl {
namespace binary {

enum class TypeId : uint8_t {
    String = 0x01,
    Bool = 0x02,
    Int32 = 0x03,
    Float32 = 0x04,
    Float64 = 0x05,
    UDim = 0x06,
    UDim2 = 0x07,
    Ray = 0x08,
    Faces = 0x09,
    Axes = 0x0a,
    BrickColor = 0x0b,
    Color3 = 0x0c,
    Vector2 = 0x0d,
    Vector3 = 0x0e,
    CFrame = 0x10,
    Enum = 0x12,
    Referent = 0x13,
    Vector3int16 = 0x14,
    NumberSequence = 0x15,
    ColorSequence = 0x16,
    NumberRange = 0x17,
    Rect = 0x18,
    PhysicalProperties = 0x19,
    Color3uint8 = 0x1a,
    Int64 = 0x1b,
    SharedString = 0x1c,
    Bytecode = 0x1d,
    OptionalCFrame = 0x1e,
    UniqueId = 0x1f,
    Font = 0x20,
    SecurityCapabilities = 0x21,
    Content = 0x22,
};

// Everything a codec needs beyond the raw bytes. `sharedStrings` is the
// file's SSTR table, in file order, for resolving SharedString indices.
struct CodecContext {
    const std::vector<SharedString>* sharedStrings = nullptr;
};

// Bounds-checked cursor over one property array's bytes. Every read returns
// a Result rather than trusting the caller to have checked the size first;
// nothing in this codec should index a raw pointer without going through it.
class Cursor {
public:
    Cursor(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    size_t remaining() const { return size_ - pos_; }
    size_t position() const { return pos_; }

    // Returns a pointer to the next `n` bytes and advances past them, or
    // ErrorCode::Truncated if fewer than `n` bytes remain.
    Result<const uint8_t*> take(size_t n) {
        if (n > remaining()) {
            return makeError(ErrorCode::Truncated, "value array read past end of buffer", pos_);
        }
        const uint8_t* p = data_ + pos_;
        pos_ += n;
        return p;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
};

// Reads `count` values of `width` bytes each in interleaved (column-major)
// form and undoes the interleaving, returning a flat buffer of count*width
// bytes in normal row-major order. Shared by every interleaved type so the
// deinterleave step is written once.
inline Result<std::vector<uint8_t>> readInterleaved(Cursor& c, size_t count, size_t width) {
    RBXL_TRY(woven, c.take(count * width));
    std::vector<uint8_t> flat(count * width);
    bit::deinterleave(woven, flat.data(), count, width);
    return flat;
}

// Appends `flat` (count*width bytes, row-major) to `out` in interleaved
// (column-major) form.
inline void writeInterleaved(const std::vector<uint8_t>& flat, size_t count, size_t width,
                              std::vector<uint8_t>& out) {
    const size_t base = out.size();
    out.resize(base + flat.size());
    bit::interleave(flat.data(), out.data() + base, count, width);
}

// Decodes one property's array of `count` values, stored as `size` bytes of
// `data` in `type`'s on-disk encoding. `Ref` values carry the raw file
// referent in `Ref::target`, not yet mapped to an InstanceId; a later stage
// performs that mapping once every INST chunk has been read.
Result<std::vector<Variant>> decodeValueArray(TypeId type, const uint8_t* data, size_t size,
                                               size_t count, const CodecContext& ctx);

// Encodes `values` into `out` in `type`'s on-disk array form, appending to
// whatever `out` already holds.
Status encodeValueArray(TypeId type, const std::vector<Variant>& values,
                         std::vector<uint8_t>& out, const CodecContext& ctx);

// True for every type id Roblox's binary format defines, whether or not this
// codec implements it yet. Distinguishes "unknown to Roblox" (a malformed
// file) from "known but not yet handled" (ErrorCode::UnsupportedType).
bool isKnownTypeId(uint8_t id);

}  // namespace binary
}  // namespace rbxl
