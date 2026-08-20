#include "binary/decode.hpp"
#include "binary/chunk.hpp"
#include "binary/valuecodec.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

// Assembles the pieces from Tasks 4-7 into a single forward pass over a
// binary place/model's chunk stream. See the task brief's "Decoding order"
// section for the chunk-by-chunk contract this follows.
namespace rbxl {
namespace binary {

namespace {

// Everything the decoder has learned about one class id by the time its
// PROP chunks arrive: the class name (for preserved-chunk bookkeeping and
// error messages) and the instances created for it, in file order so a
// PROP chunk's positional value array lines up.
struct ClassInfo {
    std::string className;
    std::vector<InstanceId> ids;
};

Result<std::string> readString(Cursor& c) {
    RBXL_TRY(lenBytes, c.take(4));
    const uint32_t length = bit::readU32LE(lenBytes);
    RBXL_TRY(bytes, c.take(length));
    return std::string(reinterpret_cast<const char*>(bytes), length);
}

// META: u32 count, then that many (key, value) length-prefixed string pairs.
Status decodeMeta(const std::vector<uint8_t>& data, Dom& dom) {
    Cursor c(data.data(), data.size());
    RBXL_TRY(countBytes, c.take(4));
    const uint32_t count = bit::readU32LE(countBytes);
    dom.metadata().reserve(dom.metadata().size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        RBXL_TRY(key, readString(c));
        RBXL_TRY(value, readString(c));
        dom.metadata().emplace_back(std::move(key), std::move(value));
    }
    return Status();
}

// SSTR: u32 version (must be 0), u32 count, then that many records of a
// 16-byte raw key plus a length-prefixed value string.
Status decodeSstr(const std::vector<uint8_t>& data, std::vector<SharedString>& table) {
    Cursor c(data.data(), data.size());
    RBXL_TRY(versionBytes, c.take(4));
    const uint32_t version = bit::readU32LE(versionBytes);
    if (version != 0) {
        return makeError(ErrorCode::Malformed, "unsupported SSTR version", c.position());
    }
    RBXL_TRY(countBytes, c.take(4));
    const uint32_t count = bit::readU32LE(countBytes);
    table.reserve(table.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        RBXL_TRY(keyBytes, c.take(16));
        std::string key(reinterpret_cast<const char*>(keyBytes), 16);
        RBXL_TRY(value, readString(c));
        table.push_back(SharedString{std::move(key), std::move(value)});
    }
    return Status();
}

// INST: classId, className, objectFormat, instanceCount, the accumulated
// referent array, and (objectFormat == 1) one service-marker byte per
// instance. Creates the instances, records classId -> {className, ids},
// and records fileReferent -> InstanceId in `referents`.
Status decodeInst(const std::vector<uint8_t>& data, Dom& dom,
                   std::unordered_map<uint32_t, ClassInfo>& classes,
                   std::unordered_map<uint32_t, InstanceId>& referents) {
    Cursor c(data.data(), data.size());
    RBXL_TRY(classIdBytes, c.take(4));
    const uint32_t classId = bit::readU32LE(classIdBytes);
    RBXL_TRY(className, readString(c));
    RBXL_TRY(formatBytes, c.take(1));
    const uint8_t objectFormat = formatBytes[0];
    RBXL_TRY(countBytes, c.take(4));
    const uint32_t instanceCount = bit::readU32LE(countBytes);

    RBXL_TRY(rawRefs, readReferentDeltaArray(c, instanceCount));

    const uint8_t* markers = nullptr;
    if (objectFormat == 1) {
        RBXL_TRY(markerBytes, c.take(instanceCount));
        markers = markerBytes;
    }

    ClassInfo info;
    info.className = className;
    info.ids.reserve(instanceCount);
    for (uint32_t i = 0; i < instanceCount; ++i) {
        InstanceId id = dom.create(className);
        if (markers != nullptr) {
            dom.at(id).isService = markers[i] != 0;
        }
        info.ids.push_back(id);
        referents[rawRefs[i]] = id;
    }
    classes[classId] = std::move(info);
    return Status();
}

// PROP: classId, propertyName, typeId, then instanceCount values assigned
// positionally to that class's instances (in the order INST created them).
// An unrecognised typeId preserves the whole chunk instead of decoding it.
Status decodeProp(const std::vector<uint8_t>& data, Dom& dom,
                   const std::unordered_map<uint32_t, ClassInfo>& classes,
                   const CodecContext& ctx) {
    Cursor c(data.data(), data.size());
    RBXL_TRY(classIdBytes, c.take(4));
    const uint32_t classId = bit::readU32LE(classIdBytes);
    RBXL_TRY(propertyName, readString(c));
    RBXL_TRY(typeIdBytes, c.take(1));
    const uint8_t typeId = typeIdBytes[0];

    auto classIt = classes.find(classId);
    if (classIt == classes.end()) {
        return makeError(ErrorCode::Malformed, "PROP names a class id with no preceding INST chunk",
                          c.position());
    }
    const ClassInfo& info = classIt->second;

    if (!isKnownTypeId(typeId)) {
        RawChunk raw;
        std::memcpy(raw.name, "PROP", 4);
        raw.className = info.className;
        raw.data = data;   // must copy: `data` is a reference into the caller's chunk
        raw.instanceCount = info.ids.size();
        dom.unknownChunks().push_back(std::move(raw));
        return Status();
    }

    const size_t valueBytes = c.remaining();
    RBXL_TRY(values, decodeValueArray(static_cast<TypeId>(typeId), data.data() + c.position(),
                                       valueBytes, info.ids.size(), ctx));

    if (values.size() != info.ids.size()) {
        return makeError(ErrorCode::Malformed, "PROP value count does not match instance count",
                          c.position());
    }
    // Moved, not copied: with ~14 million property values in the largest
    // corpus file, copying every decoded Variant into place would double
    // the string/vector allocation traffic for no reason -- `values` is
    // discarded right after this loop.
    for (size_t i = 0; i < values.size(); ++i) {
        dom.setProperty(info.ids[i], propertyName, std::move(values[i]));
    }
    return Status();
}

// Disjoint-set over every instance a PRNT chunk can name, used to detect a
// parent cycle in near-O(1) amortised time per entry. Each instance appears
// as a child at most once across the whole chunk, so the edges PRNT adds
// build a forest; "this edge would join two nodes already in the same
// component" is exactly the definition of a cycle here. This replaces an
// earlier approach that walked each entry's proposed ancestor chain by hand:
// correct, but unbounded, since an acyclic straight-line chain never
// short-circuits that walk and a chunk near the 512MB chunk cap can make it
// quadratic in entry count.
class DisjointSet {
public:
    explicit DisjointSet(std::size_t n) : parent_(n), rank_(n, 0) {
        for (std::size_t i = 0; i < n; ++i) {
            parent_[i] = static_cast<InstanceId>(i);
        }
    }

    InstanceId find(InstanceId x) {
        while (parent_[x] != x) {
            parent_[x] = parent_[parent_[x]];   // path halving
            x = parent_[x];
        }
        return x;
    }

    // Merges the sets containing `a` and `b`. Only ever called after the
    // caller has confirmed find(a) != find(b); a cycle-forming edge is
    // rejected before reaching here, not merged.
    void unite(InstanceId a, InstanceId b) {
        InstanceId ra = find(a);
        InstanceId rb = find(b);
        if (rank_[ra] < rank_[rb]) std::swap(ra, rb);
        parent_[rb] = ra;
        if (rank_[ra] == rank_[rb]) ++rank_[ra];
    }

private:
    std::vector<InstanceId> parent_;
    std::vector<uint8_t> rank_;
};

// PRNT: u8 version (must be 0), u32 count, then the child and parent
// referent arrays, applied to `dom` in file order. A parent referent of -1
// means root; any referent absent from `referents` is malformed.
//
// Cycle guard: `Dom::setParent` has no cycle protection of its own -- parenting
// an instance under its own descendant would silently drop the whole subtree
// from `roots_`, unreachable from any root, with no error (see Dom's
// contract). Before applying entry (child, parent), this checks whether
// `child` and `parent` are already in the same component of the forest built
// from earlier entries in this same PRNT chunk (see DisjointSet above); if
// so, adding this edge would close a cycle, and the entry is rejected as
// Malformed instead. A self-parent (`parent == child`) is caught by the same
// check with no special case: find(child) == find(child) is trivially true.
Status decodePrnt(const std::vector<uint8_t>& data, Dom& dom,
                   const std::unordered_map<uint32_t, InstanceId>& referents) {
    Cursor c(data.data(), data.size());
    RBXL_TRY(versionBytes, c.take(1));
    if (versionBytes[0] != 0) {
        return makeError(ErrorCode::Malformed, "unsupported PRNT version", c.position());
    }
    RBXL_TRY(countBytes, c.take(4));
    const uint32_t count = bit::readU32LE(countBytes);

    RBXL_TRY(childRaw, readReferentDeltaArray(c, count));
    RBXL_TRY(parentRaw, readReferentDeltaArray(c, count));

    DisjointSet forest(dom.instanceCount());

    for (uint32_t i = 0; i < count; ++i) {
        auto childIt = referents.find(childRaw[i]);
        if (childIt == referents.end()) {
            return makeError(ErrorCode::Malformed, "PRNT child referent not in referent map",
                              c.position());
        }
        InstanceId child = childIt->second;

        InstanceId parent = kNoInstance;
        if (parentRaw[i] != static_cast<uint32_t>(-1)) {
            auto parentIt = referents.find(parentRaw[i]);
            if (parentIt == referents.end()) {
                return makeError(ErrorCode::Malformed, "PRNT parent referent not in referent map",
                                  c.position());
            }
            parent = parentIt->second;

            if (forest.find(child) == forest.find(parent)) {
                return makeError(ErrorCode::Malformed, "PRNT chunk contains a parent cycle",
                                  c.position());
            }
            forest.unite(child, parent);
        }

        dom.setParent(child, parent);
    }
    return Status();
}

// Final pass: rewrite every Ref and Content-object value from the raw file
// referent Tasks 6-7 left in place to a real InstanceId, now that every INST
// chunk has been read and every referent is known. A Ref may point forward
// at an instance whose INST chunk had not yet been seen when the PROP chunk
// referencing it was decoded, so this cannot happen inline.
Status remapReferents(Dom& dom, const std::unordered_map<uint32_t, InstanceId>& referents) {
    const std::size_t total = dom.instanceCount();
    for (InstanceId id = 0; id < total; ++id) {
        Instance& inst = dom.at(id);
        for (auto& prop : inst.properties) {
            // This pass runs once, after the whole file has been read, so a
            // byte offset into the source no longer means anything here; the
            // instance id and property name identify the failing value
            // instead of a meaningless offset of 0.
            if (Ref* ref = std::get_if<Ref>(&prop.second)) {
                if (ref->target == kNoInstance) continue;
                auto it = referents.find(static_cast<uint32_t>(ref->target));
                if (it == referents.end()) {
                    return makeError(ErrorCode::Malformed,
                                      "Ref value names an unknown referent (instance " +
                                          std::to_string(id) + ", property \"" +
                                          dom.names().name(prop.first) + "\")",
                                      0);
                }
                ref->target = it->second;
            } else if (Content* content = std::get_if<Content>(&prop.second)) {
                if (content->sourceType != Content::SourceType::Object) continue;
                if (content->object == kNoInstance) continue;
                auto it = referents.find(static_cast<uint32_t>(content->object));
                if (it == referents.end()) {
                    return makeError(ErrorCode::Malformed,
                                      "Content object value names an unknown referent (instance " +
                                          std::to_string(id) + ", property \"" +
                                          dom.names().name(prop.first) + "\")",
                                      0);
                }
                content->object = it->second;
            }
        }
    }
    return Status();
}

}  // namespace

Result<Dom> decode(const uint8_t* data, size_t size) {
    RBXL_TRY(header, readFileHeader(data, size));
    size_t cursor = kFileHeaderSize;

    // The header's counts are untrusted: a corrupt or hostile file could
    // declare billions of instances in a handful of bytes. They are only
    // ever used below as a reservation hint, and a well-formed file cannot
    // hold more instances than it has bytes (each one needs at least a few
    // bytes of referent data), so clamping the hint to the file size turns
    // a would-be multi-gigabyte allocation attempt into, at worst, one
    // reservation no larger than the input itself.
    const size_t reserveHint = (std::min)(static_cast<size_t>(header.instanceCount), size);

    Dom dom;
    dom.reserve(reserveHint);

    std::vector<SharedString> sharedStrings;
    CodecContext ctx;
    ctx.sharedStrings = &sharedStrings;

    std::unordered_map<uint32_t, ClassInfo> classes;
    classes.reserve(header.classCount > 0 ? (std::min)(static_cast<size_t>(header.classCount), size)
                                           : 16);

    std::unordered_map<uint32_t, InstanceId> referents;
    referents.reserve(reserveHint);

    bool sawPrnt = false;

    while (cursor < size) {
        RBXL_TRY(chunk, readChunk(data, size, cursor));

        if (std::memcmp(chunk.name, "META", 4) == 0) {
            RBXL_TRY_VOID(decodeMeta(chunk.data, dom));
        } else if (std::memcmp(chunk.name, "SSTR", 4) == 0) {
            RBXL_TRY_VOID(decodeSstr(chunk.data, sharedStrings));
        } else if (std::memcmp(chunk.name, "INST", 4) == 0) {
            RBXL_TRY_VOID(decodeInst(chunk.data, dom, classes, referents));
        } else if (std::memcmp(chunk.name, "PROP", 4) == 0) {
            RBXL_TRY_VOID(decodeProp(chunk.data, dom, classes, ctx));
        } else if (std::memcmp(chunk.name, "PRNT", 4) == 0) {
            if (sawPrnt) {
                return makeError(ErrorCode::Malformed, "a second PRNT chunk is not allowed", cursor);
            }
            sawPrnt = true;
            RBXL_TRY_VOID(decodePrnt(chunk.data, dom, referents));
        } else if (std::memcmp(chunk.name, "END\0", 4) == 0) {
            break;
        } else {
            RawChunk raw;
            std::memcpy(raw.name, chunk.name, 4);
            raw.data = std::move(chunk.data);
            dom.unknownChunks().push_back(std::move(raw));
        }
    }

    RBXL_TRY_VOID(remapReferents(dom, referents));

    return dom;
}

}  // namespace binary
}  // namespace rbxl
