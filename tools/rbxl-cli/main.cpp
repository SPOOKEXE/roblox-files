// rbxl CLI — format conversion, info, terrain export, and OBJ export.
// No external argument-parsing library; hand-rolled to keep the dependency tree
// at zero beyond the rbxl library itself.
#include <rbxl/rbxl.hpp>
#include <rbxl/blob.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void usage() {
    std::cerr
        << "rbxl — Roblox file tool (v" << rbxl::version() << ")\n\n"
        << "Usage:\n"
        << "  rbxl info <file>                          File metadata and statistics\n"
        << "  rbxl convert <in> <out> [options]          Convert between formats\n"
        << "  rbxl dump <file> [--props]                 Print the instance tree\n"
        << "  rbxl export-terrain <file> <output.obj>    Export terrain as OBJ mesh\n"
        << "  rbxl export-obj <file> <output_dir>        Export parts as OBJ files\n"
        << "\n"
        << "Convert options:\n"
        << "  --lz4              Use LZ4 compression (binary output)\n"
        << "  --zstd             Use Zstd compression (binary output, default)\n"
        << "  --none             No compression\n"
        << "  --level N          Compression level (default: 3)\n"
        << "\n"
        << "Export-obj options:\n"
        << "  --per-class        Write one .obj per class instead of one merged file\n";
}

static rbxl::Result<rbxl::Dom> loadAny(const std::string& path) {
    return rbxl::loadFile(path);
}

// Replace every occurrence of `from` in `s` with `to`.
static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// Sanitise a class name for use as a filename.
static std::string sanitiseName(const std::string& name) {
    std::string out = name;
    replaceAll(out, "/", "_");
    replaceAll(out, "\\", "_");
    replaceAll(out, ":", "_");
    replaceAll(out, "*", "_");
    replaceAll(out, "?", "_");
    replaceAll(out, "\"", "_");
    replaceAll(out, "<", "_");
    replaceAll(out, ">", "_");
    replaceAll(out, "|", "_");
    return out;
}

// Pretty-print a file size.
static std::string humanSize(std::size_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double v = static_cast<double>(bytes);
    for (const char* u : units) {
        if (v < 1024.0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f %s", v, u);
            return buf;
        }
        v /= 1024.0;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f TiB", v);
    return buf;
}

// ---------------------------------------------------------------------------
// info
// ---------------------------------------------------------------------------

static int cmdInfo(const std::vector<std::string>& args) {
    if (args.size() < 3) { usage(); return 1; }
    const std::string& path = args[2];

    auto r = loadAny(path);
    if (!r) { std::cerr << "error: " << r.error().toString() << "\n"; return 1; }
    const rbxl::Dom& dom = r.value();

    // Detect format from extension.
    std::string fmt = "unknown";
    {
        auto ext = rbxl::formatFromExtension(path);
        if (ext) fmt = (ext.value() == rbxl::Format::Binary) ? "Binary" : "XML";
    }

    std::cout << "File:      " << path << "\n"
              << "Format:    " << fmt << "\n"
              << "Instances: " << dom.instanceCount() << "\n"
              << "Roots:     " << dom.roots().size() << "\n";

    // Metadata.
    if (!dom.metadata().empty()) {
        std::cout << "Metadata:\n";
        for (auto& [k, v] : dom.metadata()) {
            std::cout << "  " << k << " = " << v << "\n";
        }
    }

    // Unknown chunks.
    if (!dom.unknownChunks().empty()) {
        std::cout << "Unknown chunks: " << dom.unknownChunks().size() << "\n";
    }

    // Class histogram.
    std::map<std::string, std::size_t> classCounts;
    for (rbxl::InstanceId id = 0; id < dom.instanceCount(); ++id) {
        ++classCounts[dom.at(id).className];
    }

    // Top classes by count.
    std::vector<std::pair<std::string, std::size_t>> sorted(classCounts.begin(), classCounts.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto& a, auto& b) { return a.second > b.second; });

    std::cout << "\nClasses (" << classCounts.size() << " unique):\n";
    std::size_t shown = 0;
    for (auto& [cls, count] : sorted) {
        if (shown >= 30) {
            std::cout << "  ... and " << (sorted.size() - shown) << " more\n";
            break;
        }
        std::cout << "  " << std::setw(8) << count << "  " << cls << "\n";
        ++shown;
    }

    // Property type histogram.
    std::map<rbxl::VariantType, std::size_t> typeCounts;
    for (rbxl::InstanceId id = 0; id < dom.instanceCount(); ++id) {
        for (auto& [nameId, val] : dom.at(id).properties) {
            ++typeCounts[rbxl::variantTypeOf(val)];
        }
    }

    if (!typeCounts.empty()) {
        std::cout << "\nProperty types:\n";
        std::vector<std::pair<rbxl::VariantType, std::size_t>> types(typeCounts.begin(), typeCounts.end());
        std::sort(types.begin(), types.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
        for (auto& [vt, count] : types) {
            std::cout << "  " << std::setw(8) << count << "  " << rbxl::variantTypeName(vt) << "\n";
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// convert
// ---------------------------------------------------------------------------

static int cmdConvert(const std::vector<std::string>& args) {
    if (args.size() < 4) { usage(); return 1; }
    const std::string& inPath = args[2];
    const std::string& outPath = args[3];

    // Parse optional flags.
    rbxl::Compression compression = rbxl::Compression::Zstd;
    int level = 3;
    for (std::size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--lz4") compression = rbxl::Compression::Lz4;
        else if (args[i] == "--zstd") compression = rbxl::Compression::Zstd;
        else if (args[i] == "--none") compression = rbxl::Compression::None;
        else if (args[i] == "--level" && i + 1 < args.size()) level = std::stoi(args[++i]);
        else { std::cerr << "error: unknown option " << args[i] << "\n"; return 1; }
    }

    auto r = loadAny(inPath);
    if (!r) { std::cerr << "error: " << r.error().toString() << "\n"; return 1; }

    rbxl::SaveOptions opts;
    opts.compression = compression;
    opts.level = level;

    rbxl::Diagnostics diag;
    auto status = rbxl::saveFile(r.value(), outPath, opts, &diag);
    if (!status) { std::cerr << "error: " << status.error().toString() << "\n"; return 1; }

    for (auto& w : diag.warnings) std::cerr << "warning: " << w << "\n";

    std::cout << "converted " << inPath << " -> " << outPath << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// dump
// ---------------------------------------------------------------------------

static void dumpInstance(const rbxl::Dom& dom, rbxl::InstanceId id, int depth,
                         bool showProps, std::ostream& out) {
    const auto& inst = dom.at(id);
    out << std::string(depth * 2, ' ');
    out << inst.className;
    std::string name = dom.nameOf(id);
    if (!name.empty()) out << " \"" << name << "\"";
    out << " [" << id << "]";
    if (inst.isService) out << " (service)";
    out << "\n";

    if (showProps) {
        for (auto& [nameId, val] : inst.properties) {
            const std::string& propName = dom.names().name(nameId);
            // Skip Name — already shown.
            if (propName == "Name") continue;
            out << std::string((depth + 1) * 2, ' ') << propName << ": "
                << rbxl::variantTypeName(rbxl::variantTypeOf(val)) << "\n";
        }
    }

    for (rbxl::InstanceId child : inst.children) {
        dumpInstance(dom, child, depth + 1, showProps, out);
    }
}

static int cmdDump(const std::vector<std::string>& args) {
    if (args.size() < 3) { usage(); return 1; }
    const std::string& path = args[2];

    bool showProps = false;
    for (std::size_t i = 3; i < args.size(); ++i) {
        if (args[i] == "--props") showProps = true;
        else { std::cerr << "error: unknown option " << args[i] << "\n"; return 1; }
    }

    auto r = loadAny(path);
    if (!r) { std::cerr << "error: " << r.error().toString() << "\n"; return 1; }
    const rbxl::Dom& dom = r.value();

    for (rbxl::InstanceId root : dom.roots()) {
        dumpInstance(dom, root, 0, showProps, std::cout);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// OBJ mesh helpers
// ---------------------------------------------------------------------------

static float vec3Component(const rbxl::Vector3& v, int idx) {
    if (idx == 0) return v.x;
    if (idx == 1) return v.y;
    return v.z;
}

// A triangle in OBJ space.
struct ObjTri {
    float v[3][3];   // three vertices, each x/y/z
    float n[3];      // face normal
};

// Build a box (axis-aligned) with a CFrame transform.
// Vertices are in local space: [-sx/2, -sy/2, -sz/2] to [+sx/2, +sy/2, +sz/2].
static std::vector<ObjTri> makeBox(const rbxl::CFrame& cf, float sx, float sy, float sz) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;

    // 8 corners in local space.
    float local[8][3] = {
        {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
        {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz},
    };

    // Transform by CFrame rotation + position.
    float world[8][3];
    for (int i = 0; i < 8; ++i) {
        for (int row = 0; row < 3; ++row) {
            world[i][row] = vec3Component(cf.position, row);
            for (int col = 0; col < 3; ++col) {
                world[i][row] += cf.rotation[row * 3 + col] * local[i][col];
            }
        }
    }

    // 6 faces, 2 triangles each, outward normals.
    struct Face { int idx[4]; float nx, ny, nz; };
    Face faces[6] = {
        {{0,3,2,1},  0, 0,-1},  // -Z
        {{4,5,6,7},  0, 0, 1},  // +Z
        {{0,1,5,4},  0,-1, 0},  // -Y
        {{2,3,7,6},  0, 1, 0},  // +Y
        {{0,4,7,3}, -1, 0, 0},  // -X
        {{1,2,6,5},  1, 0, 0},  // +X
    };

    std::vector<ObjTri> tris;
    tris.reserve(12);
    for (auto& f : faces) {
        // Transform the normal by the rotation matrix.
        float rn[3] = {0, 0, 0};
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                rn[row] += cf.rotation[row * 3 + col] *
                    ((col == 0 ? f.nx : col == 1 ? f.ny : f.nz));
            }
        }

        ObjTri t1, t2;
        for (int k = 0; k < 3; ++k) {
            t1.v[0][k] = world[f.idx[0]][k];
            t1.v[1][k] = world[f.idx[1]][k];
            t1.v[2][k] = world[f.idx[2]][k];
            t2.v[0][k] = world[f.idx[0]][k];
            t2.v[1][k] = world[f.idx[2]][k];
            t2.v[2][k] = world[f.idx[3]][k];
        }
        t1.n[0] = rn[0]; t1.n[1] = rn[1]; t1.n[2] = rn[2];
        t2.n[0] = rn[0]; t2.n[1] = rn[1]; t2.n[2] = rn[2];
        tris.push_back(t1);
        tris.push_back(t2);
    }
    return tris;
}

// Build a WedgePart mesh: a box with the top-front edge sliced off.
// WedgePart in Roblox: flat bottom, vertical back, angled slope from back-top to front-bottom.
// Local axes: -X/+X width, -Y/+Y height (bottom/top), -Z (front) to +Z (back).
static std::vector<ObjTri> makeWedge(const rbxl::CFrame& cf, float sx, float sy, float sz) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;

    // 5 unique vertices for the wedge shape.
    // Bottom rectangle, top-back edge only.
    float local[5][3] = {
        {-hx, -hy, -hz},  // 0: bottom-front-left
        { hx, -hy, -hz},  // 1: bottom-front-right
        { hx, -hy,  hz},  // 2: bottom-back-right
        {-hx, -hy,  hz},  // 3: bottom-back-left
        {-hx,  hy,  hz},  // 4: top-back-left
        // implicit: top-back-right = (-x mirrored of 4)
    };
    // We need the 6th vertex for the top-back-right.
    float local6[3] = {hx, hy, hz}; // 5: top-back-right

    // Actually, WedgePart has 6 vertices.
    // Let me redo with proper geometry.
    // Bottom: z = -sz/2 (front), top: z = +sz/2 (back)
    // Height: y = -sy/2 (bottom) to y = +sy/2 (top)
    // Width: x = -sx/2 to x = +sx/2
    // Shape: bottom is full rectangle, top is only the back edge.
    float v[6][3] = {
        {-hx, -hy, -hz},  // 0: bottom-front-left
        { hx, -hy, -hz},  // 1: bottom-front-right
        { hx, -hy,  hz},  // 2: bottom-back-right
        {-hx, -hy,  hz},  // 3: bottom-back-left
        { hx,  hy,  hz},  // 4: top-back-right
        {-hx,  hy,  hz},  // 5: top-back-left
    };

    float world[6][3];
    for (int i = 0; i < 6; ++i) {
        for (int row = 0; row < 3; ++row) {
            world[i][row] = vec3Component(cf.position, row);
            for (int col = 0; col < 3; ++col) {
                world[i][row] += cf.rotation[row * 3 + col] * v[i][col];
            }
        }
    }

    // 5 faces: bottom, back, left, right, slope (front face is open — no triangle there)
    // Actually it's a closed solid: bottom, back, left, right, top-back-strip, slope.
    // Triangulation:
    // Bottom:  0,1,2  and  0,2,3         normal (0,-1,0)
    // Back:    3,2,4  and  3,4,5         normal (0,0,1)
    // Left:    0,3,5  and  0,5,?         left wall from front-bottom to back-top
    // Right:   1,4,2  and  1,?,4         right wall
    // Slope:   0,1,4  and  0,4,5         the angled face

    auto transformNormal = [&](float nx, float ny, float nz) -> std::array<float,3> {
        float r[3] = {0, 0, 0};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                r[row] += cf.rotation[row * 3 + col] * (col == 0 ? nx : col == 1 ? ny : nz);
        return {r[0], r[1], r[2]};
    };

    auto emitTri = [&](int a, int b, int c, float nx, float ny, float nz) -> ObjTri {
        ObjTri t;
        for (int k = 0; k < 3; ++k) {
            t.v[0][k] = world[a][k];
            t.v[1][k] = world[b][k];
            t.v[2][k] = world[c][k];
        }
        auto n = transformNormal(nx, ny, nz);
        t.n[0] = n[0]; t.n[1] = n[1]; t.n[2] = n[2];
        return t;
    };

    std::vector<ObjTri> tris;
    tris.reserve(10);
    // Bottom
    tris.push_back(emitTri(0, 1, 2,  0, -1, 0));
    tris.push_back(emitTri(0, 2, 3,  0, -1, 0));
    // Back
    tris.push_back(emitTri(3, 2, 4,  0, 0, 1));
    tris.push_back(emitTri(3, 4, 5,  0, 0, 1));
    // Left
    tris.push_back(emitTri(0, 3, 5, -1, 0, 0));
    // Right
    tris.push_back(emitTri(1, 4, 2,  1, 0, 0));
    // Slope (front-top to back-bottom diagonal)
    tris.push_back(emitTri(0, 1, 4,  0, 0, -1)); // approximate
    tris.push_back(emitTri(0, 4, 5,  0, 0, -1));

    return tris;
}

// CornerWedgePart: like a wedge but with one corner cut.
// For simplicity, approximate as a box (user can refine later).
static std::vector<ObjTri> makeCornerWedge(const rbxl::CFrame& cf, float sx, float sy, float sz) {
    // CornerWedgePart has 5 faces forming a shape like a corner removed from a cube.
    // For a first pass, approximate as a box.
    return makeBox(cf, sx, sy, sz);
}

// TrussPart: a diagonal strut. Approximate as a thin box along the diagonal.
static std::vector<ObjTri> makeTruss(const rbxl::CFrame& cf, float sx, float sy, float sz) {
    // Truss is typically used as a structural element; approximate as a thin column.
    return makeBox(cf, sx, sy, sz);
}

static bool isBasePart(const std::string& className) {
    static const std::set<std::string> kParts = {
        "Part", "WedgePart", "CornerWedgePart", "TrussPart",
        "SpawnLocation", "MeshPart", "UnionOperation", "NegateOperation",
        "IntersectOperation", "Terrain", "BasePart",
    };
    return kParts.count(className) > 0;
}

static std::vector<ObjTri> partToMesh(const rbxl::Dom& dom, rbxl::InstanceId id) {
    const auto& inst = dom.at(id);

    rbxl::CFrame cf;
    rbxl::Vector3 size{4, 1, 2}; // default Part size

    const rbxl::Variant* cfVal = dom.getProperty(id, "CFrame");
    if (cfVal && std::holds_alternative<rbxl::CFrame>(*cfVal)) cf = std::get<rbxl::CFrame>(*cfVal);

    const rbxl::Variant* szVal = dom.getProperty(id, "Size");
    if (szVal && std::holds_alternative<rbxl::Vector3>(*szVal)) size = std::get<rbxl::Vector3>(*szVal);

    if (inst.className == "WedgePart") return makeWedge(cf, size.x, size.y, size.z);
    if (inst.className == "CornerWedgePart") return makeCornerWedge(cf, size.x, size.y, size.z);
    if (inst.className == "TrussPart") return makeTruss(cf, size.x, size.y, size.z);
    return makeBox(cf, size.x, size.y, size.z);
}

static void writeObjHeader(std::ostream& out, const std::string& comment) {
    out << "# " << comment << "\n"
        << "# Generated by rbxl v" << rbxl::version() << "\n\n";
}

// Write triangles to an OBJ stream.
static void writeObjTriangles(std::ostream& out, const std::vector<ObjTri>& tris,
                              int& vertexOffset) {
    for (auto& tri : tris) {
        // Vertices
        for (int v = 0; v < 3; ++v) {
            out << "v " << tri.v[v][0] << " " << tri.v[v][1] << " " << tri.v[v][2] << "\n";
        }
        // Normal
        out << "vn " << tri.n[0] << " " << tri.n[1] << " " << tri.n[2] << "\n";
        // Face (1-indexed)
        int base = vertexOffset + 1;
        out << "f " << base << "//" << base << " "
            << (base + 1) << "//" << base << " "
            << (base + 2) << "//" << base << "\n";
        vertexOffset += 3;
    }
}

// Collect all BasePart instances recursively.
static void collectParts(const rbxl::Dom& dom, rbxl::InstanceId id,
                         std::vector<rbxl::InstanceId>& out) {
    const auto& inst = dom.at(id);
    if (isBasePart(inst.className) && inst.className != "Terrain") {
        out.push_back(id);
    }
    for (rbxl::InstanceId child : inst.children) {
        collectParts(dom, child, out);
    }
}

// ---------------------------------------------------------------------------
// export-terrain
// ---------------------------------------------------------------------------

static int cmdExportTerrain(const std::vector<std::string>& args) {
    if (args.size() < 4) { usage(); return 1; }
    const std::string& inPath = args[2];
    const std::string& outPath = args[3];

    auto r = loadAny(inPath);
    if (!r) { std::cerr << "error: " << r.error().toString() << "\n"; return 1; }
    const rbxl::Dom& dom = r.value();

    // Find Terrain instances.
    std::vector<rbxl::InstanceId> terrains;
    for (rbxl::InstanceId id = 0; id < dom.instanceCount(); ++id) {
        if (dom.at(id).className == "Terrain") terrains.push_back(id);
    }

    if (terrains.empty()) {
        std::cerr << "error: no Terrain instance found in file\n";
        return 1;
    }

    // Terrain voxel data is stored in a BinaryString property.
    // The property name varies by format but is typically "RegionRegionDataV1"
    // or similar. For a practical export, we parse whatever BinaryString properties
    // exist and attempt to extract occupancy data.
    //
    // Roblox terrain uses a 4x4x4 voxel grid per "region cell". The binary
    // format (when present) encodes:
    //   u32 version
    //   u32 regionX, regionY, regionZ (grid dimensions in 4-voxel cells)
    //   u8[] occupancy (4*4*4 = 64 bytes per cell, 0 = empty, 1-255 = filled)
    //   u8[] material (one material id per voxel, 64 bytes per cell)

    std::vector<ObjTri> terrainTris;
    const float voxelSize = 4.0f; // each terrain voxel is 4 studs

    for (rbxl::InstanceId tid : terrains) {
        // Try to find any BinaryString property that might be terrain data.
        const auto& inst = dom.at(tid);
        for (auto& [nameId, val] : inst.properties) {
            if (rbxl::variantTypeOf(val) != rbxl::VariantType::BinaryString) continue;
            const auto& data = std::get<rbxl::BinaryString>(val).data;
            if (data.size() < 16) continue;

            // Attempt to parse as terrain region data.
            // Read version and dimensions.
            uint32_t version = static_cast<uint32_t>(data[0]) |
                               (static_cast<uint32_t>(data[1]) << 8) |
                               (static_cast<uint32_t>(data[2]) << 16) |
                               (static_cast<uint32_t>(data[3]) << 24);
            if (version > 10) continue; // not terrain data

            uint32_t rx = static_cast<uint32_t>(data[4]) |
                          (static_cast<uint32_t>(data[5]) << 8) |
                          (static_cast<uint32_t>(data[6]) << 16) |
                          (static_cast<uint32_t>(data[7]) << 24);
            uint32_t ry = static_cast<uint32_t>(data[8]) |
                          (static_cast<uint32_t>(data[9]) << 8) |
                          (static_cast<uint32_t>(data[10]) << 16) |
                          (static_cast<uint32_t>(data[11]) << 24);
            uint32_t rz = static_cast<uint32_t>(data[12]) |
                          (static_cast<uint32_t>(data[13]) << 8) |
                          (static_cast<uint32_t>(data[14]) << 16) |
                          (static_cast<uint32_t>(data[15]) << 24);

            if (rx == 0 || ry == 0 || rz == 0) continue;
            std::size_t cellCount = static_cast<std::size_t>(rx) * ry * rz;
            std::size_t expectedSize = 16 + cellCount * 64 * 2; // occupancy + material
            if (data.size() < expectedSize) {
                // Try just occupancy (no material).
                expectedSize = 16 + cellCount * 64;
                if (data.size() < expectedSize) continue;
            }

            std::cout << "terrain: " << rx << "x" << ry << "x" << rz
                      << " cells (" << cellCount << " cells, "
                      << (rx * 4) << "x" << (ry * 4) << "x" << (rz * 4) << " voxels)\n";

            // Extract occupied voxels and generate cubes.
            std::size_t offset = 16;
            for (uint32_t cz = 0; cz < rz; ++cz) {
                for (uint32_t cy = 0; cy < ry; ++cy) {
                    for (uint32_t cx = 0; cx < rx; ++cx) {
                        // 4x4x4 occupancy sub-grid.
                        for (int vz = 0; vz < 4; ++vz) {
                            for (int vy = 0; vy < 4; ++vy) {
                                for (int vx = 0; vx < 4; ++vx) {
                                    std::size_t idx = offset +
                                        (static_cast<std::size_t>(cz) * ry * rx + cy * rx + cx) * 64 +
                                        (vz * 16 + vy * 4 + vx);
                                    if (idx >= data.size()) break;
                                    uint8_t occ = data[idx];
                                    if (occ == 0) continue;

                                    // World position of this voxel.
                                    float wx = static_cast<float>(cx * 4 + vx) * voxelSize;
                                    float wy = static_cast<float>(cy * 4 + vy) * voxelSize;
                                    float wz = static_cast<float>(cz * 4 + vz) * voxelSize;

                                    rbxl::CFrame vcf;
                                    vcf.position = {wx, wy, wz};
                                    auto box = makeBox(vcf, voxelSize, voxelSize, voxelSize);
                                    terrainTris.insert(terrainTris.end(), box.begin(), box.end());
                                }
                            }
                        }
                    }
                }
            }

            std::cout << "exported " << terrainTris.size() / 12 << " terrain voxels\n";
            break; // use the first matching property
        }
    }

    if (terrainTris.empty()) {
        std::cerr << "error: could not parse terrain voxel data from any BinaryString property\n"
                  << "note: this terrain may use an unsupported encoding. Only the standard\n"
                  << "      RegionRegionDataV1 format with raw occupancy grids is supported.\n";
        return 1;
    }

    std::ofstream out(outPath);
    if (!out) { std::cerr << "error: cannot open " << outPath << " for writing\n"; return 1; }

    writeObjHeader(out, "Terrain from " + inPath);
    int vertexOffset = 0;
    writeObjTriangles(out, terrainTris, vertexOffset);

    std::cout << "wrote " << outPath << " (" << terrainTris.size() / 2
              << " triangles, " << vertexOffset / 3 << " vertices)\n";
    return 0;
}

// ---------------------------------------------------------------------------
// export-obj
// ---------------------------------------------------------------------------

static int cmdExportObj(const std::vector<std::string>& args) {
    if (args.size() < 4) { usage(); return 1; }
    const std::string& inPath = args[2];
    const std::string& outDir = args[3];

    bool perClass = false;
    for (std::size_t i = 4; i < args.size(); ++i) {
        if (args[i] == "--per-class") perClass = true;
        else { std::cerr << "error: unknown option " << args[i] << "\n"; return 1; }
    }

    auto r = loadAny(inPath);
    if (!r) { std::cerr << "error: " << r.error().toString() << "\n"; return 1; }
    const rbxl::Dom& dom = r.value();

    // Collect parts.
    std::vector<rbxl::InstanceId> parts;
    for (rbxl::InstanceId root : dom.roots()) {
        collectParts(dom, root, parts);
    }

    if (parts.empty()) {
        std::cerr << "warning: no BasePart instances found in file\n";
        return 0;
    }

    std::cout << "found " << parts.size() << " parts\n";

    // Ensure output directory exists.
    {
        std::string mkdirCmd = "mkdir -p \"" + outDir + "\"";
        int rc = std::system(mkdirCmd.c_str());
        if (rc != 0) { std::cerr << "error: failed to create directory " << outDir << "\n"; return 1; }
    }

    if (perClass) {
        // Group by class name.
        std::map<std::string, std::vector<rbxl::InstanceId>> byClass;
        for (rbxl::InstanceId id : parts) {
            byClass[dom.at(id).className].push_back(id);
        }

        for (auto& [cls, ids] : byClass) {
            std::string filename = outDir + "/" + sanitiseName(cls) + ".obj";
            std::ofstream out(filename);
            if (!out) { std::cerr << "error: cannot open " << filename << "\n"; continue; }

            writeObjHeader(out, "Class: " + cls + " from " + inPath);
            int vertexOffset = 0;
            for (rbxl::InstanceId id : ids) {
                auto tris = partToMesh(dom, id);
                writeObjTriangles(out, tris, vertexOffset);
            }
            std::cout << "  " << filename << " (" << ids.size() << " parts)\n";
        }
    } else {
        // Single merged file.
        std::string filename = outDir + "/" + sanitiseName(
            dom.roots().empty() ? "model" : dom.nameOf(dom.roots()[0])) + ".obj";
        std::ofstream out(filename);
        if (!out) { std::cerr << "error: cannot open " << filename << "\n"; return 1; }

        writeObjHeader(out, "All parts from " + inPath);
        int vertexOffset = 0;
        for (rbxl::InstanceId id : parts) {
            auto tris = partToMesh(dom, id);
            writeObjTriangles(out, tris, vertexOffset);
        }
        std::cout << "wrote " << filename << " (" << parts.size() << " parts, "
                  << vertexOffset / 3 << " vertices)\n";
    }

    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);

    if (args.size() < 2) { usage(); return 1; }

    const std::string& cmd = args[1];
    if (cmd == "info")           return cmdInfo(args);
    if (cmd == "convert")        return cmdConvert(args);
    if (cmd == "dump")           return cmdDump(args);
    if (cmd == "export-terrain") return cmdExportTerrain(args);
    if (cmd == "export-obj")     return cmdExportObj(args);

    if (cmd == "--help" || cmd == "-h" || cmd == "help") { usage(); return 0; }

    std::cerr << "error: unknown command '" << cmd << "'\n\n";
    usage();
    return 1;
}
