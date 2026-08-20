#include "binary/encode.hpp"
#include "binary/chunk.hpp"
#include "binary/valuecodec.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// The inverse of decode.cpp: assembles a `Dom` back into a binary
// place/model's chunk stream. See the task brief's "Encoding order" section
// for the chunk-by-chunk contract this follows, and the two reconciliation
// rules that keep a hand-built (rather than merely round-tripped) `Dom`
// writable.
namespace rbxl {
namespace binary {

namespace {

// Everything the encoder knows about one (className, isService) group by
// the time INST/PROP chunks are written: the class id assigned to it (in
// order of first appearance, for stable output) and the instances in it, in
// Dom pool order. A pool index is dense, unique, and exactly the file
// referent the decoder expects, so it doubles as this group's referent
// array with no separate assignment pass.
struct ClassGroup {
    uint32_t classId = 0;
    std::string className;
    bool isService = false;
    std::vector<InstanceId> ids;
};

// One PROP chunk's worth of already-reconciled values, aligned 1:1 with its
// class's `ids`. Built before any bytes are written so the SSTR table (which
// must precede every PROP chunk) can be collected from the final, already
// defaulted/coerced values rather than the raw per-instance data.
struct PropPlan {
    uint32_t classId = 0;
    std::string name;
    TypeId wireType = TypeId::String;
    std::vector<Variant> values;
};

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t buf[4];
    bit::writeU32LE(buf, value);
    out.insert(out.end(), buf, buf + 4);
}

void putString(std::vector<uint8_t>& out, const std::string& s) {
    putU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

// Binary search for `nameId` in an instance's sorted property list, mirroring
// Dom::getProperty but skipping the string->NameId round trip since callers
// here already have the id.
const Variant* findProperty(const Instance& inst, NameId nameId) {
    auto it = std::lower_bound(inst.properties.begin(), inst.properties.end(), nameId,
        [](const std::pair<NameId, Variant>& entry, NameId key) { return entry.first < key; });
    if (it != inst.properties.end() && it->first == nameId) {
        return &it->second;
    }
    return nullptr;
}

// The wire type a given Variant alternative is written as. Returns false for
// alternatives with no binary representation: Nil (an absent property, dealt
// with separately by the reconciliation rule), and the XML-only types
// (Vector2int16, Region3, Region3int16, EnumItem, ContentId, BinaryString,
// ProtectedString) that this binary codec never decodes into a Dom in the
// first place. NetAssetRef shares SharedString's wire format (see
// types.hpp), so it maps to TypeId::SharedString; convertToWireForm below
// performs the corresponding value conversion.
bool wireTypeFor(VariantType type, TypeId& out) {
    switch (type) {
        case VariantType::String: out = TypeId::String; return true;
        case VariantType::Bool: out = TypeId::Bool; return true;
        case VariantType::Int32: out = TypeId::Int32; return true;
        case VariantType::Int64: out = TypeId::Int64; return true;
        case VariantType::Float32: out = TypeId::Float32; return true;
        case VariantType::Float64: out = TypeId::Float64; return true;
        case VariantType::UDim: out = TypeId::UDim; return true;
        case VariantType::UDim2: out = TypeId::UDim2; return true;
        case VariantType::Ray: out = TypeId::Ray; return true;
        case VariantType::Faces: out = TypeId::Faces; return true;
        case VariantType::Axes: out = TypeId::Axes; return true;
        case VariantType::BrickColor: out = TypeId::BrickColor; return true;
        case VariantType::Color3: out = TypeId::Color3; return true;
        case VariantType::Color3uint8: out = TypeId::Color3uint8; return true;
        case VariantType::Vector2: out = TypeId::Vector2; return true;
        case VariantType::Vector3: out = TypeId::Vector3; return true;
        case VariantType::CFrame: out = TypeId::CFrame; return true;
        case VariantType::OptionalCFrame: out = TypeId::OptionalCFrame; return true;
        case VariantType::EnumValue: out = TypeId::Enum; return true;
        case VariantType::Ref: out = TypeId::Referent; return true;
        case VariantType::Vector3int16: out = TypeId::Vector3int16; return true;
        case VariantType::NumberSequence: out = TypeId::NumberSequence; return true;
        case VariantType::ColorSequence: out = TypeId::ColorSequence; return true;
        case VariantType::NumberRange: out = TypeId::NumberRange; return true;
        case VariantType::Rect: out = TypeId::Rect; return true;
        case VariantType::PhysicalProperties: out = TypeId::PhysicalProperties; return true;
        case VariantType::SharedString: out = TypeId::SharedString; return true;
        case VariantType::NetAssetRef: out = TypeId::SharedString; return true;
        case VariantType::Bytecode: out = TypeId::Bytecode; return true;
        case VariantType::UniqueId: out = TypeId::UniqueId; return true;
        case VariantType::Font: out = TypeId::Font; return true;
        case VariantType::SecurityCapabilities: out = TypeId::SecurityCapabilities; return true;
        case VariantType::Content: out = TypeId::Content; return true;
        default: return false;
    }
}

// A zero-initialised value for `type`, used to fill in an instance missing a
// property its classmates define. `reflection` is threaded through for
// Task 14 to consult the spec-defined default instead; it is always null
// until that task lands (and the pointer is otherwise unused here, since
// ReflectionDatabase is only forward-declared), so this always falls back to
// the zero value.
Variant defaultValueFor(VariantType type, const ReflectionDatabase* /*reflection*/,
                         const std::string& /*className*/, const std::string& /*propertyName*/) {
    switch (type) {
        case VariantType::String: return std::string();
        case VariantType::Bool: return false;
        case VariantType::Int32: return int32_t(0);
        case VariantType::Int64: return int64_t(0);
        case VariantType::Float32: return 0.0f;
        case VariantType::Float64: return 0.0;
        case VariantType::UDim: return UDim{};
        case VariantType::UDim2: return UDim2{};
        case VariantType::Ray: return Ray{};
        case VariantType::Faces: return Faces{};
        case VariantType::Axes: return Axes{};
        case VariantType::BrickColor: return BrickColor{};
        case VariantType::Color3: return Color3{};
        case VariantType::Color3uint8: return Color3uint8{};
        case VariantType::Vector2: return Vector2{};
        case VariantType::Vector3: return Vector3{};
        case VariantType::CFrame: return CFrame{};
        case VariantType::OptionalCFrame: return OptionalCFrame{};
        case VariantType::EnumValue: return EnumValue{};
        case VariantType::Ref: return Ref{};
        case VariantType::Vector3int16: return Vector3int16{};
        case VariantType::NumberSequence: return NumberSequence{};
        case VariantType::ColorSequence: return ColorSequence{};
        case VariantType::NumberRange: return NumberRange{};
        case VariantType::Rect: return Rect{};
        case VariantType::PhysicalProperties: return PhysicalProperties{};
        case VariantType::SharedString: return SharedString{};
        case VariantType::NetAssetRef: return NetAssetRef{};
        case VariantType::Bytecode: return Bytecode{};
        case VariantType::UniqueId: return UniqueId{};
        case VariantType::Font: return Font{};
        case VariantType::SecurityCapabilities: return SecurityCapabilities{};
        case VariantType::Content: return Content{};
        default: return std::monostate{};
    }
}

// The two lossless conversions the heterogeneous-property rule names: a
// narrower Int32 fits Int64 exactly, and Color3uint8's 0..255 channels map
// exactly onto Color3's 0..1 floats. Anything else is not attempted; the
// caller falls back to the type's default and records a warning.
bool coerceLossless(const Variant& src, VariantType targetType, Variant& out) {
    if (targetType == VariantType::Int64) {
        if (const int32_t* i = std::get_if<int32_t>(&src)) {
            out = static_cast<int64_t>(*i);
            return true;
        }
    } else if (targetType == VariantType::Color3) {
        if (const Color3uint8* c = std::get_if<Color3uint8>(&src)) {
            out = Color3{c->r / 255.0f, c->g / 255.0f, c->b / 255.0f};
            return true;
        }
    }
    return false;
}

// NetAssetRef and SharedString share a wire format (see types.hpp), but
// encodeValueArray's SharedString path only accepts the SharedString
// alternative. Once a property's reconciled type is NetAssetRef, every
// value in it (original, defaulted, or otherwise) is itself a NetAssetRef;
// converting here lets the rest of the pipeline treat it as plain
// SharedString from this point on.
void convertToWireForm(VariantType targetType, Variant& value) {
    if (targetType == VariantType::NetAssetRef) {
        const NetAssetRef& n = std::get<NetAssetRef>(value);
        value = SharedString{n.key, n.value};
    }
}

// Groups every instance by (className, isService) in Dom pool order, so
// class ids come out in first-appearance order and are therefore stable
// across runs for the same Dom.
std::vector<ClassGroup> groupByClass(const Dom& dom) {
    std::vector<ClassGroup> classes;
    std::unordered_map<std::string, std::size_t> indexByKey;
    const std::size_t total = dom.instanceCount();
    for (InstanceId id = 0; id < total; ++id) {
        const Instance& inst = dom.at(id);
        std::string key = inst.className;
        key.push_back(inst.isService ? '\x01' : '\x00');
        auto it = indexByKey.find(key);
        std::size_t idx;
        if (it == indexByKey.end()) {
            idx = classes.size();
            indexByKey.emplace(std::move(key), idx);
            ClassGroup group;
            group.classId = static_cast<uint32_t>(idx);
            group.className = inst.className;
            group.isService = inst.isService;
            classes.push_back(std::move(group));
        } else {
            idx = it->second;
        }
        classes[idx].ids.push_back(id);
    }
    return classes;
}

// Builds one PropPlan per (class, property name) pair: the union of
// property names across the class's instances, sorted for determinism, each
// resolved to a single type and a value per instance via the
// heterogeneous-property rule (first non-Nil value picks the type; missing
// values get the default; mismatched values are coerced when lossless, else
// defaulted with a warning).
std::vector<PropPlan> buildPropPlans(const Dom& dom, const std::vector<ClassGroup>& classes,
                                      const EncodeOptions& options, EncodeDiagnostics& diags) {
    std::vector<PropPlan> plans;
    for (const ClassGroup& group : classes) {
        std::vector<std::pair<std::string, NameId>> names;
        std::unordered_set<NameId> seen;
        for (InstanceId id : group.ids) {
            for (const auto& prop : dom.at(id).properties) {
                if (seen.insert(prop.first).second) {
                    names.emplace_back(dom.names().name(prop.first), prop.first);
                }
            }
        }
        std::sort(names.begin(), names.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [propName, nameId] : names) {
            VariantType targetType = VariantType::Nil;
            for (InstanceId id : group.ids) {
                const Variant* v = findProperty(dom.at(id), nameId);
                if (v != nullptr && variantTypeOf(*v) != VariantType::Nil) {
                    targetType = variantTypeOf(*v);
                    break;
                }
            }
            if (targetType == VariantType::Nil) {
                continue;   // every instance held Nil (or was absent); nothing to write
            }

            TypeId wireType;
            if (!wireTypeFor(targetType, wireType)) {
                diags.warnings.push_back(
                    "skipped property \"" + propName + "\" on class \"" + group.className +
                    "\": " + variantTypeName(targetType) + " has no binary representation");
                continue;
            }

            PropPlan plan;
            plan.classId = group.classId;
            plan.name = propName;
            plan.wireType = wireType;
            plan.values.reserve(group.ids.size());
            for (InstanceId id : group.ids) {
                const Variant* v = findProperty(dom.at(id), nameId);
                Variant chosen;
                if (v == nullptr || variantTypeOf(*v) == VariantType::Nil) {
                    chosen = defaultValueFor(targetType, options.reflection, group.className, propName);
                } else if (variantTypeOf(*v) == targetType) {
                    chosen = *v;
                } else {
                    Variant coerced;
                    if (coerceLossless(*v, targetType, coerced)) {
                        chosen = std::move(coerced);
                    } else {
                        chosen = defaultValueFor(targetType, options.reflection, group.className, propName);
                        diags.warnings.push_back(
                            "instance " + std::to_string(id) + " (class \"" + group.className +
                            "\") property \"" + propName + "\": expected " +
                            variantTypeName(targetType) + ", found " +
                            variantTypeName(variantTypeOf(*v)) + "; replaced with default");
                    }
                }
                convertToWireForm(targetType, chosen);
                plan.values.push_back(std::move(chosen));
            }
            plans.push_back(std::move(plan));
        }
    }
    return plans;
}

// Collects every SharedString value across the already-reconciled plans
// (which is also where a NetAssetRef-typed property's values now live, per
// convertToWireForm) into the file's SSTR table, deduplicating exactly the
// way encodeSharedString looks values up: by key and value together.
std::vector<SharedString> buildSharedStringTable(const std::vector<PropPlan>& plans) {
    std::vector<SharedString> table;
    std::unordered_map<std::string, std::size_t> index;
    for (const PropPlan& plan : plans) {
        if (plan.wireType != TypeId::SharedString) continue;
        for (const Variant& v : plan.values) {
            const SharedString& s = std::get<SharedString>(v);
            std::string composite = s.value;
            composite.push_back('\x1f');
            composite += s.key;
            if (index.find(composite) != index.end()) continue;
            index.emplace(std::move(composite), table.size());
            table.push_back(s);
        }
    }
    return table;
}

// SSTR keys are always exactly 16 raw bytes on disk (decodeSstr always reads
// 16); pad a short key with zero bytes or truncate a long one to fit.
std::string fitSharedStringKey(const std::string& key) {
    std::string fitted = key;
    fitted.resize(16, '\0');
    return fitted;
}

Status writeSstrChunk(std::vector<uint8_t>& out, const std::vector<SharedString>& table,
                       const EncodeOptions& options) {
    if (table.empty()) return Status();
    std::vector<uint8_t> payload;
    putU32(payload, 0);   // version
    putU32(payload, static_cast<uint32_t>(table.size()));
    for (const SharedString& s : table) {
        std::string key = fitSharedStringKey(s.key);
        payload.insert(payload.end(), key.begin(), key.end());
        putString(payload, s.value);
    }
    return writeChunk(out, "SSTR", payload, options.compression, options.level);
}

Status writeMetaChunk(std::vector<uint8_t>& out, const Dom& dom, const EncodeOptions& options) {
    if (dom.metadata().empty()) return Status();
    std::vector<uint8_t> payload;
    putU32(payload, static_cast<uint32_t>(dom.metadata().size()));
    for (const auto& [key, value] : dom.metadata()) {
        putString(payload, key);
        putString(payload, value);
    }
    return writeChunk(out, "META", payload, options.compression, options.level);
}

Status writeInstChunks(std::vector<uint8_t>& out, const std::vector<ClassGroup>& classes,
                        const EncodeOptions& options) {
    for (const ClassGroup& group : classes) {
        std::vector<uint8_t> payload;
        putU32(payload, group.classId);
        putString(payload, group.className);
        payload.push_back(group.isService ? 1 : 0);
        putU32(payload, static_cast<uint32_t>(group.ids.size()));

        std::vector<uint32_t> referents(group.ids.begin(), group.ids.end());
        writeReferentDeltaArray(std::move(referents), payload);

        if (group.isService) {
            payload.insert(payload.end(), group.ids.size(), uint8_t(1));
        }
        RBXL_TRY_VOID(writeChunk(out, "INST", payload, options.compression, options.level));
    }
    return Status();
}

Status writePropChunks(std::vector<uint8_t>& out, const std::vector<PropPlan>& plans,
                        const CodecContext& ctx, const EncodeOptions& options) {
    for (const PropPlan& plan : plans) {
        std::vector<uint8_t> payload;
        putU32(payload, plan.classId);
        putString(payload, plan.name);
        payload.push_back(static_cast<uint8_t>(plan.wireType));
        RBXL_TRY_VOID(encodeValueArray(plan.wireType, plan.values, payload, ctx));
        RBXL_TRY_VOID(writeChunk(out, "PROP", payload, options.compression, options.level));
    }
    return Status();
}

// The preserved-chunk rule: a RawChunk named PROP is only safe to re-emit
// when its className still names a class with exactly the instance count it
// had when the chunk was captured (see RawChunk::instanceCount), because its
// value array is positional against that count and its embedded referents
// cannot be remapped. When it is safe, its first four bytes (the class id)
// are patched to match this encode's class id assignment. Every other raw
// chunk (any other name, including genuinely unrecognised top-level chunk
// types) is re-emitted byte-for-byte.
Status writePreservedChunks(std::vector<uint8_t>& out, const Dom& dom,
                             const std::vector<ClassGroup>& classes,
                             const EncodeOptions& options, EncodeDiagnostics& diags) {
    std::unordered_map<std::string, std::size_t> classByName;
    for (std::size_t i = 0; i < classes.size(); ++i) {
        classByName.emplace(classes[i].className, i);   // first appearance wins
    }

    for (const RawChunk& raw : dom.unknownChunks()) {
        const bool isProp = std::memcmp(raw.name, "PROP", 4) == 0;
        if (!isProp) {
            RBXL_TRY_VOID(writeChunk(out, raw.name, raw.data, options.compression, options.level));
            continue;
        }

        auto it = classByName.find(raw.className);
        const bool classSurvives =
            it != classByName.end() && classes[it->second].ids.size() == raw.instanceCount;
        if (!classSurvives) {
            diags.warnings.push_back(
                "dropped preserved PROP chunk for class \"" + raw.className +
                "\": instance count no longer matches the file it was loaded from");
            continue;
        }
        if (raw.data.size() < 4) {
            diags.warnings.push_back(
                "dropped preserved PROP chunk for class \"" + raw.className +
                "\": chunk body too short to hold a class id");
            continue;
        }

        std::vector<uint8_t> patched = raw.data;
        bit::writeU32LE(patched.data(), classes[it->second].classId);
        RBXL_TRY_VOID(writeChunk(out, "PROP", patched, options.compression, options.level));
    }
    return Status();
}

Status writePrntChunk(std::vector<uint8_t>& out, const Dom& dom, const EncodeOptions& options) {
    std::vector<InstanceId> order = dom.postOrder();
    std::vector<uint8_t> payload;
    payload.push_back(0);   // version
    putU32(payload, static_cast<uint32_t>(order.size()));

    std::vector<uint32_t> childRefs(order.size());
    std::vector<uint32_t> parentRefs(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        childRefs[i] = static_cast<uint32_t>(order[i]);
        const InstanceId parent = dom.at(order[i]).parent;
        parentRefs[i] = parent == kNoInstance ? static_cast<uint32_t>(-1)
                                               : static_cast<uint32_t>(parent);
    }
    writeReferentDeltaArray(std::move(childRefs), payload);
    writeReferentDeltaArray(std::move(parentRefs), payload);
    return writeChunk(out, "PRNT", payload, options.compression, options.level);
}

Status writeEndChunk(std::vector<uint8_t>& out) {
    static const uint8_t kEndBody[9] = {'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    // Always uncompressed, regardless of EncodeOptions: the format's closing
    // marker must be readable without decompressing anything.
    return writeChunk(out, "END\0", std::vector<uint8_t>(kEndBody, kEndBody + 9),
                       Compression::None, 0);
}

}  // namespace

Result<std::vector<uint8_t>> encode(const Dom& dom, const EncodeOptions& options,
                                     EncodeDiagnostics* diagnosticsOut) {
    EncodeDiagnostics localDiags;
    EncodeDiagnostics& diags = diagnosticsOut != nullptr ? *diagnosticsOut : localDiags;

    std::vector<ClassGroup> classes = groupByClass(dom);
    std::vector<PropPlan> plans = buildPropPlans(dom, classes, options, diags);
    std::vector<SharedString> sharedStrings = buildSharedStringTable(plans);

    CodecContext ctx;
    ctx.sharedStrings = &sharedStrings;

    std::vector<uint8_t> out;
    FileHeader header;
    header.classCount = static_cast<uint32_t>(classes.size());
    header.instanceCount = static_cast<uint32_t>(dom.instanceCount());
    writeFileHeader(out, header);

    RBXL_TRY_VOID(writeSstrChunk(out, sharedStrings, options));
    RBXL_TRY_VOID(writeMetaChunk(out, dom, options));
    RBXL_TRY_VOID(writeInstChunks(out, classes, options));
    RBXL_TRY_VOID(writePropChunks(out, plans, ctx, options));
    RBXL_TRY_VOID(writePreservedChunks(out, dom, classes, options, diags));
    RBXL_TRY_VOID(writePrntChunk(out, dom, options));
    RBXL_TRY_VOID(writeEndChunk(out));

    return out;
}

}  // namespace binary
}  // namespace rbxl
