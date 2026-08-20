#pragma once
#include <rbxl/variant.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rbxl {

using NameId = uint32_t;
constexpr NameId kNoName = static_cast<NameId>(-1);

// Interns property names so each Instance stores a 4-byte id instead of a string.
class StringPool {
public:
    NameId intern(const std::string& name);
    NameId find(const std::string& name) const;   // kNoName when absent
    const std::string& name(NameId id) const;
    std::size_t size() const { return names_.size(); }
private:
    std::vector<std::string> names_;
    std::unordered_map<std::string, NameId> index_;
};

struct Instance {
    std::string className;
    bool isService = false;   // INST object format 1 / service marker

    // Sorted by NameId. Flat, so there is no per-property heap allocation.
    std::vector<std::pair<NameId, Variant>> properties;

    std::vector<InstanceId> children;
    InstanceId parent = kNoInstance;
};

// A chunk the decoder could not interpret, preserved byte-for-byte so files
// using future Roblox types survive a load/save round-trip.
struct RawChunk {
    char name[4] = {0, 0, 0, 0};
    std::string className;       // set for preserved PROP chunks; empty otherwise
    std::vector<uint8_t> data;   // decompressed payload
    // Instance count of `className` at the moment this PROP chunk was decoded
    // (0 for non-PROP raw chunks, where it is unused). The chunk's value
    // array is positional against that count, so the binary encoder re-emits
    // it only when the class still has exactly this many instances; see the
    // preserved-chunk rule in binary/encode.cpp.
    std::size_t instanceCount = 0;
};

class Dom {
public:
    // --- Instances ---------------------------------------------------------
    // Reserves capacity for `instanceCount` instances up front. Purely a
    // performance hint for a decoder that knows the file header's count in
    // advance; correctness never depends on having called it.
    void reserve(std::size_t instanceCount);
    InstanceId create(std::string className);
    Instance& at(InstanceId id);
    const Instance& at(InstanceId id) const;
    bool valid(InstanceId id) const { return id < instances_.size(); }
    std::size_t instanceCount() const { return instances_.size(); }

    const std::vector<InstanceId>& roots() const { return roots_; }

    // Detaches `child` from its current parent and attaches it to `parent`.
    // Passing kNoInstance makes it a root.
    void setParent(InstanceId child, InstanceId parent);

    // --- Properties --------------------------------------------------------
    void setProperty(InstanceId id, const std::string& name, Variant value);
    const Variant* getProperty(InstanceId id, const std::string& name) const;
    // Convenience over the "Name" property; empty string when unset.
    std::string nameOf(InstanceId id) const;

    // --- Pools and file-level data ----------------------------------------
    StringPool& names() { return names_; }
    const StringPool& names() const { return names_; }

    std::vector<std::pair<std::string, std::string>>& metadata() { return metadata_; }
    const std::vector<std::pair<std::string, std::string>>& metadata() const { return metadata_; }

    std::vector<RawChunk>& unknownChunks() { return unknownChunks_; }
    const std::vector<RawChunk>& unknownChunks() const { return unknownChunks_; }

    // --- Traversal ---------------------------------------------------------
    // Depth-first post-order over the whole forest: every descendant is visited
    // before its ancestor, siblings in order. This is the order Roblox Studio
    // writes PRNT entries in, so the binary encoder reuses it directly.
    std::vector<InstanceId> postOrder() const;

private:
    // O(1) removal from roots_ (see setParent): every instance starts life
    // as a root, so a decoder reparenting an entire file's worth of
    // instances would otherwise pay the vector's O(size) find-and-erase once
    // per instance, which is quadratic in instance count. rootIndex_ maps a
    // root's InstanceId to its slot in roots_ so removal can swap-with-last
    // and pop instead of shifting; this means roots_ does not preserve
    // insertion order after a removal, only membership.
    void removeRoot(InstanceId id);

    std::vector<Instance> instances_;
    std::vector<InstanceId> roots_;
    std::unordered_map<InstanceId, std::size_t> rootIndex_;
    StringPool names_;
    std::vector<std::pair<std::string, std::string>> metadata_;
    std::vector<RawChunk> unknownChunks_;
};

}  // namespace rbxl
