# roblox-rbxl Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A self-contained C++17 library and CLI that decodes and writes all four Roblox file formats (`.rbxl`, `.rbxlx`, `.rbxm`, `.rbxmx`) with full coverage of the data types used by current Roblox releases.

**Architecture:** Format-specific readers lower into one shared in-memory `Dom` (an instance pool plus `Variant` property values), so binary and XML share a single type model and format conversion is just `load` then `save`. The binary chunk layer is a pipeline stage rather than a persistent parallel representation; its only durable output beyond the `Dom` is a list of verbatim chunks the decoder did not recognise, which are re-emitted unchanged on write so files containing future Roblox types survive a round-trip.

**Tech Stack:** C++17, CMake 3.16+, vendored LZ4 + Zstandard + pugixml + doctest. No network access at build time. No Roblox API dump.

**Spec:** This document is self-contained. Appendix A is the normative format reference, derived from the rbx-dom specification (`https://github.com/rojo-rbx/rbx-dom/blob/master/docs/binary.md` and `docs/xml.md`, MIT) and verified byte-for-byte against the sample corpus in `temp/`.

---

## Global Constraints

- **Language:** C++17. No compiler extensions. Must build clean with `-Wall -Wextra -Wpedantic` on GCC 8+, Clang 7+, and MSVC 2019+.
- **No exceptions in the core.** Every core API returns `rbxl::Result<T>`. The library must compile and pass tests under `-fno-exceptions`. Throwing wrappers live in one file (`include/rbxl/throwing.hpp`) guarded by `#ifdef __cpp_exceptions`.
- **No RTTI dependence.** `std::variant` only, never `dynamic_cast`.
- **No network at build time.** Dependencies are committed under `third_party/`. `tools/vendor.sh` regenerates them and is run by a human, never by the build.
- **No Roblox API dump.** Property types are read from the file. `ReflectionDatabase` is an optional caller-supplied interface, and every code path must work when it is `nullptr`.
- **Endianness:** the library must be correct on big-endian hosts. Never `memcpy` a multi-byte integer and use it directly; always go through the explicit byte-order helpers in `bitutil.hpp`.
- **Naming:** namespace `rbxl`. Types `PascalCase`, functions and variables `camelCase`, constants `kPascalCase`, private members trailing underscore.
- **Test corpus:** `temp/` is gitignored and may be absent. Corpus tests must skip cleanly, not fail, when a file is missing.
- **Binary format version:** header version `0`. **XML format version:** `4`.
- **Commit style:** conventional commits (`feat:`, `test:`, `fix:`, `chore:`). One commit per task minimum. Never add AI co-author or generated-by footers.

---

## File Structure

```
CMakeLists.txt                    Top-level build. Options: RBXL_BUILD_CLI, RBXL_BUILD_TESTS.
tools/vendor.sh                   Human-run script that populates third_party/.

include/rbxl/
  result.hpp        Result<T>, Error, ErrorCode.                         Task 1
  bitutil.hpp       Byte order, zigzag, interleaving, Roblox float.      Task 2
  types.hpp         Vector3, CFrame, UDim2, Font, Content, ... structs.  Task 3
  variant.hpp       Variant + VariantType enum + type-name mapping.      Task 3
  dom.hpp           Instance, Dom, StringPool, InstanceId.               Task 4
  compression.hpp   Compression enum, shared by chunk layer and API.     Task 5
  format.hpp        Format enum, detection, SaveOptions.                 Task 12
  reflection.hpp    Optional ReflectionDatabase interface.               Task 14
  blob.hpp          Attributes / Tags / MaterialColors helpers.          Task 13
  throwing.hpp      loadOrThrow / saveOrThrow.                           Task 12
  rbxl.hpp          Umbrella header + load() / save().                   Task 12

src/binary/
  chunk.hpp/.cpp    Header parse/write, chunk framing, LZ4/Zstd codec.   Task 5
  valuecodec.hpp    Shared decl for all property array codecs.           Task 6
  valuecodec_scalar.cpp   Scalar and byte-array types.                   Task 6
  valuecodec_struct.cpp   Composite and struct types.                    Task 7
  decode.cpp        Chunk stream -> Dom.                                 Task 8
  encode.cpp        Dom -> chunk stream.                                 Task 9

src/xml/
  base64.hpp/.cpp   RFC 2045 encode/decode.                              Task 10
  decode.cpp        pugixml document -> Dom.                             Task 10
  encode.cpp        Dom -> pugixml document.                             Task 11

src/blob/attributes.cpp, tags.cpp, materialcolors.cpp                    Task 13
src/format.cpp      Format sniffing and the public load/save entry.      Task 12

tools/rbxl-cli/main.cpp   info / convert / dump / roundtrip.             Task 15

third_party/{lz4,zstd,pugixml,doctest}/                                  Task 1
tests/                                                                   per task
```

---

## Task 1: Project scaffolding, vendored dependencies, and `Result<T>`

**Files:**
- Create: `tools/vendor.sh`, `CMakeLists.txt`, `third_party/CMakeLists.txt`
- Create: `include/rbxl/result.hpp`
- Create: `tests/CMakeLists.txt`, `tests/main.cpp`, `tests/test_result.cpp`, `tests/test_deps.cpp`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing.
- Produces: CMake targets `rbxl` (static library, alias `rbxl::rbxl`), `rbxl_tests`. Headers reachable as `#include <rbxl/...>`. `rbxl::Result<T>`, `rbxl::Error`, `rbxl::ErrorCode`, macro `RBXL_TRY`.

- [ ] **Step 1: Write the vendoring script**

Create `tools/vendor.sh`. This is run once by a human; its output is committed.

```bash
#!/usr/bin/env bash
# Populates third_party/ from upstream releases. Run manually; the build never calls this.
set -euo pipefail
cd "$(dirname "$0")/.."
LZ4_VER=1.10.0; ZSTD_VER=1.5.7; PUGI_VER=1.14; DOCTEST_VER=2.4.11
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
rm -rf third_party/lz4 third_party/zstd third_party/pugixml third_party/doctest
mkdir -p third_party/{lz4,zstd,pugixml,doctest}

curl -sSL "https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VER}.tar.gz" | tar xz -C "$TMP"
cp "$TMP/lz4-${LZ4_VER}/lib/"{lz4.c,lz4.h} third_party/lz4/
cp "$TMP/lz4-${LZ4_VER}/LICENSE" third_party/lz4/

curl -sSL "https://github.com/facebook/zstd/releases/download/v${ZSTD_VER}/zstd-${ZSTD_VER}.tar.gz" | tar xz -C "$TMP"
cp -r "$TMP/zstd-${ZSTD_VER}/lib/"{common,compress,decompress} third_party/zstd/
cp "$TMP/zstd-${ZSTD_VER}/lib/"{zstd.h,zstd_errors.h} third_party/zstd/
cp "$TMP/zstd-${ZSTD_VER}/LICENSE" third_party/zstd/
# Assembly is disabled via ZSTD_DISABLE_ASM; drop the .S so no toolchain needs to assemble it.
rm -f third_party/zstd/decompress/*.S

curl -sSL "https://github.com/zeux/pugixml/releases/download/v${PUGI_VER}/pugixml-${PUGI_VER}.tar.gz" | tar xz -C "$TMP"
cp "$TMP/pugixml-${PUGI_VER}/src/"{pugixml.cpp,pugixml.hpp,pugiconfig.hpp} third_party/pugixml/
cp "$TMP/pugixml-${PUGI_VER}/LICENSE.md" third_party/pugixml/

curl -sSL -o third_party/doctest/doctest.h \
  "https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VER}/doctest/doctest.h"
curl -sSL -o third_party/doctest/LICENSE.txt \
  "https://raw.githubusercontent.com/doctest/doctest/v${DOCTEST_VER}/LICENSE.txt"
echo "vendored: lz4 ${LZ4_VER}, zstd ${ZSTD_VER}, pugixml ${PUGI_VER}, doctest ${DOCTEST_VER}"
```

- [ ] **Step 2: Run it and confirm the trees exist**

```bash
chmod +x tools/vendor.sh && ./tools/vendor.sh
ls third_party/lz4/lz4.c third_party/zstd/zstd.h third_party/pugixml/pugixml.cpp third_party/doctest/doctest.h
```
Expected: all four paths listed, no errors.

- [ ] **Step 3: Write `third_party/CMakeLists.txt`**

```cmake
# Vendored dependencies. Warnings are suppressed; we do not maintain this code.
add_library(rbxl_lz4 STATIC lz4/lz4.c)
target_include_directories(rbxl_lz4 PUBLIC lz4)

file(GLOB ZSTD_SOURCES zstd/common/*.c zstd/compress/*.c zstd/decompress/*.c)
add_library(rbxl_zstd STATIC ${ZSTD_SOURCES})
target_include_directories(rbxl_zstd PUBLIC zstd)
target_include_directories(rbxl_zstd PRIVATE zstd/common)
# ZSTD_DISABLE_ASM keeps the build toolchain-independent (no .S files vendored).
target_compile_definitions(rbxl_zstd PRIVATE ZSTD_DISABLE_ASM=1 XXH_NAMESPACE=RBXL_ZSTD_)

add_library(rbxl_pugixml STATIC pugixml/pugixml.cpp)
target_include_directories(rbxl_pugixml PUBLIC pugixml)

add_library(rbxl_doctest INTERFACE)
target_include_directories(rbxl_doctest INTERFACE doctest)

foreach(dep rbxl_lz4 rbxl_zstd rbxl_pugixml)
  set_target_properties(${dep} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(${dep} PRIVATE -w)
  endif()
endforeach()
```

- [ ] **Step 4: Write the top-level `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(roblox_rbxl VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RBXL_BUILD_CLI "Build the rbxl command line tool" ON)
option(RBXL_BUILD_TESTS "Build the test suite" ON)

add_subdirectory(third_party)

add_library(rbxl STATIC)
add_library(rbxl::rbxl ALIAS rbxl)
target_include_directories(rbxl PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(rbxl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rbxl PRIVATE rbxl_lz4 rbxl_zstd rbxl_pugixml)
if(NOT MSVC)
  target_compile_options(rbxl PRIVATE -Wall -Wextra -Wpedantic)
else()
  target_compile_options(rbxl PRIVATE /W4)
endif()
# Sources are appended by later tasks via target_sources in src/CMakeLists.txt.
add_subdirectory(src)

if(RBXL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(RBXL_BUILD_CLI)
  add_subdirectory(tools/rbxl-cli)
endif()
```

Create `src/CMakeLists.txt` containing only a placeholder source so the target links:

```cmake
target_sources(rbxl PRIVATE version.cpp)
```

And `src/version.cpp`:

```cpp
#include <rbxl/result.hpp>

namespace rbxl {
const char* version() { return "0.1.0"; }
}
```

For now, comment out the `add_subdirectory(tools/rbxl-cli)` line; Task 15 creates that directory and re-enables it.

- [ ] **Step 5: Write `include/rbxl/result.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <new>

namespace rbxl {

enum class ErrorCode {
    None = 0,
    Io,                 // could not open or read the file
    BadMagic,           // not a Roblox file
    BadVersion,         // known container, unsupported version
    Truncated,          // ran off the end of the buffer
    Malformed,          // structurally invalid content
    UnsupportedType,    // known-but-unhandled property type
    Compression,        // LZ4 or Zstd failure
    XmlParse,           // pugixml reported a parse error
    InvalidArgument,    // caller error, e.g. unknown target format
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
    std::size_t offset = 0;   // byte offset into the source, when meaningful

    Error() = default;
    Error(ErrorCode c, std::string m, std::size_t off = 0)
        : code(c), message(std::move(m)), offset(off) {}

    // Human-readable "code: message (at offset N)".
    std::string toString() const;
};

// Value-or-error. Never throws. Check with operator bool / hasValue() before value().
template <typename T>
class Result {
public:
    Result(T value) : hasValue_(true) { new (&storage_.value) T(std::move(value)); }
    Result(Error error) : hasValue_(false) { new (&storage_.error) Error(std::move(error)); }
    Result(const Result& other) : hasValue_(other.hasValue_) {
        if (hasValue_) new (&storage_.value) T(other.storage_.value);
        else new (&storage_.error) Error(other.storage_.error);
    }
    Result(Result&& other) noexcept : hasValue_(other.hasValue_) {
        if (hasValue_) new (&storage_.value) T(std::move(other.storage_.value));
        else new (&storage_.error) Error(std::move(other.storage_.error));
    }
    Result& operator=(Result other) noexcept { swapWith(other); return *this; }
    ~Result() {
        if (hasValue_) storage_.value.~T();
        else storage_.error.~Error();
    }

    bool hasValue() const { return hasValue_; }
    explicit operator bool() const { return hasValue_; }

    T& value() { return storage_.value; }
    const T& value() const { return storage_.value; }
    const Error& error() const { return storage_.error; }

    T valueOr(T fallback) const { return hasValue_ ? storage_.value : std::move(fallback); }

private:
    void swapWith(Result& other) noexcept {
        Result tmp(std::move(other));
        other.~Result();
        new (&other) Result(std::move(*this));
        this->~Result();
        new (this) Result(std::move(tmp));
    }
    union Storage {
        Storage() {}
        ~Storage() {}
        T value;
        Error error;
    } storage_;
    bool hasValue_;
};

// Specialisation for operations that produce no value.
template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}
    bool hasValue() const { return error_.code == ErrorCode::None; }
    explicit operator bool() const { return hasValue(); }
    const Error& error() const { return error_; }
private:
    Error error_;
};

using Status = Result<void>;

inline Error makeError(ErrorCode code, std::string message, std::size_t offset = 0) {
    return Error(code, std::move(message), offset);
}

}  // namespace rbxl

// Unwrap a Result into `name`, propagating the error from the enclosing function.
// Requires the enclosing function to return a Result type.
#define RBXL_TRY(name, expr)                       \
    auto name##_result_ = (expr);                  \
    if (!name##_result_) return name##_result_.error(); \
    auto& name = name##_result_.value()

// Propagate an error from an expression whose value is discarded.
#define RBXL_TRY_VOID(expr)                        \
    do {                                           \
        auto rbxl_status_ = (expr);                \
        if (!rbxl_status_) return rbxl_status_.error(); \
    } while (0)
```

Add `src/result.cpp`:

```cpp
#include <rbxl/result.hpp>

namespace rbxl {

std::string Error::toString() const {
    static const char* kNames[] = {"None", "Io", "BadMagic", "BadVersion", "Truncated",
                                   "Malformed", "UnsupportedType", "Compression",
                                   "XmlParse", "InvalidArgument"};
    std::string out = kNames[static_cast<int>(code)];
    out += ": ";
    out += message;
    if (offset != 0) out += " (at offset " + std::to_string(offset) + ")";
    return out;
}

}  // namespace rbxl
```

Register it: `src/CMakeLists.txt` becomes `target_sources(rbxl PRIVATE version.cpp result.cpp)`.

- [ ] **Step 6: Write the failing tests**

`tests/main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
```

`tests/test_result.cpp`:

```cpp
#include <doctest.h>
#include <rbxl/result.hpp>
#include <string>
#include <vector>

using namespace rbxl;

static Result<int> succeeds() { return 42; }
static Result<int> fails() { return makeError(ErrorCode::Truncated, "ran out", 7); }

TEST_CASE("Result carries a value") {
    auto r = succeeds();
    REQUIRE(r);
    CHECK(r.value() == 42);
}

TEST_CASE("Result carries an error") {
    auto r = fails();
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Truncated);
    CHECK(r.error().message == "ran out");
    CHECK(r.error().offset == 7);
    CHECK(r.error().toString() == "Truncated: ran out (at offset 7)");
}

TEST_CASE("Result manages non-trivial payloads without leaking") {
    Result<std::vector<std::string>> r{std::vector<std::string>{"a", "b"}};
    REQUIRE(r);
    CHECK(r.value().size() == 2);
    auto moved = std::move(r);
    CHECK(moved.value()[1] == "b");
}

TEST_CASE("RBXL_TRY propagates errors") {
    auto fn = []() -> Result<int> {
        RBXL_TRY(v, fails());
        return v + 1;
    };
    auto r = fn();
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Truncated);
}

TEST_CASE("Status reports success by default") {
    Status ok;
    CHECK(ok);
    Status bad{makeError(ErrorCode::Io, "nope")};
    CHECK_FALSE(bad);
}
```

`tests/test_deps.cpp` proves all three vendored libraries link and work:

```cpp
#include <doctest.h>
#include <lz4.h>
#include <zstd.h>
#include <pugixml.hpp>
#include <string>
#include <vector>

TEST_CASE("vendored lz4 round-trips a buffer") {
    std::string input(4096, 'a');
    std::vector<char> comp(static_cast<size_t>(LZ4_compressBound(static_cast<int>(input.size()))));
    int n = LZ4_compress_default(input.data(), comp.data(),
                                 static_cast<int>(input.size()), static_cast<int>(comp.size()));
    REQUIRE(n > 0);
    std::vector<char> out(input.size());
    int m = LZ4_decompress_safe(comp.data(), out.data(), n, static_cast<int>(out.size()));
    REQUIRE(m == static_cast<int>(input.size()));
    CHECK(std::string(out.begin(), out.end()) == input);
}

TEST_CASE("vendored zstd round-trips a buffer and reports its magic") {
    std::string input(4096, 'b');
    std::vector<char> comp(ZSTD_compressBound(input.size()));
    size_t n = ZSTD_compress(comp.data(), comp.size(), input.data(), input.size(), 3);
    REQUIRE_FALSE(ZSTD_isError(n));
    // The Roblox chunk reader identifies zstd by this exact 4-byte prefix.
    CHECK(static_cast<unsigned char>(comp[0]) == 0x28);
    CHECK(static_cast<unsigned char>(comp[1]) == 0xb5);
    CHECK(static_cast<unsigned char>(comp[2]) == 0x2f);
    CHECK(static_cast<unsigned char>(comp[3]) == 0xfd);
    std::vector<char> out(input.size());
    size_t m = ZSTD_decompress(out.data(), out.size(), comp.data(), n);
    REQUIRE_FALSE(ZSTD_isError(m));
    CHECK(std::string(out.begin(), out.end()) == input);
}

TEST_CASE("vendored pugixml parses a document with a leading comment") {
    // Real Roblox XML places in the wild start with a comment before <roblox>.
    const char* doc = "<!-- hi --><roblox version=\"4\"><Item class=\"Part\" referent=\"0\"/></roblox>";
    pugi::xml_document xml;
    auto result = xml.load_string(doc);
    REQUIRE(result);
    auto root = xml.child("roblox");
    REQUIRE(root);
    CHECK(std::string(root.attribute("version").value()) == "4");
    CHECK(std::string(root.child("Item").attribute("class").value()) == "Part");
}
```

`tests/CMakeLists.txt`:

```cmake
add_executable(rbxl_tests
  main.cpp
  test_result.cpp
  test_deps.cpp)
target_link_libraries(rbxl_tests PRIVATE rbxl rbxl_doctest rbxl_lz4 rbxl_zstd rbxl_pugixml)
target_include_directories(rbxl_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(rbxl_tests PRIVATE RBXL_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/temp")
add_test(NAME rbxl_tests COMMAND rbxl_tests)
```

- [ ] **Step 7: Configure, build, and run**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
./build/tests/rbxl_tests
```
Expected: all test cases pass. If `Error::toString` or `Result` misbehaves, fix before continuing; every later task depends on this.

- [ ] **Step 8: Update `.gitignore`**

```
temp/
build/
```

- [ ] **Step 9: Commit**

```bash
git add .gitignore CMakeLists.txt src tests third_party tools PLAN.md
git commit -m "chore: scaffold cmake project with vendored lz4, zstd, pugixml, doctest"
```

---

## Task 2: Byte-level primitives (`bitutil.hpp`)

This is the highest-risk-per-line code in the project. Every value in a binary file passes through it, and a mistake produces plausible-looking garbage rather than a crash. All test vectors below are taken from the format specification and independently verified.

**Files:**
- Create: `include/rbxl/bitutil.hpp`
- Create: `tests/test_bitutil.cpp`
- Modify: `tests/CMakeLists.txt` (add `test_bitutil.cpp`)

**Interfaces:**
- Consumes: nothing.
- Produces: namespace `rbxl::bit` with
  `readU16LE/readU32LE/readU64LE`, `readU16BE/readU32BE/readU64BE`,
  `writeU16LE/writeU32LE/writeU64LE`, `writeU16BE/writeU32BE/writeU64BE`
  (all `const uint8_t*` / `uint8_t*` based),
  `zigzagEncode32(int32_t)->uint32_t`, `zigzagDecode32(uint32_t)->int32_t`,
  `zigzagEncode64(int64_t)->uint64_t`, `zigzagDecode64(uint64_t)->int64_t`,
  `decodeRobloxFloat(uint32_t)->float`, `encodeRobloxFloat(float)->uint32_t`,
  `readF32LE/writeF32LE`, `readF64LE/writeF64LE`,
  `interleave(const uint8_t* src, uint8_t* dst, size_t count, size_t width)`,
  `deinterleave(const uint8_t* src, uint8_t* dst, size_t count, size_t width)`.

- [ ] **Step 1: Write the failing test**

`tests/test_bitutil.cpp`:

```cpp
#include <doctest.h>
#include <rbxl/bitutil.hpp>
#include <cstdint>
#include <vector>

using namespace rbxl::bit;

TEST_CASE("byte order helpers are explicit, not memcpy") {
    const uint8_t buf[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(readU32LE(buf) == 0x04030201u);
    CHECK(readU32BE(buf) == 0x01020304u);
    CHECK(readU16LE(buf) == 0x0201u);
    CHECK(readU16BE(buf) == 0x0102u);
    CHECK(readU64LE(buf) == 0x0807060504030201ull);
    CHECK(readU64BE(buf) == 0x0102030405060708ull);

    uint8_t out[8] = {};
    writeU32BE(out, 0x01020304u);
    CHECK(out[0] == 0x01);
    CHECK(out[3] == 0x04);
    writeU32LE(out, 0x01020304u);
    CHECK(out[0] == 0x04);
    CHECK(out[3] == 0x01);
}

TEST_CASE("zigzag transformation matches the spec") {
    // Spec: x >= 0 -> 2x, x < 0 -> 2|x| - 1.
    CHECK(zigzagEncode32(0) == 0u);
    CHECK(zigzagEncode32(2) == 4u);
    CHECK(zigzagEncode32(4) == 8u);
    CHECK(zigzagEncode32(-1) == 1u);
    CHECK(zigzagEncode32(-2) == 3u);
    CHECK(zigzagDecode32(0) == 0);
    CHECK(zigzagDecode32(4) == 2);
    CHECK(zigzagDecode32(8) == 4);
    CHECK(zigzagDecode32(1) == -1);
    CHECK(zigzagDecode32(3) == -2);

    CHECK(zigzagDecode32(zigzagEncode32(INT32_MIN)) == INT32_MIN);
    CHECK(zigzagDecode32(zigzagEncode32(INT32_MAX)) == INT32_MAX);
    CHECK(zigzagDecode64(zigzagEncode64(INT64_MIN)) == INT64_MIN);
    CHECK(zigzagDecode64(zigzagEncode64(INT64_MAX)) == INT64_MAX);
}

TEST_CASE("Roblox float format is IEEE-754 rotated left by one bit") {
    // IEEE-754 1.0f is 0x3F800000. Roblox moves the sign bit to the low end.
    CHECK(encodeRobloxFloat(1.0f) == 0x7F000000u);
    CHECK(decodeRobloxFloat(0x7F000000u) == 1.0f);
    CHECK(encodeRobloxFloat(3.0f) == 0x80800000u);
    CHECK(decodeRobloxFloat(0x80800000u) == 3.0f);
    // The sign bit lands in bit 0, so negation flips the least significant bit.
    CHECK(encodeRobloxFloat(-1.0f) == 0x7F000001u);
    CHECK(decodeRobloxFloat(0x7F000001u) == -1.0f);
    CHECK(decodeRobloxFloat(0x80000000u) == 2.0f);
    CHECK(decodeRobloxFloat(0x80000001u) == -2.0f);
    CHECK(decodeRobloxFloat(0x80800001u) == -3.0f);

    for (float v : {0.0f, -0.0f, 1.5f, -1234.5f, 3.4028235e38f, 1.17549435e-38f}) {
        CHECK(decodeRobloxFloat(encodeRobloxFloat(v)) == v);
    }
}

TEST_CASE("interleaving stores arrays column-wise") {
    // Spec: the sequence A0 A1 B0 B1 C0 C1 is stored as A0 B0 C0 A1 B1 C1.
    const uint8_t flat[6] = {0xA0, 0xA1, 0xB0, 0xB1, 0xC0, 0xC1};
    uint8_t woven[6] = {};
    interleave(flat, woven, /*count=*/3, /*width=*/2);
    const uint8_t expected[6] = {0xA0, 0xB0, 0xC0, 0xA1, 0xB1, 0xC1};
    CHECK(std::vector<uint8_t>(woven, woven + 6) == std::vector<uint8_t>(expected, expected + 6));

    uint8_t back[6] = {};
    deinterleave(woven, back, 3, 2);
    CHECK(std::vector<uint8_t>(back, back + 6) == std::vector<uint8_t>(flat, flat + 6));
}

TEST_CASE("golden vector: interleaved UDim scales decode to 1.0 and 3.0") {
    // From the spec: UDim {1, 2} and {3, 4} encode Scale as 7f 80 00 80 00 00 00 00.
    const uint8_t src[8] = {0x7f, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00};
    uint8_t flat[8] = {};
    deinterleave(src, flat, /*count=*/2, /*width=*/4);
    CHECK(decodeRobloxFloat(readU32BE(flat + 0)) == 1.0f);
    CHECK(decodeRobloxFloat(readU32BE(flat + 4)) == 3.0f);
}

TEST_CASE("golden vector: interleaved UDim offsets decode to 2 and 4") {
    // From the spec: the Offset half of the same pair is 00 00 00 00 00 00 04 08.
    const uint8_t src[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
    uint8_t flat[8] = {};
    deinterleave(src, flat, 2, 4);
    CHECK(zigzagDecode32(readU32BE(flat + 0)) == 2);
    CHECK(zigzagDecode32(readU32BE(flat + 4)) == 4);
}

TEST_CASE("golden vector: interleaved Vector3 array") {
    // From the spec: Vector3(1,2,3) and Vector3(-1,-2,-3) as three component arrays.
    auto decodeAxis = [](const uint8_t (&src)[8], float& a, float& b) {
        uint8_t flat[8] = {};
        deinterleave(src, flat, 2, 4);
        a = decodeRobloxFloat(readU32BE(flat + 0));
        b = decodeRobloxFloat(readU32BE(flat + 4));
    };
    const uint8_t xs[8] = {0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t ys[8] = {0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t zs[8] = {0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x01};
    float a = 0, b = 0;
    decodeAxis(xs, a, b); CHECK(a ==  1.0f); CHECK(b == -1.0f);
    decodeAxis(ys, a, b); CHECK(a ==  2.0f); CHECK(b == -2.0f);
    decodeAxis(zs, a, b); CHECK(a ==  3.0f); CHECK(b == -3.0f);
}

TEST_CASE("golden vector: interleaved BrickColor numbers") {
    // From the spec: 1004, 37, 1010 encode as 00 00 00 00 00 00 03 00 03 EC 25 F2.
    const uint8_t src[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x03, 0x00, 0x03, 0xEC, 0x25, 0xF2};
    uint8_t flat[12] = {};
    deinterleave(src, flat, /*count=*/3, /*width=*/4);
    CHECK(readU32BE(flat + 0) == 1004u);
    CHECK(readU32BE(flat + 4) == 37u);
    CHECK(readU32BE(flat + 8) == 1010u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -5
```
Expected: FAIL to compile with "rbxl/bitutil.hpp: No such file or directory".

- [ ] **Step 3: Write `include/rbxl/bitutil.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>

namespace rbxl {
namespace bit {

static_assert(std::numeric_limits<float>::is_iec559, "requires IEEE-754 float");
static_assert(std::numeric_limits<double>::is_iec559, "requires IEEE-754 double");

// --- Byte order ------------------------------------------------------------
// Always explicit. Never memcpy a multi-byte integer; the library must work on
// big-endian hosts.

inline uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline uint64_t readU64LE(const uint8_t* p) {
    return static_cast<uint64_t>(readU32LE(p)) |
           (static_cast<uint64_t>(readU32LE(p + 4)) << 32);
}
inline uint64_t readU64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(readU32BE(p)) << 32) |
           static_cast<uint64_t>(readU32BE(p + 4));
}

inline void writeU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8);
}
inline void writeU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8); p[1] = static_cast<uint8_t>(v);
}
inline void writeU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);       p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16); p[3] = static_cast<uint8_t>(v >> 24);
}
inline void writeU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24); p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);  p[3] = static_cast<uint8_t>(v);
}
inline void writeU64LE(uint8_t* p, uint64_t v) {
    writeU32LE(p, static_cast<uint32_t>(v));
    writeU32LE(p + 4, static_cast<uint32_t>(v >> 32));
}
inline void writeU64BE(uint8_t* p, uint64_t v) {
    writeU32BE(p, static_cast<uint32_t>(v >> 32));
    writeU32BE(p + 4, static_cast<uint32_t>(v));
}

// --- Integer transformation (zigzag) ---------------------------------------
// Maps signed values onto small unsigned ones so that runs of nearby numbers
// compress well: 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3, 2 -> 4 ...

inline uint32_t zigzagEncode32(int32_t v) {
    return (static_cast<uint32_t>(v) << 1) ^ static_cast<uint32_t>(v >> 31);
}
inline int32_t zigzagDecode32(uint32_t v) {
    return static_cast<int32_t>((v >> 1) ^ (~(v & 1u) + 1u));
}
inline uint64_t zigzagEncode64(int64_t v) {
    return (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
}
inline int64_t zigzagDecode64(uint64_t v) {
    return static_cast<int64_t>((v >> 1) ^ (~(v & 1ull) + 1ull));
}

// --- Roblox float format ---------------------------------------------------
// Roblox stores float32 as  eeeeeeee mmmmmmmm mmmmmmmm mmmmmmms  (big-endian),
// i.e. IEEE-754's leading sign bit rotated round to the least significant bit.
// Encoding is therefore a 1-bit left rotate, decoding a 1-bit right rotate.

inline float decodeRobloxFloat(uint32_t robloxBits) {
    const uint32_t ieee = (robloxBits >> 1) | (robloxBits << 31);
    float out;
    std::memcpy(&out, &ieee, sizeof(out));
    return out;
}
inline uint32_t encodeRobloxFloat(float value) {
    uint32_t ieee;
    std::memcpy(&ieee, &value, sizeof(ieee));
    return (ieee << 1) | (ieee >> 31);
}

// Plain little-endian IEEE floats, used by the types that skip the Roblox format.
inline float readF32LE(const uint8_t* p) {
    const uint32_t bits = readU32LE(p);
    float out; std::memcpy(&out, &bits, sizeof(out)); return out;
}
inline void writeF32LE(uint8_t* p, float v) {
    uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); writeU32LE(p, bits);
}
inline double readF64LE(const uint8_t* p) {
    const uint64_t bits = readU64LE(p);
    double out; std::memcpy(&out, &bits, sizeof(out)); return out;
}
inline void writeF64LE(uint8_t* p, double v) {
    uint64_t bits; std::memcpy(&bits, &v, sizeof(bits)); writeU64LE(p, bits);
}

// --- Byte interleaving -----------------------------------------------------
// Arrays are stored in "columns": every value's first byte, then every value's
// second byte, and so on. Neighbouring values usually share high bytes, so this
// groups near-identical bytes together and the chunk compressor exploits it.
//
// src holds `count` values of `width` bytes each, laid out normally.
// dst receives the interleaved form: dst[b * count + i] == src[i * width + b].

inline void interleave(const uint8_t* src, uint8_t* dst, std::size_t count, std::size_t width) {
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t b = 0; b < width; ++b)
            dst[b * count + i] = src[i * width + b];
}

inline void deinterleave(const uint8_t* src, uint8_t* dst, std::size_t count, std::size_t width) {
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t b = 0; b < width; ++b)
            dst[i * width + b] = src[b * count + i];
}

}  // namespace bit
}  // namespace rbxl
```

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests -ts="*"
```
Expected: PASS, including all four golden-vector cases.

- [ ] **Step 5: Commit**

```bash
git add include/rbxl/bitutil.hpp tests/test_bitutil.cpp tests/CMakeLists.txt
git commit -m "feat: add byte order, zigzag, Roblox float, and interleaving primitives"
```

---

## Task 3: Value types and `Variant`

**Files:**
- Create: `include/rbxl/types.hpp`, `include/rbxl/variant.hpp`
- Create: `src/variant.cpp`
- Create: `tests/test_variant.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `rbxl::Result` (Task 1).
- Produces: all structs listed below in namespace `rbxl`; `rbxl::Variant`;
  `rbxl::VariantType` (an enum class with one entry per alternative, `Nil = 0`);
  `VariantType variantTypeOf(const Variant&)`;
  `const char* variantTypeName(VariantType)`.

**Design note.** `Attributes`, `Tags`, and `MaterialColors` are *not* Variant alternatives. Roblox stores them as opaque `BinaryString` blobs with their own private encodings; the DOM keeps them as raw bytes so a round-trip is lossless by construction, and Task 13 supplies explicit parse/serialise helpers for callers who want structure.

- [ ] **Step 1: Write `include/rbxl/types.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rbxl {

using InstanceId = uint32_t;
constexpr InstanceId kNoInstance = static_cast<InstanceId>(-1);

struct Vector2      { float x = 0, y = 0; };
struct Vector3      { float x = 0, y = 0, z = 0; };
struct Vector2int16 { int16_t x = 0, y = 0; };
struct Vector3int16 { int16_t x = 0, y = 0, z = 0; };

// Rotation is row-major: R00 R01 R02 R10 R11 R12 R20 R21 R22.
struct CFrame {
    Vector3 position;
    float rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};
struct OptionalCFrame { bool hasValue = false; CFrame value; };

struct UDim  { float scale = 0; int32_t offset = 0; };
struct UDim2 { UDim x, y; };
struct Ray   { Vector3 origin, direction; };
struct Rect  { Vector2 min, max; };
struct Region3      { Vector3 min, max; };
struct Region3int16 { Vector3int16 min, max; };

struct Color3      { float r = 0, g = 0, b = 0; };
struct Color3uint8 { uint8_t r = 0, g = 0, b = 0; };
struct BrickColor  { uint32_t number = 0; };

// Bitfields. Faces: Front, Bottom, Left, Back, Top, Right in the low 6 bits.
// Axes: X, Y, Z in the low 3 bits.
struct Faces { uint8_t bits = 0; };
struct Axes  { uint8_t bits = 0; };

struct NumberSequenceKeypoint { float time = 0, value = 0, envelope = 0; };
struct NumberSequence { std::vector<NumberSequenceKeypoint> keypoints; };

// `envelope` is serialised by Roblox but has no effect.
struct ColorSequenceKeypoint { float time = 0; Color3 color; float envelope = 0; };
struct ColorSequence { std::vector<ColorSequenceKeypoint> keypoints; };

struct NumberRange { float min = 0, max = 0; };

struct PhysicalProperties {
    bool custom = false;
    bool hasAcousticAbsorption = false;
    float density = 0, friction = 0, elasticity = 0;
    float frictionWeight = 0, elasticityWeight = 0;
    float acousticAbsorption = 1.0f;   // spec default when the flag bit is clear
};

struct Font {
    std::string family;        // a content URI, e.g. rbxasset://fonts/families/Arial.json
    uint16_t weight = 400;     // FontWeight value, 100..900 in steps of 100
    uint8_t style = 0;         // FontStyle: 0 = Normal, 1 = Italic
    std::string cachedFaceId;  // may be empty; omitted from XML when empty
};

// Roblox's instance identity stamp. Note the field ORDER and the Random
// rotation differ between the binary and XML encodings; see the codec tasks.
struct UniqueId {
    uint32_t index = 0;
    uint32_t time = 0;      // seconds since 2021-01-01
    int64_t random = 0;
};

struct SecurityCapabilities { uint64_t value = 0; };

// The modern Content datatype (Roblox release 645+). Distinct from ContentId.
struct Content {
    enum class SourceType : uint32_t { None = 0, Uri = 1, Object = 2 };
    SourceType sourceType = SourceType::None;
    std::string uri;                    // when sourceType == Uri
    InstanceId object = kNoInstance;    // when sourceType == Object
};

// The legacy type, called `Content` before release 645. Serialised as <url>.
struct ContentId { std::string url; };

struct Ref { InstanceId target = kNoInstance; };

// Enum properties. Named EnumValue because `Enum` reads badly in C++.
struct EnumValue { uint32_t value = 0; };

// Attributes-only type carrying the enum's name alongside its value.
struct EnumItem { std::string enumType; uint32_t value = 0; };

// Arbitrary bytes. Roblox hides several private formats behind this type
// (attributes, tags, terrain material colours); see blob.hpp.
struct BinaryString { std::vector<uint8_t> data; };

// Source code. Whitespace must be preserved exactly; XML writes it as CDATA.
struct ProtectedString { std::string value; };

// Precompiled Luau. Never interpret or modify this; read and write it as-is.
struct Bytecode { std::vector<uint8_t> data; };

// A string held once in the file's shared table and referenced by many
// instances. `key` holds the raw identifier bytes (16 bytes in binary files,
// the base64-decoded `md5` attribute in XML). Roblox ignores the key's value.
struct SharedString { std::string key; std::string value; };

// Stored identically to SharedString but semantically an asset reference.
struct NetAssetRef { std::string key; std::string value; };

}  // namespace rbxl
```

- [ ] **Step 2: Write `include/rbxl/variant.hpp`**

```cpp
#pragma once
#include <rbxl/types.hpp>
#include <string>
#include <variant>

namespace rbxl {

// One alternative per serialisable property type across both file formats.
// std::monostate means "no value"; a default-constructed Variant is Nil.
using Variant = std::variant<
    std::monostate,
    std::string,        // String
    bool,               // Bool
    int32_t,            // Int32
    int64_t,            // Int64
    float,              // Float32
    double,             // Float64
    UDim, UDim2, Ray, Faces, Axes, BrickColor,
    Color3, Color3uint8, Vector2, Vector2int16, Vector3, Vector3int16,
    CFrame, OptionalCFrame, Region3, Region3int16, Rect,
    EnumValue, EnumItem, Ref,
    NumberSequence, ColorSequence, NumberRange,
    PhysicalProperties, Font, UniqueId, SecurityCapabilities,
    Content, ContentId,
    BinaryString, ProtectedString, Bytecode,
    SharedString, NetAssetRef>;

// Mirrors the alternatives above, in the same order.
enum class VariantType {
    Nil = 0, String, Bool, Int32, Int64, Float32, Float64,
    UDim, UDim2, Ray, Faces, Axes, BrickColor,
    Color3, Color3uint8, Vector2, Vector2int16, Vector3, Vector3int16,
    CFrame, OptionalCFrame, Region3, Region3int16, Rect,
    EnumValue, EnumItem, Ref,
    NumberSequence, ColorSequence, NumberRange,
    PhysicalProperties, Font, UniqueId, SecurityCapabilities,
    Content, ContentId,
    BinaryString, ProtectedString, Bytecode,
    SharedString, NetAssetRef,
};

inline VariantType variantTypeOf(const Variant& v) {
    return static_cast<VariantType>(v.index());
}

const char* variantTypeName(VariantType type);

// Deep equality. Floats compare bitwise so that NaN payloads and signed zero
// survive round-trip assertions unchanged.
bool variantEqual(const Variant& a, const Variant& b);

}  // namespace rbxl
```

- [ ] **Step 3: Write `src/variant.cpp`**

Implement `variantTypeName` as a `switch` returning the literal name for every enumerator (the compiler will warn on a missing case, which is the point), and `variantEqual` as a `std::visit` over both alternatives that returns `false` when the indices differ and otherwise compares members field by field. Floats and doubles are compared by `std::memcmp` of their bit patterns, never by `==`.

- [ ] **Step 4: Write `tests/test_variant.cpp`**

```cpp
#include <doctest.h>
#include <rbxl/variant.hpp>
#include <cstring>

using namespace rbxl;

TEST_CASE("VariantType enum stays aligned with the variant alternatives") {
    CHECK(variantTypeOf(Variant{}) == VariantType::Nil);
    CHECK(variantTypeOf(Variant{std::string("hi")}) == VariantType::String);
    CHECK(variantTypeOf(Variant{true}) == VariantType::Bool);
    CHECK(variantTypeOf(Variant{int32_t{1}}) == VariantType::Int32);
    CHECK(variantTypeOf(Variant{int64_t{1}}) == VariantType::Int64);
    CHECK(variantTypeOf(Variant{1.0f}) == VariantType::Float32);
    CHECK(variantTypeOf(Variant{1.0}) == VariantType::Float64);
    CHECK(variantTypeOf(Variant{CFrame{}}) == VariantType::CFrame);
    CHECK(variantTypeOf(Variant{Font{}}) == VariantType::Font);
    CHECK(variantTypeOf(Variant{Content{}}) == VariantType::Content);
    CHECK(variantTypeOf(Variant{ContentId{}}) == VariantType::ContentId);
    CHECK(variantTypeOf(Variant{NetAssetRef{}}) == VariantType::NetAssetRef);
}

TEST_CASE("every VariantType has a name") {
    for (int i = 0; i <= static_cast<int>(VariantType::NetAssetRef); ++i) {
        const char* name = variantTypeName(static_cast<VariantType>(i));
        REQUIRE(name != nullptr);
        CHECK(std::strlen(name) > 0);
    }
}

TEST_CASE("variantEqual compares by value and by type") {
    CHECK(variantEqual(Variant{int32_t{5}}, Variant{int32_t{5}}));
    CHECK_FALSE(variantEqual(Variant{int32_t{5}}, Variant{int64_t{5}}));
    CHECK(variantEqual(Variant{Vector3{1, 2, 3}}, Variant{Vector3{1, 2, 3}}));
    CHECK_FALSE(variantEqual(Variant{Vector3{1, 2, 3}}, Variant{Vector3{1, 2, 4}}));

    NumberSequence a; a.keypoints.push_back({0.0f, 1.0f, 0.0f});
    NumberSequence b; b.keypoints.push_back({0.0f, 1.0f, 0.0f});
    CHECK(variantEqual(Variant{a}, Variant{b}));
    b.keypoints.push_back({1.0f, 0.0f, 0.0f});
    CHECK_FALSE(variantEqual(Variant{a}, Variant{b}));
}

TEST_CASE("float comparison is bitwise so signed zero is preserved") {
    CHECK_FALSE(variantEqual(Variant{0.0f}, Variant{-0.0f}));
}

TEST_CASE("Variant stays small enough for multi-million value files") {
    // RaceAPet.rbxl holds ~14 million property values. Guard against a new
    // alternative silently inflating every one of them. Raise deliberately,
    // with a measurement, never incidentally.
    CHECK(sizeof(Variant) <= 88);
}
```

- [ ] **Step 5: Build and run**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/rbxl/types.hpp include/rbxl/variant.hpp src/variant.cpp tests/test_variant.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add Roblox value types and the property Variant"
```

---

## Task 4: The `Dom` (instance pool, string pool, tree operations)

**Files:**
- Create: `include/rbxl/dom.hpp`, `src/dom.cpp`
- Create: `tests/test_dom.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Variant`, `InstanceId`, `kNoInstance` (Task 3).
- Produces: `rbxl::NameId`, `rbxl::StringPool`, `rbxl::Instance`, `rbxl::RawChunk`, `rbxl::Dom` with the member functions listed in Step 1.

**Design note.** Instances live in one `std::vector` and refer to each other by index, not by pointer. File referents are already dense integers, so index lookup is O(1) with no hash map; parent/child cycles cannot leak the way `shared_ptr` would; and a 734,657-instance file costs one allocation for the pool rather than 734,657 control blocks. Property names are interned to a `NameId`, because a class's instances all share the same property names and storing those strings per instance would dominate memory.

- [ ] **Step 1: Write `include/rbxl/dom.hpp`**

```cpp
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
    std::vector<uint8_t> data;   // decompressed payload
};

class Dom {
public:
    // --- Instances ---------------------------------------------------------
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
    std::vector<Instance> instances_;
    std::vector<InstanceId> roots_;
    StringPool names_;
    std::vector<std::pair<std::string, std::string>> metadata_;
    std::vector<RawChunk> unknownChunks_;
};

}  // namespace rbxl
```

- [ ] **Step 2: Write `tests/test_dom.cpp`**

```cpp
#include <doctest.h>
#include <rbxl/dom.hpp>

using namespace rbxl;

TEST_CASE("StringPool interns names stably") {
    StringPool pool;
    NameId a = pool.intern("Size");
    NameId b = pool.intern("Name");
    CHECK(pool.intern("Size") == a);
    CHECK(a != b);
    CHECK(pool.name(a) == "Size");
    CHECK(pool.find("Size") == a);
    CHECK(pool.find("Nope") == kNoName);
    CHECK(pool.size() == 2);
}

TEST_CASE("Dom creates instances and parents them") {
    Dom dom;
    auto root = dom.create("Model");
    auto child = dom.create("Part");
    CHECK(dom.roots().size() == 2);

    dom.setParent(child, root);
    CHECK(dom.at(child).parent == root);
    CHECK(dom.at(root).children.size() == 1);
    CHECK(dom.at(root).children[0] == child);
    CHECK(dom.roots().size() == 1);
    CHECK(dom.roots()[0] == root);
}

TEST_CASE("Reparenting detaches from the previous parent") {
    Dom dom;
    auto a = dom.create("Model");
    auto b = dom.create("Model");
    auto child = dom.create("Part");
    dom.setParent(child, a);
    dom.setParent(child, b);
    CHECK(dom.at(a).children.empty());
    CHECK(dom.at(b).children.size() == 1);
    CHECK(dom.at(child).parent == b);
}

TEST_CASE("Detaching to kNoInstance makes an instance a root again") {
    Dom dom;
    auto parent = dom.create("Model");
    auto child = dom.create("Part");
    dom.setParent(child, parent);
    dom.setParent(child, kNoInstance);
    CHECK(dom.at(parent).children.empty());
    CHECK(dom.roots().size() == 2);
}

TEST_CASE("Properties round-trip through the name pool") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Baseplate"));
    dom.setProperty(id, "Size", Vector3{4, 1, 2});
    CHECK(dom.nameOf(id) == "Baseplate");

    const Variant* size = dom.getProperty(id, "Size");
    REQUIRE(size != nullptr);
    CHECK(variantTypeOf(*size) == VariantType::Vector3);
    CHECK(std::get<Vector3>(*size).x == 4.0f);
    CHECK(dom.getProperty(id, "Missing") == nullptr);
}

TEST_CASE("Setting a property twice overwrites rather than duplicating") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("A"));
    dom.setProperty(id, "Name", std::string("B"));
    CHECK(dom.at(id).properties.size() == 1);
    CHECK(dom.nameOf(id) == "B");
}

TEST_CASE("Property storage stays sorted by NameId") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Zebra", std::string("z"));
    dom.setProperty(id, "Alpha", std::string("a"));
    dom.setProperty(id, "Middle", std::string("m"));
    const auto& props = dom.at(id).properties;
    for (std::size_t i = 1; i < props.size(); ++i) {
        CHECK(props[i - 1].first < props[i].first);
    }
}

TEST_CASE("postOrder visits descendants before ancestors") {
    // Build the tree from the format spec:
    //   1 -> {2, 3 -> {5, 6}, 4 -> {7}}
    Dom dom;
    auto n1 = dom.create("Folder");
    auto n2 = dom.create("Folder");
    auto n3 = dom.create("Folder");
    auto n4 = dom.create("Folder");
    auto n5 = dom.create("Folder");
    auto n6 = dom.create("Folder");
    auto n7 = dom.create("Folder");
    dom.setParent(n2, n1); dom.setParent(n3, n1); dom.setParent(n4, n1);
    dom.setParent(n5, n3); dom.setParent(n6, n3);
    dom.setParent(n7, n4);

    // Spec states Roblox Studio writes: 2, 5, 6, 3, 7, 4, 1
    std::vector<InstanceId> expected{n2, n5, n6, n3, n7, n4, n1};
    CHECK(dom.postOrder() == expected);
}
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -5
```
Expected: FAIL, `rbxl/dom.hpp: No such file or directory`.

- [ ] **Step 4: Implement `src/dom.cpp`**

Implementation notes that the tests pin down:
- `create` appends to `instances_` and pushes the new id onto `roots_`.
- `setParent` removes the child from its old parent's `children` (or from `roots_`), then appends to the new parent's `children` (or to `roots_`). Use `std::find` plus erase; children vectors are short.
- `setProperty` interns the name, binary-searches `properties` by `NameId`, and either assigns in place or inserts at the lower-bound position so the vector stays sorted.
- `getProperty` binary-searches; returns `nullptr` when the name is not interned at all.
- `postOrder` iterates `roots_` and runs an explicit stack (not recursion — a 734k-instance file can nest deeply enough to overflow the call stack). Reserve `instances_.size()` up front.

- [ ] **Step 5: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS, including the post-order case matching the spec's `2, 5, 6, 3, 7, 4, 1`.

- [ ] **Step 6: Commit**

```bash
git add include/rbxl/dom.hpp src/dom.cpp tests/test_dom.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add Dom instance pool, string pool, and post-order traversal"
```

---

## Task 5: Binary chunk layer (header, framing, LZ4/Zstd)

**Files:**
- Create: `include/rbxl/compression.hpp`, `src/binary/chunk.hpp`, `src/binary/chunk.cpp`
- Create: `tests/test_chunk.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Result`, `bit::*`.
- Produces, in namespace `rbxl::binary`:
  - `struct FileHeader { uint32_t classCount = 0; uint32_t instanceCount = 0; };`
  - `struct Chunk { char name[4]; std::vector<uint8_t> data; };`
  - `rbxl::Compression`, declared in the **public** header `include/rbxl/compression.hpp` as
    `enum class Compression { None, Lz4, Zstd };`. It lives in a public header rather than
    beside the chunk code because `SaveOptions` (Task 12) exposes it to callers.
  - `Result<FileHeader> readFileHeader(const uint8_t* data, size_t size);`
  - `void writeFileHeader(std::vector<uint8_t>& out, const FileHeader&);`
  - `Result<Chunk> readChunk(const uint8_t* data, size_t size, size_t& cursor);`
  - `Status writeChunk(std::vector<uint8_t>& out, const char name[4], const std::vector<uint8_t>& payload, Compression, int level);`
  - `constexpr size_t kFileHeaderSize = 32;`
  - `constexpr size_t kChunkHeaderSize = 16;`

**Format reference (normative):**

File header, 32 bytes:

| Offset | Size | Field | Value |
|---:|---:|:--|:--|
| 0 | 8 | Magic | `<roblox!` |
| 8 | 6 | Signature | `89 ff 0d 0a 1a 0a` |
| 14 | 2 | Version (`u16` LE) | `0` |
| 16 | 4 | Class count (`i32` LE) | number of distinct classes |
| 20 | 4 | Instance count (`i32` LE) | total instances |
| 24 | 8 | Reserved | zero |

Chunk header, 16 bytes: name `char[4]` (zero-padded), compressed length `u32` LE, uncompressed length `u32` LE, reserved `u32` LE = 0. If compressed length is `0`, the payload is `uncompressedLength` raw bytes. Otherwise the payload is `compressedLength` bytes: **Zstd if it begins with `28 b5 2f fd`, LZ4 block format otherwise.** The `END` chunk (`</roblox>`, 9 bytes) must never be compressed.

- [ ] **Step 1: Write the failing test**

`tests/test_chunk.cpp`:

```cpp
#include <doctest.h>
#include "binary/chunk.hpp"
#include <cstring>
#include <string>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

static std::vector<uint8_t> bytes(const char* s, size_t n) {
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(s),
                                reinterpret_cast<const uint8_t*>(s) + n);
}

TEST_CASE("file header round-trips") {
    std::vector<uint8_t> out;
    FileHeader in; in.classCount = 3; in.instanceCount = 17;
    writeFileHeader(out, in);
    REQUIRE(out.size() == kFileHeaderSize);
    CHECK(std::memcmp(out.data(), "<roblox!", 8) == 0);
    CHECK(out[8] == 0x89); CHECK(out[9] == 0xff); CHECK(out[10] == 0x0d);
    CHECK(out[11] == 0x0a); CHECK(out[12] == 0x1a); CHECK(out[13] == 0x0a);

    auto parsed = readFileHeader(out.data(), out.size());
    REQUIRE(parsed);
    CHECK(parsed.value().classCount == 3);
    CHECK(parsed.value().instanceCount == 17);
}

TEST_CASE("file header rejects bad input") {
    auto tooShort = readFileHeader(reinterpret_cast<const uint8_t*>("<roblox!"), 8);
    REQUIRE_FALSE(tooShort);
    CHECK(tooShort.error().code == ErrorCode::Truncated);

    std::vector<uint8_t> wrong(kFileHeaderSize, 0);
    std::memcpy(wrong.data(), "<xml----", 8);
    auto badMagic = readFileHeader(wrong.data(), wrong.size());
    REQUIRE_FALSE(badMagic);
    CHECK(badMagic.error().code == ErrorCode::BadMagic);
}

TEST_CASE("uncompressed chunks round-trip") {
    std::vector<uint8_t> file;
    auto payload = bytes("</roblox>", 9);
    REQUIRE(writeChunk(file, "END\0", payload, Compression::None, 0));
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE(chunk);
    CHECK(std::memcmp(chunk.value().name, "END\0", 4) == 0);
    CHECK(chunk.value().data == payload);
    CHECK(cursor == file.size());
}

TEST_CASE("lz4 chunks round-trip") {
    std::vector<uint8_t> payload(8192, 0x5A);
    std::vector<uint8_t> file;
    REQUIRE(writeChunk(file, "PROP", payload, Compression::Lz4, 0));
    // Compression must actually have happened.
    CHECK(file.size() < payload.size());
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE(chunk);
    CHECK(chunk.value().data == payload);
}

TEST_CASE("zstd chunks round-trip and are detected by magic") {
    std::vector<uint8_t> payload(8192, 0x3C);
    std::vector<uint8_t> file;
    REQUIRE(writeChunk(file, "PROP", payload, Compression::Zstd, 3));
    // The compressed body starts right after the 16-byte chunk header.
    CHECK(file[kChunkHeaderSize + 0] == 0x28);
    CHECK(file[kChunkHeaderSize + 1] == 0xb5);
    CHECK(file[kChunkHeaderSize + 2] == 0x2f);
    CHECK(file[kChunkHeaderSize + 3] == 0xfd);
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE(chunk);
    CHECK(chunk.value().data == payload);
}

TEST_CASE("truncated chunk payloads are reported, not read past") {
    std::vector<uint8_t> payload(64, 1);
    std::vector<uint8_t> file;
    REQUIRE(writeChunk(file, "PROP", payload, Compression::None, 0));
    file.resize(file.size() - 8);
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    REQUIRE_FALSE(chunk);
    CHECK(chunk.error().code == ErrorCode::Truncated);
}

TEST_CASE("a declared uncompressed length that is absurd is rejected") {
    // Guards against a malicious or corrupt file forcing a huge allocation.
    std::vector<uint8_t> file(kChunkHeaderSize, 0);
    std::memcpy(file.data(), "PROP", 4);
    bit::writeU32LE(file.data() + 4, 4);           // compressed length
    bit::writeU32LE(file.data() + 8, 0xFFFFFFFFu); // uncompressed length
    file.insert(file.end(), {0x01, 0x02, 0x03, 0x04});
    size_t cursor = 0;
    auto chunk = readChunk(file.data(), file.size(), cursor);
    CHECK_FALSE(chunk);
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL, `binary/chunk.hpp: No such file or directory`.

- [ ] **Step 3: Implement `src/binary/chunk.hpp` and `chunk.cpp`**

Implementation notes:
- `readFileHeader` checks size, magic, signature, then version, returning `BadMagic` / `BadVersion` / `Truncated` with the failing offset. Class and instance counts are read as `u32` LE and rejected if the signed interpretation is negative.
- `readChunk` reads the 16-byte header, validates `cursor + headerSize + bodyLength <= size`, then either copies the body or decompresses it. Cap `uncompressedLength` at a `kMaxChunkSize` of 512 MiB and return `Malformed` above that, so a corrupt length cannot trigger a giant allocation before any data is read.
- Decompression dispatch: if the first four body bytes are `28 b5 2f fd`, call `ZSTD_decompress`; else `LZ4_decompress_safe`. Both must return exactly `uncompressedLength`; anything else is `ErrorCode::Compression`.
- `writeChunk` with `Compression::None` writes compressed length `0`. Otherwise it compresses, and **falls back to storing the payload uncompressed if the compressed form is not smaller** — a chunk larger than its input wastes space and Roblox reads uncompressed chunks fine.
- `LZ4_compress_default` produces the raw block format Roblox expects; do not use the frame API (`LZ4F_*`), which would prepend a frame magic and break Roblox's sniffing.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS.

- [ ] **Step 5: Add a corpus smoke test**

Append to `tests/test_chunk.cpp`. It reads real headers from `temp/` and skips when the corpus is absent.

```cpp
#include <fstream>

static std::vector<uint8_t> readFileOrEmpty(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

TEST_CASE("corpus: real place files parse their header and chunk stream") {
    const char* names[] = {"Bladeborne Assets.rbxl", "Bladeborne Floor 0.rbxl",
                           "Bladeborne Floor 1.rbxl", "FusionCore.rbxl", "RaceAPet.rbxl"};
    for (const char* name : names) {
        auto data = readFileOrEmpty(std::string(RBXL_TEST_DATA_DIR) + "/" + name);
        if (data.empty()) continue;   // corpus not present; not a failure
        CAPTURE(name);
        auto header = readFileHeader(data.data(), data.size());
        REQUIRE(header);
        CHECK(header.value().instanceCount > 0);

        size_t cursor = kFileHeaderSize;
        size_t instChunks = 0, propChunks = 0;
        bool sawEnd = false;
        while (cursor < data.size()) {
            auto chunk = readChunk(data.data(), data.size(), cursor);
            REQUIRE(chunk);
            if (std::memcmp(chunk.value().name, "INST", 4) == 0) ++instChunks;
            if (std::memcmp(chunk.value().name, "PROP", 4) == 0) ++propChunks;
            if (std::memcmp(chunk.value().name, "END\0", 4) == 0) { sawEnd = true; break; }
        }
        CHECK(sawEnd);
        CHECK(instChunks == header.value().classCount);
        CHECK(propChunks > 0);
    }
}
```

Run it. Expected when the corpus is present: every file parses, and `instChunks` equals the header's class count (111, 144, 150, 143, and 162 respectively).

- [ ] **Step 6: Commit**

```bash
git add src/binary/chunk.hpp src/binary/chunk.cpp tests/test_chunk.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add binary chunk framing with lz4 and zstd support"
```

---

## Task 6: Property array codecs, part 1 (scalar and byte-array types)

**Files:**
- Create: `src/binary/valuecodec.hpp`, `src/binary/valuecodec_scalar.cpp`
- Create: `tests/test_valuecodec_scalar.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Variant`, `bit::*`, `Result`.
- Produces, in namespace `rbxl::binary`:
  - `enum class TypeId : uint8_t { ... };` with every value in Appendix A.1.
  - `struct CodecContext { const std::vector<SharedString>* sharedStrings = nullptr; };`
  - `Result<std::vector<Variant>> decodeValueArray(TypeId, const uint8_t* data, size_t size, size_t count, const CodecContext&);`
  - `Status encodeValueArray(TypeId, const std::vector<Variant>&, std::vector<uint8_t>& out, const CodecContext&);`
  - `bool isKnownTypeId(uint8_t);`

**Important:** `Ref` values decoded here carry the **raw file referent** in `Ref::target`, already accumulated but not yet mapped to an `InstanceId`. Task 8 performs the mapping. Keeping the mapping out of the codec is what lets Task 7's `Content` reuse the same referent reader.

**Types covered by this task:**

| Type | ID | Encoding |
|:--|:--|:--|
| String | `0x01` | `u32` LE length prefix + bytes, in sequence, no interleaving |
| Bool | `0x02` | one byte per value, `0x00`/`0x01`, in sequence |
| Int32 | `0x03` | zigzag, big-endian `u32`, **interleaved** width 4 |
| Float32 | `0x04` | Roblox float, big-endian, **interleaved** width 4 |
| Float64 | `0x05` | IEEE-754 little-endian, in sequence, no interleaving |
| Faces | `0x09` | one byte bitfield per value, in sequence |
| Axes | `0x0a` | one byte bitfield per value, in sequence |
| BrickColor | `0x0b` | untransformed big-endian `u32`, **interleaved** width 4 |
| Enum | `0x12` | untransformed big-endian `u32`, **interleaved** width 4 |
| Referent | `0x13` | as Int32, **interleaved**, then **accumulated**: value[i] += value[i-1] |
| Vector3int16 | `0x14` | three little-endian `i16`, in sequence, no interleaving |
| Color3uint8 | `0x1a` | three byte arrays R, G, B; each in sequence, no interleaving |
| Int64 | `0x1b` | zigzag 64-bit, big-endian, **interleaved** width 8 |
| SharedString | `0x1c` | big-endian `u32` index into the SSTR table, **interleaved** width 4 |
| Bytecode | `0x1d` | identical to String; never interpret the contents |
| SecurityCapabilities | `0x21` | as Int64 (zigzag, big-endian, interleaved width 8), reinterpreted as `u64` |

`NetAssetRef` properties use type ID `0x1c` and are indistinguishable from `SharedString` in the binary format. Decode them as `SharedString`; only the XML codec can tell them apart.

- [ ] **Step 1: Write the failing test**

`tests/test_valuecodec_scalar.cpp`:

```cpp
#include <doctest.h>
#include "binary/valuecodec.hpp"
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

static std::vector<Variant> decodeOk(TypeId type, std::vector<uint8_t> data, size_t count,
                                     const CodecContext& ctx = {}) {
    auto r = decodeValueArray(type, data.data(), data.size(), count, ctx);
    REQUIRE(r);
    return r.value();
}

static std::vector<uint8_t> encodeOk(TypeId type, const std::vector<Variant>& values,
                                     const CodecContext& ctx = {}) {
    std::vector<uint8_t> out;
    REQUIRE(encodeValueArray(type, values, out, ctx));
    return out;
}

TEST_CASE("String values are length-prefixed and sequential") {
    std::vector<uint8_t> data{0x02, 0, 0, 0, 'h', 'i', 0x01, 0, 0, 0, 'x'};
    auto values = decodeOk(TypeId::String, data, 2);
    REQUIRE(values.size() == 2);
    CHECK(std::get<std::string>(values[0]) == "hi");
    CHECK(std::get<std::string>(values[1]) == "x");
    CHECK(encodeOk(TypeId::String, values) == data);
}

TEST_CASE("Bool values are one byte each") {
    auto values = decodeOk(TypeId::Bool, {0x01, 0x00, 0x01}, 3);
    CHECK(std::get<bool>(values[0]));
    CHECK_FALSE(std::get<bool>(values[1]));
    CHECK(std::get<bool>(values[2]));
    CHECK(encodeOk(TypeId::Bool, values) == std::vector<uint8_t>{0x01, 0x00, 0x01});
}

TEST_CASE("Int32 values are zigzagged, big-endian, and interleaved") {
    // The Offset half of the spec's UDim example: values 2 and 4.
    auto values = decodeOk(TypeId::Int32, {0, 0, 0, 0, 0, 0, 0x04, 0x08}, 2);
    CHECK(std::get<int32_t>(values[0]) == 2);
    CHECK(std::get<int32_t>(values[1]) == 4);
    CHECK(encodeOk(TypeId::Int32, values) ==
          std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0x04, 0x08});
}

TEST_CASE("Float32 values use the Roblox format and are interleaved") {
    // The Scale half of the spec's UDim example: values 1.0 and 3.0.
    auto values = decodeOk(TypeId::Float32, {0x7f, 0x80, 0x00, 0x80, 0, 0, 0, 0}, 2);
    CHECK(std::get<float>(values[0]) == 1.0f);
    CHECK(std::get<float>(values[1]) == 3.0f);
    CHECK(encodeOk(TypeId::Float32, values) ==
          std::vector<uint8_t>{0x7f, 0x80, 0x00, 0x80, 0, 0, 0, 0});
}

TEST_CASE("Float64 values are plain little-endian IEEE doubles") {
    std::vector<uint8_t> data{0, 0, 0, 0, 0, 0, 0xf0, 0x3f};   // 1.0
    auto values = decodeOk(TypeId::Float64, data, 1);
    CHECK(std::get<double>(values[0]) == 1.0);
    CHECK(encodeOk(TypeId::Float64, values) == data);
}

TEST_CASE("Faces and Axes are single-byte bitfields") {
    // Spec: Front / (Back, Top) / (Bottom, Left, Right) -> 01 18 26
    auto faces = decodeOk(TypeId::Faces, {0x01, 0x18, 0x26}, 3);
    CHECK(std::get<Faces>(faces[0]).bits == 0x01);
    CHECK(std::get<Faces>(faces[2]).bits == 0x26);
    // Spec: X / (X,Y) / (X,Z) -> 01 03 05
    auto axes = decodeOk(TypeId::Axes, {0x01, 0x03, 0x05}, 3);
    CHECK(std::get<Axes>(axes[1]).bits == 0x03);
    CHECK(encodeOk(TypeId::Axes, axes) == std::vector<uint8_t>{0x01, 0x03, 0x05});
}

TEST_CASE("BrickColor numbers are untransformed, big-endian, interleaved") {
    // Spec: Really red (1004), Bright green (37), Really blue (1010).
    std::vector<uint8_t> data{0, 0, 0, 0, 0, 0, 0x03, 0x00, 0x03, 0xEC, 0x25, 0xF2};
    auto values = decodeOk(TypeId::BrickColor, data, 3);
    CHECK(std::get<BrickColor>(values[0]).number == 1004u);
    CHECK(std::get<BrickColor>(values[1]).number == 37u);
    CHECK(std::get<BrickColor>(values[2]).number == 1010u);
    CHECK(encodeOk(TypeId::BrickColor, values) == data);
}

TEST_CASE("Referents accumulate across the array") {
    // Spec: raw [1619, 1, 4, 2, 3, 5] means [1619, 1620, 1624, 1626, 1629, 1634].
    std::vector<int32_t> raw{1619, 1, 4, 2, 3, 5};
    std::vector<uint8_t> flat(raw.size() * 4), woven(raw.size() * 4);
    for (size_t i = 0; i < raw.size(); ++i)
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(raw[i]));
    bit::interleave(flat.data(), woven.data(), raw.size(), 4);

    auto values = decodeOk(TypeId::Referent, woven, raw.size());
    const int32_t expected[] = {1619, 1620, 1624, 1626, 1629, 1634};
    for (size_t i = 0; i < raw.size(); ++i)
        CHECK(static_cast<int32_t>(std::get<Ref>(values[i]).target) == expected[i]);

    // Encoding must re-apply the delta, reproducing the original bytes exactly.
    CHECK(encodeOk(TypeId::Referent, values) == woven);
}

TEST_CASE("A null referent decodes to kNoInstance") {
    std::vector<uint8_t> flat(4), woven(4);
    bit::writeU32BE(flat.data(), bit::zigzagEncode32(-1));
    bit::interleave(flat.data(), woven.data(), 1, 4);
    auto values = decodeOk(TypeId::Referent, woven, 1);
    CHECK(std::get<Ref>(values[0]).target == kNoInstance);
}

TEST_CASE("Vector3int16 is three little-endian i16 in sequence") {
    // NOTE: the published spec's positive example is a typo (it shows big-endian
    // bytes). Its prose and its negative example both say little-endian, which is
    // what Roblox actually writes. Do not "fix" this test to match the document.
    std::vector<uint8_t> data{0x01, 0x00, 0x02, 0x00, 0x03, 0x00,
                              0xFF, 0xFF, 0xFE, 0xFF, 0xFD, 0xFF};
    auto values = decodeOk(TypeId::Vector3int16, data, 2);
    auto a = std::get<Vector3int16>(values[0]);
    auto b = std::get<Vector3int16>(values[1]);
    CHECK(a.x == 1); CHECK(a.y == 2); CHECK(a.z == 3);
    CHECK(b.x == -1); CHECK(b.y == -2); CHECK(b.z == -3);
    CHECK(encodeOk(TypeId::Vector3int16, values) == data);
}

TEST_CASE("Color3uint8 stores three separate component arrays") {
    // Spec: values (0,255,255) and (63,0,127) -> 00 3f ff 00 ff 7f
    std::vector<uint8_t> data{0x00, 0x3f, 0xff, 0x00, 0xff, 0x7f};
    auto values = decodeOk(TypeId::Color3uint8, data, 2);
    auto a = std::get<Color3uint8>(values[0]);
    auto b = std::get<Color3uint8>(values[1]);
    CHECK(a.r == 0);  CHECK(a.g == 255); CHECK(a.b == 255);
    CHECK(b.r == 63); CHECK(b.g == 0);   CHECK(b.b == 127);
    CHECK(encodeOk(TypeId::Color3uint8, values) == data);
}

TEST_CASE("Int64 is zigzagged, big-endian, and interleaved at width 8") {
    std::vector<Variant> values{int64_t{-5}, int64_t{1}, int64_t{9000000000LL}};
    auto data = encodeOk(TypeId::Int64, values);
    CHECK(data.size() == 24);
    auto back = decodeOk(TypeId::Int64, data, 3);
    CHECK(std::get<int64_t>(back[0]) == -5);
    CHECK(std::get<int64_t>(back[1]) == 1);
    CHECK(std::get<int64_t>(back[2]) == 9000000000LL);
}

TEST_CASE("SecurityCapabilities uses the Int64 encoding as unsigned bits") {
    std::vector<Variant> values{SecurityCapabilities{0xFFFFFFFFFFFFFFFFull},
                                SecurityCapabilities{0}};
    auto data = encodeOk(TypeId::SecurityCapabilities, values);
    auto back = decodeOk(TypeId::SecurityCapabilities, data, 2);
    CHECK(std::get<SecurityCapabilities>(back[0]).value == 0xFFFFFFFFFFFFFFFFull);
    CHECK(std::get<SecurityCapabilities>(back[1]).value == 0ull);
}

TEST_CASE("SharedString resolves through the file's shared table") {
    std::vector<SharedString> table{{"key0", "hello"}, {"key1", "world"}};
    CodecContext ctx; ctx.sharedStrings = &table;
    std::vector<uint8_t> flat{0, 0, 0, 1, 0, 0, 0, 0}, woven(8);
    bit::interleave(flat.data(), woven.data(), 2, 4);
    auto values = decodeOk(TypeId::SharedString, woven, 2, ctx);
    CHECK(std::get<SharedString>(values[0]).value == "world");
    CHECK(std::get<SharedString>(values[1]).value == "hello");
}

TEST_CASE("An out-of-range shared string index is an error, not a crash") {
    std::vector<SharedString> table{{"key0", "hello"}};
    CodecContext ctx; ctx.sharedStrings = &table;
    std::vector<uint8_t> flat{0, 0, 0, 9}, woven(4);
    bit::interleave(flat.data(), woven.data(), 1, 4);
    auto r = decodeValueArray(TypeId::SharedString, woven.data(), woven.size(), 1, ctx);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Malformed);
}

TEST_CASE("Truncated input is reported rather than read past the end") {
    // Two Int32 values need 8 bytes; supply 5.
    auto r = decodeValueArray(TypeId::Int32, std::vector<uint8_t>{1, 2, 3, 4, 5}.data(), 5, 2, {});
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Truncated);
}

TEST_CASE("Unknown type ids are reported as such") {
    CHECK(isKnownTypeId(0x01));
    CHECK(isKnownTypeId(0x22));
    CHECK_FALSE(isKnownTypeId(0x00));
    CHECK_FALSE(isKnownTypeId(0x7F));
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL to compile, `binary/valuecodec.hpp: No such file or directory`.

- [ ] **Step 3: Implement**

Write `valuecodec.hpp` with the `TypeId` enum from Appendix A.1, `CodecContext`, and the two dispatch declarations. Implement a small bounds-checked `Cursor` helper in the header (`Result` on every read) and use it everywhere; never index a raw pointer without first checking the remaining size.

`valuecodec_scalar.cpp` implements a `switch` over `TypeId` for the sixteen types in the table above and returns `ErrorCode::UnsupportedType` for the rest, which Task 7 fills in. Shared helpers to write once and reuse:

```cpp
// Read `count` values of `width` bytes, undoing interleaving into a flat buffer.
Result<std::vector<uint8_t>> readInterleaved(Cursor& c, size_t count, size_t width);
// Write a flat buffer of `count` values of `width` bytes in interleaved form.
void writeInterleaved(const std::vector<uint8_t>& flat, size_t count, size_t width,
                      std::vector<uint8_t>& out);
```

Referent decoding is the one place with state: read the interleaved zigzag `i32` array, then walk it accumulating (`value[i] += value[i-1]`), and finally map `-1` to `kNoInstance`. Encoding reverses this: map `kNoInstance` back to `-1`, take successive differences, then zigzag, byte-swap, and interleave.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/binary/valuecodec.hpp src/binary/valuecodec_scalar.cpp tests/test_valuecodec_scalar.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add binary codecs for scalar property types"
```

---

## Task 7: Property array codecs, part 2 (composite and struct types)

**Files:**
- Create: `src/binary/valuecodec_struct.cpp`
- Create: `tests/test_valuecodec_struct.cpp`
- Modify: `src/binary/valuecodec.hpp` (dispatch to the new cases), `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Task 6, unchanged signatures.
- Produces: the remaining `TypeId` cases handled by `decodeValueArray` / `encodeValueArray`.

**The structure-of-arrays rule.** Composite types are *not* stored value by value. Each component becomes its own array covering all values, and each of those arrays is separately interleaved. A `Vector3` array of N values is stored as N X components, then N Y components, then N Z components. Getting this backwards produces values that are individually well-formed and collectively scrambled.

**Types covered by this task:**

| Type | ID | Component arrays, in order |
|:--|:--|:--|
| UDim | `0x06` | `Scale` (Float32), `Offset` (Int32) |
| UDim2 | `0x07` | `X.Scale`, `Y.Scale`, `X.Offset`, `Y.Offset` — note scales precede offsets |
| Ray | `0x08` | none: six little-endian IEEE `f32` per value, in sequence |
| Color3 | `0x0c` | `R`, `G`, `B` (Float32) |
| Vector2 | `0x0d` | `X`, `Y` (Float32) |
| Vector3 | `0x0e` | `X`, `Y`, `Z` (Float32) |
| CFrame | `0x10` | per value: `u8` id, then 9 plain IEEE `f32` if id is `0x00`; then a `Vector3` array of positions |
| NumberSequence | `0x15` | none: per value `u32` count + `count` × (time, value, envelope) as plain LE `f32` |
| ColorSequence | `0x16` | none: per value `u32` count + `count` × (time, R, G, B, envelope) as plain LE `f32` |
| NumberRange | `0x17` | none: two plain LE `f32` per value |
| Rect | `0x18` | `Min.X`, `Min.Y`, `Max.X`, `Max.Y` (Float32) |
| PhysicalProperties | `0x19` | none: `u8` bitfield per value, followed inline by 5 or 6 plain LE `f32` when bit 0 is set |
| OptionalCFrame | `0x1e` | a `0x10` byte, a CFrame array, a `0x02` byte, then a Bool array |
| UniqueId | `0x1f` | none: 16-byte records, **interleaved at width 16** |
| Font | `0x20` | none: per value `String` family, `u16` LE weight, `u8` style, `String` cachedFaceId |
| Content | `0x22` | an `Enum` array of source types, then counted URI / object / external-object sections |

- [ ] **Step 1: Write the failing test**

`tests/test_valuecodec_struct.cpp`. Each case pairs a decode against a spec byte vector with an encode round-trip.

```cpp
#include <doctest.h>
#include "binary/valuecodec.hpp"
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

static std::vector<Variant> decodeOk(TypeId t, std::vector<uint8_t> d, size_t n,
                                     const CodecContext& ctx = {}) {
    auto r = decodeValueArray(t, d.data(), d.size(), n, ctx);
    REQUIRE(r);
    return r.value();
}
static std::vector<uint8_t> encodeOk(TypeId t, const std::vector<Variant>& v,
                                     const CodecContext& ctx = {}) {
    std::vector<uint8_t> out;
    REQUIRE(encodeValueArray(t, v, out, ctx));
    return out;
}

TEST_CASE("UDim splits into a Scale array then an Offset array") {
    // Spec: UDim{1,2} and UDim{3,4}.
    std::vector<uint8_t> data{0x7f, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08};
    auto values = decodeOk(TypeId::UDim, data, 2);
    CHECK(std::get<UDim>(values[0]).scale == 1.0f);
    CHECK(std::get<UDim>(values[0]).offset == 2);
    CHECK(std::get<UDim>(values[1]).scale == 3.0f);
    CHECK(std::get<UDim>(values[1]).offset == 4);
    CHECK(encodeOk(TypeId::UDim, values) == data);
}

TEST_CASE("UDim2 orders components X.Scale, Y.Scale, X.Offset, Y.Offset") {
    // Spec: a single UDim2 {0.75, -30, -1.5, 60}.
    std::vector<uint8_t> data{0x7e, 0x80, 0x00, 0x00, 0x7f, 0x80, 0x00, 0x01,
                              0x00, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x78};
    auto values = decodeOk(TypeId::UDim2, data, 1);
    auto v = std::get<UDim2>(values[0]);
    CHECK(v.x.scale == 0.75f);
    CHECK(v.x.offset == -30);
    CHECK(v.y.scale == -1.5f);
    CHECK(v.y.offset == 60);
    CHECK(encodeOk(TypeId::UDim2, values) == data);
}

TEST_CASE("Color3 is three interleaved Float32 arrays") {
    // Spec: Color3 for RGB 255, 180, 20.
    std::vector<uint8_t> data{0x7f, 0x00, 0x00, 0x00, 0x7e, 0x69,
                              0x69, 0x6a, 0x7b, 0x41, 0x41, 0x42};
    auto values = decodeOk(TypeId::Color3, data, 1);
    auto c = std::get<Color3>(values[0]);
    CHECK(c.r == 1.0f);
    CHECK(c.g == doctest::Approx(180.0f / 255.0f));
    CHECK(c.b == doctest::Approx(20.0f / 255.0f));
    CHECK(encodeOk(TypeId::Color3, values) == data);
}

TEST_CASE("Vector3 is three interleaved Float32 arrays") {
    // Spec: Vector3(1,2,3) and Vector3(-1,-2,-3).
    std::vector<uint8_t> data{0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                              0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
                              0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x01};
    auto values = decodeOk(TypeId::Vector3, data, 2);
    auto a = std::get<Vector3>(values[0]);
    auto b = std::get<Vector3>(values[1]);
    CHECK(a.x == 1.0f); CHECK(a.y == 2.0f); CHECK(a.z == 3.0f);
    CHECK(b.x == -1.0f); CHECK(b.y == -2.0f); CHECK(b.z == -3.0f);
    CHECK(encodeOk(TypeId::Vector3, values) == data);
}

TEST_CASE("Vector2 is two interleaved Float32 arrays") {
    // Spec: Vector2(-100.80, 200.55) and Vector2(200.55, -100.80).
    std::vector<uint8_t> data{0x85, 0x86, 0x93, 0x91, 0x33, 0x19, 0x35, 0x9a,
                              0x86, 0x85, 0x91, 0x93, 0x19, 0x33, 0x9a, 0x35};
    auto values = decodeOk(TypeId::Vector2, data, 2);
    CHECK(std::get<Vector2>(values[0]).x == doctest::Approx(-100.80f));
    CHECK(std::get<Vector2>(values[0]).y == doctest::Approx(200.55f));
    CHECK(std::get<Vector2>(values[1]).x == doctest::Approx(200.55f));
    CHECK(encodeOk(TypeId::Vector2, values) == data);
}

TEST_CASE("Rect orders components Min.X, Min.Y, Max.X, Max.Y") {
    // Spec: Rect(-1,-10,8,9) and Rect(0,1,5,6).
    std::vector<uint8_t> data{0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
                              0x82, 0x7f, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00,
                              0x82, 0x81, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
                              0x82, 0x81, 0x20, 0x80, 0x00, 0x00, 0x00, 0x00};
    auto values = decodeOk(TypeId::Rect, data, 2);
    auto a = std::get<Rect>(values[0]);
    CHECK(a.min.x == -1.0f); CHECK(a.min.y == -10.0f);
    CHECK(a.max.x == 8.0f);  CHECK(a.max.y == 9.0f);
    CHECK(encodeOk(TypeId::Rect, values) == data);
}

TEST_CASE("Ray is six plain little-endian floats in sequence") {
    std::vector<Variant> values{Ray{{1, 2, 3}, {4, 5, 6}}};
    auto data = encodeOk(TypeId::Ray, values);
    CHECK(data.size() == 24);
    CHECK(bit::readF32LE(data.data()) == 1.0f);
    CHECK(bit::readF32LE(data.data() + 20) == 6.0f);
    auto back = decodeOk(TypeId::Ray, data, 1);
    CHECK(std::get<Ray>(back[0]).direction.z == 6.0f);
}

TEST_CASE("NumberRange is two plain little-endian floats") {
    // Spec: NumberRange(0, 0.5) and NumberRange(0.5, 1).
    std::vector<uint8_t> data{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f,
                              0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x80, 0x3f};
    auto values = decodeOk(TypeId::NumberRange, data, 2);
    CHECK(std::get<NumberRange>(values[0]).min == 0.0f);
    CHECK(std::get<NumberRange>(values[0]).max == 0.5f);
    CHECK(std::get<NumberRange>(values[1]).max == 1.0f);
    CHECK(encodeOk(TypeId::NumberRange, values) == data);
}

TEST_CASE("CFrame uses rotation id 0x02 for the identity orientation") {
    // Spec id table: 0x02 is (0,0,0), i.e. the identity rotation matrix.
    std::vector<Variant> values{CFrame{}};
    auto data = encodeOk(TypeId::CFrame, values);
    CHECK(data[0] == 0x02);              // id byte, no matrix follows
    CHECK(data.size() == 1 + 12);        // id + one interleaved Vector3
    auto back = decodeOk(TypeId::CFrame, data, 1);
    auto c = std::get<CFrame>(back[0]);
    CHECK(c.rotation[0] == 1.0f);
    CHECK(c.rotation[4] == 1.0f);
    CHECK(c.rotation[8] == 1.0f);
    CHECK(c.rotation[1] == 0.0f);
}

TEST_CASE("CFrame writes a full matrix when the rotation is not a special case") {
    CFrame tilted;
    const float m[9] = {0.5f, -0.5f, 0.7f, 0.7f, 0.5f, -0.5f, 0.1f, 0.8f, 0.6f};
    for (int i = 0; i < 9; ++i) tilted.rotation[i] = m[i];
    tilted.position = {4, 5, 6};
    auto data = encodeOk(TypeId::CFrame, {tilted});
    CHECK(data[0] == 0x00);              // id 0 means a matrix follows
    CHECK(data.size() == 1 + 36 + 12);
    auto back = decodeOk(TypeId::CFrame, data, 1);
    auto c = std::get<CFrame>(back[0]);
    for (int i = 0; i < 9; ++i) CHECK(c.rotation[i] == m[i]);
    CHECK(c.position.x == 4.0f);
}

TEST_CASE("CFrame positions are stored as one Vector3 array after all id bytes") {
    CFrame a; a.position = {1, 2, 3};
    CFrame b; b.position = {4, 5, 6};
    auto data = encodeOk(TypeId::CFrame, {a, b});
    // Two identity ids, then a single interleaved Vector3 array of two values.
    CHECK(data[0] == 0x02);
    CHECK(data[1] == 0x02);
    CHECK(data.size() == 2 + 24);
    auto back = decodeOk(TypeId::CFrame, data, 2);
    CHECK(std::get<CFrame>(back[1]).position.z == 6.0f);
}

TEST_CASE("NumberSequence stores a count then keypoint triples") {
    NumberSequence seq;
    seq.keypoints = {{0.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.5f}};
    auto data = encodeOk(TypeId::NumberSequence, {seq});
    CHECK(bit::readU32LE(data.data()) == 3u);
    CHECK(data.size() == 4 + 3 * 12);
    auto back = decodeOk(TypeId::NumberSequence, data, 1);
    auto s = std::get<NumberSequence>(back[0]);
    REQUIRE(s.keypoints.size() == 3);
    CHECK(s.keypoints[2].envelope == 0.5f);
}

TEST_CASE("ColorSequence keypoints carry a trailing unused envelope float") {
    ColorSequence seq;
    seq.keypoints = {{0.0f, Color3{1, 1, 1}, 0.0f}, {1.0f, Color3{0, 0, 0}, 0.0f}};
    auto data = encodeOk(TypeId::ColorSequence, {seq});
    CHECK(bit::readU32LE(data.data()) == 2u);
    CHECK(data.size() == 4 + 2 * 20);   // 5 floats per keypoint
    auto back = decodeOk(TypeId::ColorSequence, data, 1);
    CHECK(std::get<ColorSequence>(back[0]).keypoints.size() == 2);
}

TEST_CASE("PhysicalProperties encodes its flag bits per the spec") {
    // Spec worked example, four values.
    std::vector<uint8_t> data{
        0x00,
        0x01, 0x33, 0x33, 0x33, 0x3f, 0x9a, 0x99, 0x99, 0x3e,
              0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3f,
        0x02,
        0x03, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x3e,
              0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x80, 0x3e, 0x00, 0x00, 0x00, 0x3f};
    auto values = decodeOk(TypeId::PhysicalProperties, data, 4);
    auto v0 = std::get<PhysicalProperties>(values[0]);
    CHECK_FALSE(v0.custom);
    CHECK_FALSE(v0.hasAcousticAbsorption);

    auto v1 = std::get<PhysicalProperties>(values[1]);
    CHECK(v1.custom);
    CHECK_FALSE(v1.hasAcousticAbsorption);
    CHECK(v1.density == doctest::Approx(0.7f));
    CHECK(v1.acousticAbsorption == 1.0f);   // default when the bit is clear

    auto v2 = std::get<PhysicalProperties>(values[2]);
    CHECK_FALSE(v2.custom);
    CHECK(v2.hasAcousticAbsorption);

    auto v3 = std::get<PhysicalProperties>(values[3]);
    CHECK(v3.custom);
    CHECK(v3.hasAcousticAbsorption);
    CHECK(v3.density == doctest::Approx(0.25f));
    CHECK(v3.acousticAbsorption == doctest::Approx(0.5f));

    CHECK(encodeOk(TypeId::PhysicalProperties, values) == data);
}

TEST_CASE("OptionalCFrame nests a CFrame array and a Bool array") {
    // Spec worked example: one present identity-ish CFrame, one absent.
    OptionalCFrame present; present.hasValue = true;
    OptionalCFrame absent;  absent.hasValue = false;
    auto data = encodeOk(TypeId::OptionalCFrame, {present, absent});
    CHECK(data[0] == 0x10);                       // inner CFrame type id
    CHECK(data[data.size() - 3] == 0x02);         // inner Bool type id
    CHECK(data[data.size() - 2] == 0x01);         // first value present
    CHECK(data[data.size() - 1] == 0x00);         // second value absent
    auto back = decodeOk(TypeId::OptionalCFrame, data, 2);
    CHECK(std::get<OptionalCFrame>(back[0]).hasValue);
    CHECK_FALSE(std::get<OptionalCFrame>(back[1]).hasValue);
}

TEST_CASE("UniqueId records are 16 bytes interleaved at width 16") {
    std::vector<Variant> values{UniqueId{1, 2, 3}, UniqueId{4, 5, 6}};
    auto data = encodeOk(TypeId::UniqueId, values);
    CHECK(data.size() == 32);
    auto back = decodeOk(TypeId::UniqueId, data, 2);
    CHECK(std::get<UniqueId>(back[0]).index == 1u);
    CHECK(std::get<UniqueId>(back[0]).time == 2u);
    CHECK(std::get<UniqueId>(back[0]).random == 3);
    CHECK(std::get<UniqueId>(back[1]).random == 6);
}

TEST_CASE("Font stores family, weight, style, and cached face id") {
    Font f; f.family = "rbxasset://fonts/families/Arial.json";
    f.weight = 700; f.style = 1; f.cachedFaceId = "";
    auto data = encodeOk(TypeId::Font, {f});
    auto back = decodeOk(TypeId::Font, data, 1);
    auto g = std::get<Font>(back[0]);
    CHECK(g.family == f.family);
    CHECK(g.weight == 700);
    CHECK(g.style == 1);
    CHECK(g.cachedFaceId.empty());
}

TEST_CASE("Content stores a source-type array then counted sections") {
    Content none;
    Content uri;  uri.sourceType = Content::SourceType::Uri; uri.uri = "rbxassetid://123";
    Content obj;  obj.sourceType = Content::SourceType::Object; obj.object = 7;
    auto data = encodeOk(TypeId::Content, {none, uri, obj});
    auto back = decodeOk(TypeId::Content, data, 3);
    CHECK(std::get<Content>(back[0]).sourceType == Content::SourceType::None);
    CHECK(std::get<Content>(back[1]).sourceType == Content::SourceType::Uri);
    CHECK(std::get<Content>(back[1]).uri == "rbxassetid://123");
    CHECK(std::get<Content>(back[2]).sourceType == Content::SourceType::Object);
    CHECK(std::get<Content>(back[2]).object == 7u);
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: every new case fails with `UnsupportedType`.

- [ ] **Step 3: Implement `src/binary/valuecodec_struct.cpp`**

Notes on the cases that are easy to get wrong:

- **Component arrays.** Write one helper pair, `readFloat32Array(Cursor&, size_t count)` and `writeFloat32Array(const std::vector<float>&, std::vector<uint8_t>&)`, plus the `Int32` equivalents, and build every composite type from them. Do not hand-roll interleaving per type.
- **CFrame.** Encoding must check whether the rotation matrix matches one of the 24 special orientations (Appendix A.2) and emit the corresponding id byte when it does. Compare exactly, not approximately: emit an id only when the matrix elements are exactly `-1`, `0`, or `1` in the right pattern, otherwise write id `0x00` plus the matrix. The matrix floats are **plain IEEE-754, not the Roblox float format**, while the positions that follow *are* a normal interleaved `Vector3` array.
- **OptionalCFrame.** The layout is literally: byte `0x10`, then the CFrame array for all values, then byte `0x02`, then the Bool array. Absent values still occupy a slot in the CFrame array and are written as the identity CFrame.
- **UniqueId.** Each record is `index` as big-endian `u32`, `time` as big-endian `u32`, then `random` as a big-endian `i64` **rotated right by one bit** on read and rotated left by one on write. Build all records into a flat `count * 16` buffer, then interleave at width 16.
- **PhysicalProperties.** Bit 0 means custom (6 or 5 floats follow), bit 1 means `AcousticAbsorption` is present. Bit 1 set with bit 0 clear means no floats at all. When bit 0 is set and bit 1 is clear, only five floats follow and `acousticAbsorption` defaults to `1.0`.
- **Content.** The source-type array is a full `Enum` array (interleaved big-endian `u32`), and the three sections that follow are each a `u32` count plus that many entries. Object and external-object referents use the same accumulated referent encoding as type `0x13`. `ExternalObjectRefs` cannot be meaningful across files: decode and discard them, and always write a count of `0`.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/binary/valuecodec_struct.cpp tests/test_valuecodec_struct.cpp src/binary/valuecodec.hpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add binary codecs for composite property types"
```

---

## Task 8: Binary decoder (chunk stream to `Dom`)

**Files:**
- Create: `src/binary/decode.hpp`, `src/binary/decode.cpp`
- Create: `tests/test_binary_decode.cpp`
- Modify: `include/rbxl/dom.hpp` (add `className` to `RawChunk`), `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `readFileHeader`, `readChunk` (Task 5); `decodeValueArray`, `isKnownTypeId` (Tasks 6-7); `Dom` (Task 4).
- Produces: `Result<Dom> rbxl::binary::decode(const uint8_t* data, size_t size);`

**Modify `RawChunk`** to carry the owning class so the encoder can re-target it:

```cpp
struct RawChunk {
    char name[4] = {0, 0, 0, 0};
    std::string className;   // set for preserved PROP chunks; empty otherwise
    std::vector<uint8_t> data;
};
```

**Decoding order.** The decoder makes a single forward pass and handles chunks as it meets them:

1. `META` — append each key/value pair to `dom.metadata()`.
2. `SSTR` — read `version` (`u32`, must be 0), `count` (`u32`), then `count` records of 16 raw key bytes plus a length-prefixed string. Store as `SharedString{key, value}` in a local table handed to the codecs via `CodecContext`.
3. `INST` — read `classId`, `className`, `objectFormat`, `instanceCount`, the accumulated referent array, and the service markers when `objectFormat == 1`. Create that many instances in the `Dom`, record `classId -> {className, vector<InstanceId>}`, and record `fileReferent -> InstanceId` in the referent map.
4. `PROP` — read `classId`, `propertyName`, `typeId`. If `typeId` is unknown, store the whole decompressed payload as a `RawChunk` named `PROP` with `className` set, and continue. Otherwise decode `instanceCount` values and assign them positionally to that class's instances.
5. `PRNT` — read `version` (must be 0), `count`, then the child and parent referent arrays, and call `dom.setParent` for each pair in file order.
6. `END` — stop.
7. Anything else — store as a `RawChunk` and continue.

Then, as a final pass, walk every property of every instance and rewrite `Ref` and `Content`-object values from file referents to `InstanceId`s. Deferring this is necessary because a `Ref` may point at an instance whose `INST` chunk has not been read yet.

- [ ] **Step 1: Write the failing test**

`tests/test_binary_decode.cpp`. Build a minimal file in memory rather than depending on the corpus, then add corpus cases.

```cpp
#include <doctest.h>
#include "binary/decode.hpp"
#include "binary/chunk.hpp"
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

// Helpers for assembling chunk payloads by hand.
static void putU32(std::vector<uint8_t>& v, uint32_t x) {
    v.resize(v.size() + 4);
    bit::writeU32LE(v.data() + v.size() - 4, x);
}
static void putString(std::vector<uint8_t>& v, const std::string& s) {
    putU32(v, static_cast<uint32_t>(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}
static void putRefArray(std::vector<uint8_t>& v, const std::vector<int32_t>& refs) {
    std::vector<uint8_t> flat(refs.size() * 4), woven(refs.size() * 4);
    int32_t prev = 0;
    for (size_t i = 0; i < refs.size(); ++i) {
        bit::writeU32BE(flat.data() + i * 4, bit::zigzagEncode32(refs[i] - prev));
        prev = refs[i];
    }
    bit::interleave(flat.data(), woven.data(), refs.size(), 4);
    v.insert(v.end(), woven.begin(), woven.end());
}

// A file with one Folder named "Root" and one Part named "Child" parented to it.
static std::vector<uint8_t> buildMinimalFile() {
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 2; header.instanceCount = 2;
    writeFileHeader(file, header);

    std::vector<uint8_t> inst0;
    putU32(inst0, 0); putString(inst0, "Folder"); inst0.push_back(0);
    putU32(inst0, 1); putRefArray(inst0, {0});
    REQUIRE(writeChunk(file, "INST", inst0, Compression::None, 0));

    std::vector<uint8_t> inst1;
    putU32(inst1, 1); putString(inst1, "Part"); inst1.push_back(0);
    putU32(inst1, 1); putRefArray(inst1, {1});
    REQUIRE(writeChunk(file, "INST", inst1, Compression::None, 0));

    std::vector<uint8_t> prop0;
    putU32(prop0, 0); putString(prop0, "Name"); prop0.push_back(0x01);
    putString(prop0, "Root");
    REQUIRE(writeChunk(file, "PROP", prop0, Compression::None, 0));

    std::vector<uint8_t> prop1;
    putU32(prop1, 1); putString(prop1, "Name"); prop1.push_back(0x01);
    putString(prop1, "Child");
    REQUIRE(writeChunk(file, "PROP", prop1, Compression::None, 0));

    std::vector<uint8_t> prnt;
    prnt.push_back(0); putU32(prnt, 2);
    putRefArray(prnt, {1, 0});     // children
    putRefArray(prnt, {0, -1});    // parents
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));

    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));
    return file;
}

TEST_CASE("decoder builds instances, properties, and hierarchy") {
    auto file = buildMinimalFile();
    auto result = decode(file.data(), file.size());
    REQUIRE(result);
    Dom& dom = result.value();

    REQUIRE(dom.instanceCount() == 2);
    REQUIRE(dom.roots().size() == 1);
    InstanceId root = dom.roots()[0];
    CHECK(dom.at(root).className == "Folder");
    CHECK(dom.nameOf(root) == "Root");
    REQUIRE(dom.at(root).children.size() == 1);
    InstanceId child = dom.at(root).children[0];
    CHECK(dom.at(child).className == "Part");
    CHECK(dom.nameOf(child) == "Child");
}

TEST_CASE("service markers set isService") {
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 1; header.instanceCount = 1;
    writeFileHeader(file, header);
    std::vector<uint8_t> inst;
    putU32(inst, 0); putString(inst, "Workspace"); inst.push_back(1);   // service
    putU32(inst, 1); putRefArray(inst, {0}); inst.push_back(1);          // marker
    REQUIRE(writeChunk(file, "INST", inst, Compression::None, 0));
    std::vector<uint8_t> prnt; prnt.push_back(0); putU32(prnt, 1);
    putRefArray(prnt, {0}); putRefArray(prnt, {-1});
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));
    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));

    auto result = decode(file.data(), file.size());
    REQUIRE(result);
    CHECK(result.value().at(result.value().roots()[0]).isService);
}

TEST_CASE("unknown property type ids are preserved verbatim") {
    auto file = buildMinimalFile();
    // Rewrite the second PROP chunk's type id byte to an unallocated value.
    // Locate it by scanning for the "Name" string that follows the class id.
    bool patched = false;
    for (size_t i = 0; i + 12 < file.size(); ++i) {
        if (std::memcmp(file.data() + i, "Name", 4) == 0 && file[i + 4] == 0x01) {
            file[i + 4] = 0x7F;   // unknown type id
            patched = true;
            break;
        }
    }
    REQUIRE(patched);

    auto result = decode(file.data(), file.size());
    REQUIRE(result);          // must not fail
    Dom& dom = result.value();
    REQUIRE(dom.unknownChunks().size() == 1);
    CHECK(std::memcmp(dom.unknownChunks()[0].name, "PROP", 4) == 0);
    CHECK_FALSE(dom.unknownChunks()[0].className.empty());
}

TEST_CASE("unrecognised chunk names are preserved verbatim") {
    auto file = buildMinimalFile();
    // Insert a chunk with a name no decoder knows, just before END.
    std::vector<uint8_t> prefix(file.begin(), file.end() - (16 + 9));
    std::vector<uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
    REQUIRE(writeChunk(prefix, "XXXX", payload, Compression::None, 0));
    prefix.insert(prefix.end(), file.end() - (16 + 9), file.end());

    auto result = decode(prefix.data(), prefix.size());
    REQUIRE(result);
    auto& unknown = result.value().unknownChunks();
    REQUIRE(unknown.size() == 1);
    CHECK(std::memcmp(unknown[0].name, "XXXX", 4) == 0);
    CHECK(unknown[0].data == payload);
}

TEST_CASE("Ref properties resolve to instance ids after all INST chunks") {
    // ObjectValue.Value pointing forward at an instance declared later.
    std::vector<uint8_t> file;
    FileHeader header; header.classCount = 2; header.instanceCount = 2;
    writeFileHeader(file, header);
    std::vector<uint8_t> inst0;
    putU32(inst0, 0); putString(inst0, "ObjectValue"); inst0.push_back(0);
    putU32(inst0, 1); putRefArray(inst0, {0});
    REQUIRE(writeChunk(file, "INST", inst0, Compression::None, 0));
    std::vector<uint8_t> inst1;
    putU32(inst1, 1); putString(inst1, "Part"); inst1.push_back(0);
    putU32(inst1, 1); putRefArray(inst1, {1});
    REQUIRE(writeChunk(file, "INST", inst1, Compression::None, 0));
    std::vector<uint8_t> prop;
    putU32(prop, 0); putString(prop, "Value"); prop.push_back(0x13);
    putRefArray(prop, {1});
    REQUIRE(writeChunk(file, "PROP", prop, Compression::None, 0));
    std::vector<uint8_t> prnt; prnt.push_back(0); putU32(prnt, 2);
    putRefArray(prnt, {0, 1}); putRefArray(prnt, {-1, -1});
    REQUIRE(writeChunk(file, "PRNT", prnt, Compression::None, 0));
    std::vector<uint8_t> end{'<', '/', 'r', 'o', 'b', 'l', 'o', 'x', '>'};
    REQUIRE(writeChunk(file, "END\0", end, Compression::None, 0));

    auto result = decode(file.data(), file.size());
    REQUIRE(result);
    Dom& dom = result.value();
    const Variant* v = dom.getProperty(0, "Value");
    REQUIRE(v != nullptr);
    InstanceId target = std::get<Ref>(*v).target;
    REQUIRE(dom.valid(target));
    CHECK(dom.at(target).className == "Part");
}

TEST_CASE("corpus: real places decode fully") {
    struct Expect { const char* file; uint32_t classes; uint32_t instances; };
    const Expect cases[] = {
        {"Bladeborne Assets.rbxl", 111, 9280},
        {"Bladeborne Floor 0.rbxl", 144, 6475},
        {"Bladeborne Floor 1.rbxl", 150, 152408},
        {"FusionCore.rbxl", 143, 104184},
        {"RaceAPet.rbxl", 162, 734657},
    };
    for (const auto& c : cases) {
        std::ifstream in(std::string(RBXL_TEST_DATA_DIR) + "/" + c.file, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        CAPTURE(c.file);
        auto result = decode(data.data(), data.size());
        REQUIRE_MESSAGE(result, result.hasValue() ? "" : result.error().toString());
        CHECK(result.value().instanceCount() == c.instances);
        CHECK(result.value().roots().size() > 0);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL, `binary/decode.hpp: No such file or directory`.

- [ ] **Step 3: Implement `src/binary/decode.cpp`**

Additional notes:
- Reserve `dom` capacity from the header's instance count, and reserve the referent map likewise. This is the difference between a fast load and a quadratic one on the 734k-instance file.
- Trust the header's counts only as hints for reservation. Validate everything against actual chunk contents; a corrupt count must not cause an over-read.
- A `PROP` chunk naming a `classId` with no preceding `INST` chunk is `ErrorCode::Malformed`.
- A `PRNT` referent that is not in the referent map is `ErrorCode::Malformed`. A parent referent of `-1` means root.
- Reject a second `PRNT` chunk; the spec allows exactly one.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS. The corpus case must report the exact instance counts above; a mismatch means the referent or INST handling is wrong.

- [ ] **Step 5: Commit**

```bash
git add src/binary/decode.hpp src/binary/decode.cpp tests/test_binary_decode.cpp include/rbxl/dom.hpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: decode binary rbxl and rbxm files into a Dom"
```

---

## Task 9: Binary encoder (`Dom` to chunk stream)

**Files:**
- Create: `src/binary/encode.hpp`, `src/binary/encode.cpp`
- Create: `tests/test_binary_encode.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Dom`, `writeFileHeader`, `writeChunk`, `encodeValueArray`.
- Produces:
  ```cpp
  namespace rbxl::binary {
  struct EncodeOptions {
      rbxl::Compression compression = rbxl::Compression::Zstd;
      int level = 3;
      const ReflectionDatabase* reflection = nullptr;   // forward-declared; may be null
  };
  struct EncodeDiagnostics { std::vector<std::string> warnings; };
  Result<std::vector<uint8_t>> encode(const Dom&, const EncodeOptions& = {},
                                      EncodeDiagnostics* diagnostics = nullptr);
  }
  ```

`ReflectionDatabase` is forward-declared here; Task 14 defines it. Until then the pointer is always null and the default-value path is exercised.

**Encoding order:**

1. Group instances by `(className, isService)`. Assign class ids in order of first appearance, which keeps output stable across runs for the same `Dom`.
2. Assign file referents. Use the instance's index in the `Dom` pool, which is dense and already unique.
3. Collect every `SharedString` and `NetAssetRef` value into the `SSTR` table, deduplicating by value. Emit `SSTR` before any `PROP` chunk.
4. Write `META` if `dom.metadata()` is non-empty.
5. Write one `INST` chunk per class.
6. Write `PROP` chunks: for each class, take the union of property names across its instances, sorted by name for determinism. Emit one chunk per name.
7. Re-emit preserved `RawChunk`s (see the rule below).
8. Write `PRNT` using `dom.postOrder()`, which already produces the order Roblox Studio writes.
9. Write `END` uncompressed.

**The heterogeneous-property rule.** The format requires every instance of a class to define the same properties. When a hand-built `Dom` violates this, the encoder must reconcile rather than fail:
- Determine the property's type from the first instance that has a non-`Nil` value.
- Instances missing that property get a zero-initialised value of that type, or the reflection database's default when one is supplied.
- Instances holding a *different* type for the same name are coerced when the conversion is lossless (`Int32` to `Int64`, `Color3uint8` to `Color3`) and otherwise replaced with the default, with a warning pushed onto `EncodeDiagnostics`.

**The preserved-chunk rule.** A `RawChunk` named `PROP` is re-emitted only when its `className` still exists and that class still has exactly the instance count it had at load time, because the chunk's value array is positional and its embedded referents cannot be remapped. Patch the chunk's first four bytes with the newly assigned class id before writing. When the count no longer matches, drop the chunk and push a warning. Chunks with other names are re-emitted unchanged.

- [ ] **Step 1: Write the failing test**

`tests/test_binary_encode.cpp`:

```cpp
#include <doctest.h>
#include "binary/encode.hpp"
#include "binary/decode.hpp"
#include <cstring>
#include <fstream>
#include <string>

using namespace rbxl;
using namespace rbxl::binary;

static Dom buildSample() {
    Dom dom;
    auto model = dom.create("Model");
    dom.setProperty(model, "Name", std::string("Rig"));
    auto part = dom.create("Part");
    dom.setProperty(part, "Name", std::string("Head"));
    dom.setProperty(part, "Size", Vector3{2, 1, 1});
    dom.setProperty(part, "Anchored", true);
    dom.setParent(part, model);
    return dom;
}

TEST_CASE("encoded output is a well-formed file that decodes back") {
    Dom dom = buildSample();
    auto encoded = encode(dom);
    REQUIRE(encoded);
    CHECK(std::memcmp(encoded.value().data(), "<roblox!", 8) == 0);
    // The END magic must be the last nine bytes and uncompressed.
    CHECK(std::memcmp(encoded.value().data() + encoded.value().size() - 9, "</roblox>", 9) == 0);

    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    Dom& d = back.value();
    REQUIRE(d.instanceCount() == 2);
    REQUIRE(d.roots().size() == 1);
    InstanceId model = d.roots()[0];
    CHECK(d.at(model).className == "Model");
    CHECK(d.nameOf(model) == "Rig");
    REQUIRE(d.at(model).children.size() == 1);
    InstanceId part = d.at(model).children[0];
    CHECK(d.nameOf(part) == "Head");
    CHECK(std::get<Vector3>(*d.getProperty(part, "Size")).y == 1.0f);
    CHECK(std::get<bool>(*d.getProperty(part, "Anchored")));
}

TEST_CASE("all three compression modes produce decodable files") {
    Dom dom = buildSample();
    for (auto mode : {Compression::None, Compression::Lz4, Compression::Zstd}) {
        EncodeOptions options; options.compression = mode;
        auto encoded = encode(dom, options);
        REQUIRE(encoded);
        auto back = decode(encoded.value().data(), encoded.value().size());
        REQUIRE(back);
        CHECK(back.value().instanceCount() == 2);
    }
}

TEST_CASE("Ref properties survive re-encoding") {
    Dom dom;
    auto holder = dom.create("ObjectValue");
    auto target = dom.create("Part");
    dom.setProperty(holder, "Value", Ref{target});
    auto encoded = encode(dom);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    const Variant* v = back.value().getProperty(0, "Value");
    REQUIRE(v != nullptr);
    CHECK(back.value().at(std::get<Ref>(*v).target).className == "Part");
}

TEST_CASE("shared strings are pooled and deduplicated") {
    Dom dom;
    SharedString shared{std::string(16, '\0'), "a large repeated payload"};
    for (int i = 0; i < 3; ++i) {
        auto id = dom.create("MeshPart");
        dom.setProperty(id, "PhysicalConfigData", shared);
    }
    auto encoded = encode(dom);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    for (InstanceId id = 0; id < 3; ++id) {
        const Variant* v = back.value().getProperty(id, "PhysicalConfigData");
        REQUIRE(v != nullptr);
        CHECK(std::get<SharedString>(*v).value == "a large repeated payload");
    }
}

TEST_CASE("services keep their service marker through a round-trip") {
    Dom dom;
    auto ws = dom.create("Workspace");
    dom.at(ws).isService = true;
    auto encoded = encode(dom);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(back.value().at(0).isService);
}

TEST_CASE("instances of one class with differing properties are reconciled") {
    Dom dom;
    auto a = dom.create("Part");
    dom.setProperty(a, "Transparency", 0.5f);
    auto b = dom.create("Part");        // no Transparency at all

    EncodeDiagnostics diags;
    auto encoded = encode(dom, {}, &diags);
    REQUIRE(encoded);
    auto back = decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    // Both instances must now carry the property; the filled one gets the default.
    CHECK(std::get<float>(*back.value().getProperty(0, "Transparency")) == 0.5f);
    CHECK(std::get<float>(*back.value().getProperty(1, "Transparency")) == 0.0f);
}

TEST_CASE("PRNT is written in depth-first post-order") {
    Dom dom;
    auto root = dom.create("Folder");
    auto mid = dom.create("Folder");
    auto leaf = dom.create("Folder");
    dom.setParent(mid, root);
    dom.setParent(leaf, mid);
    auto encoded = encode(dom, {Compression::None, 0, nullptr});
    REQUIRE(encoded);
    // Find the PRNT chunk and confirm the leaf's referent comes first.
    // (Locating it by scanning for the literal name is sufficient here.)
    const auto& bytes = encoded.value();
    size_t at = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())
                    .find("PRNT");
    REQUIRE(at != std::string::npos);
    const uint8_t* payload = bytes.data() + at + kChunkHeaderSize;
    CHECK(payload[0] == 0);                        // version
    CHECK(bit::readU32LE(payload + 1) == 3u);      // instance count
    // First child referent, after deinterleaving a 3-element i32 array.
    uint8_t flat[12];
    bit::deinterleave(payload + 5, flat, 3, 4);
    CHECK(bit::zigzagDecode32(bit::readU32BE(flat)) == static_cast<int32_t>(leaf));
}

TEST_CASE("corpus: decode then encode then decode is stable") {
    const char* names[] = {"Bladeborne Assets.rbxl", "Bladeborne Floor 0.rbxl",
                           "FusionCore.rbxl"};
    for (const char* name : names) {
        std::ifstream in(std::string(RBXL_TEST_DATA_DIR) + "/" + name, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        CAPTURE(name);
        auto first = decode(data.data(), data.size());
        REQUIRE(first);
        auto encoded = encode(first.value());
        REQUIRE(encoded);
        auto second = decode(encoded.value().data(), encoded.value().size());
        REQUIRE(second);
        CHECK(second.value().instanceCount() == first.value().instanceCount());
        CHECK(second.value().roots().size() == first.value().roots().size());
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL, `binary/encode.hpp: No such file or directory`.

- [ ] **Step 3: Implement `src/binary/encode.cpp`** following the ordering and the two reconciliation rules above.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
```
Expected: PASS.

- [ ] **Step 5: Verify against Roblox Studio (manual, one time)**

Encode `Bladeborne Assets.rbxl` through the CLI once Task 15 lands, or write a throwaway `main` now, and open the result in Roblox Studio. This is the only check that catches "structurally valid but Roblox refuses it" problems. Record the outcome in the commit message.

- [ ] **Step 6: Commit**

```bash
git add src/binary/encode.hpp src/binary/encode.cpp tests/test_binary_encode.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: encode a Dom to binary rbxl and rbxm files"
```

---

## Task 10: XML decoder (`.rbxlx` / `.rbxmx` to `Dom`)

**Files:**
- Create: `src/xml/base64.hpp`, `src/xml/base64.cpp`, `src/xml/decode.hpp`, `src/xml/decode.cpp`
- Create: `tests/test_base64.cpp`, `tests/test_xml_decode.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Dom`, `Variant`, pugixml.
- Produces:
  - `std::string rbxl::xml::base64Encode(const uint8_t*, size_t);`
  - `Result<std::vector<uint8_t>> rbxl::xml::base64Decode(const std::string&);`
  - `Result<Dom> rbxl::xml::decode(const char* data, size_t size);`

**Parsing setup.** Load with `pugi::xml_document::load_buffer_inplace_own` so the 153 MB place is not copied, and with parse flags `pugi::parse_default | pugi::parse_ws_pcdata_single`. That last flag is required: without it pugixml discards whitespace-only text nodes, and `ProtectedString` (script source) must keep its contents byte for byte. Skip any leading comment or processing instruction before `<roblox>` — real files in the wild begin with one.

**The element-name trap.** Element names do not reliably identify types. Three renames are standard (`BrickColor` serialises as `int`, `CFrame` as `CoordinateFrame`, `Enum` as `token`), and third-party writers use stale names: the sample place in `temp/` writes the legacy `ContentId` type under the element name `Content`. Disambiguate structurally, on the child element:

| Element | Child | Decoded as |
|:--|:--|:--|
| `Content` | `url` | `ContentId` (the legacy type) |
| `Content` | `uri`, `null`, or `Ref` | `Content` (release 645+) |
| `ContentId` | `url`, `null`, `binary`, `hash` | `ContentId`; `binary`/`hash` contents are discarded and the value treated as empty |

**Referents.** `Item/@referent` values are arbitrary strings (Roblox uses `RBX` plus a UUID). Map string to `InstanceId` in a first pass over all `Item` elements, then resolve `Ref` element contents in a second pass. `null` is reserved and means no value.

**Two deliberate behaviours to document:**
- `SharedString` keys are the `md5` attribute, base64-decoded into `SharedString::key`. The attribute is only an identifier and need not be a real MD5, so the library never computes one. When writing binary, a key that is not exactly 16 bytes is zero-padded or truncated. Roblox ignores the field, so this is lossless in practice but changes the key text across an XML to binary to XML trip.
- `UniqueId` XML layout is bytes 0-7 `Random` (`u64`), 8-11 `Time` (`u32`), 12-15 `Index` (`u32`), which is the **reverse field order** from the binary format. The specification further states the `Random` component is left-circular rotated by one bit in XML relative to binary. No file in `temp/` contains a `UniqueId`, so this rotation is the one rule in the project that cannot be validated against the corpus. Implement it as written, add a `// VERIFY:` comment at the call site, and confirm it by saving an instance with a `UniqueId` from Roblox Studio in both formats and comparing.

- [ ] **Step 1: Write `tests/test_base64.cpp`**

```cpp
#include <doctest.h>
#include "xml/base64.hpp"

using namespace rbxl::xml;

static std::string enc(const std::string& s) {
    return base64Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

TEST_CASE("base64 matches RFC 2045 test vectors") {
    CHECK(enc("") == "");
    CHECK(enc("f") == "Zg==");
    CHECK(enc("fo") == "Zm8=");
    CHECK(enc("foo") == "Zm9v");
    CHECK(enc("foob") == "Zm9vYg==");
    CHECK(enc("fooba") == "Zm9vYmE=");
    CHECK(enc("foobar") == "Zm9vYmFy");
    CHECK(enc("Rojo is cool!") == "Um9qbyBpcyBjb29sIQ==");
}

TEST_CASE("base64 decodes back, ignoring embedded whitespace") {
    auto r = base64Decode("Um9qbyBp\n  cyBjb29s IQ==");
    REQUIRE(r);
    CHECK(std::string(r.value().begin(), r.value().end()) == "Rojo is cool!");
}

TEST_CASE("base64 rejects invalid input") {
    CHECK_FALSE(base64Decode("!!!!"));
    CHECK_FALSE(base64Decode("Zg="));      // bad length
}

TEST_CASE("base64 handles arbitrary binary bytes") {
    std::vector<uint8_t> data;
    for (int i = 0; i < 256; ++i) data.push_back(static_cast<uint8_t>(i));
    auto text = base64Encode(data.data(), data.size());
    auto back = base64Decode(text);
    REQUIRE(back);
    CHECK(back.value() == data);
}
```

- [ ] **Step 2: Implement `base64.cpp`, run, and confirm those pass**

- [ ] **Step 3: Write `tests/test_xml_decode.cpp`**

```cpp
#include <doctest.h>
#include "xml/decode.hpp"
#include <cmath>
#include <fstream>
#include <string>

using namespace rbxl;

static Dom decodeOk(const std::string& xml) {
    auto r = rbxl::xml::decode(xml.data(), xml.size());
    REQUIRE_MESSAGE(r, r.hasValue() ? "" : r.error().toString());
    return std::move(r.value());
}

TEST_CASE("a leading comment before the root element is tolerated") {
    Dom dom = decodeOk(
        "<!-- Saved by something --><roblox version=\"4\">"
        "<Item class=\"Part\" referent=\"0\"><Properties>"
        "<string name=\"Name\">Baseplate</string></Properties></Item></roblox>");
    REQUIRE(dom.instanceCount() == 1);
    CHECK(dom.at(0).className == "Part");
    CHECK(dom.nameOf(0) == "Baseplate");
}

TEST_CASE("nested Items become a hierarchy") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Model\" referent=\"a\"><Properties/>"
        "<Item class=\"Part\" referent=\"b\"><Properties/></Item></Item></roblox>");
    REQUIRE(dom.instanceCount() == 2);
    REQUIRE(dom.roots().size() == 1);
    InstanceId root = dom.roots()[0];
    CHECK(dom.at(root).className == "Model");
    REQUIRE(dom.at(root).children.size() == 1);
    CHECK(dom.at(dom.at(root).children[0]).className == "Part");
}

TEST_CASE("scalar type elements decode") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<bool name=\"Anchored\">true</bool>"
        "<int name=\"Count\">-7</int>"
        "<int64 name=\"Big\">9000000000</int64>"
        "<float name=\"Transparency\">0.5</float>"
        "<double name=\"Time\">1.25</double>"
        "<token name=\"Material\">256</token>"
        "</Properties></Item></roblox>");
    CHECK(std::get<bool>(*dom.getProperty(0, "Anchored")));
    CHECK(std::get<int32_t>(*dom.getProperty(0, "Count")) == -7);
    CHECK(std::get<int64_t>(*dom.getProperty(0, "Big")) == 9000000000LL);
    CHECK(std::get<float>(*dom.getProperty(0, "Transparency")) == 0.5f);
    CHECK(std::get<double>(*dom.getProperty(0, "Time")) == 1.25);
    CHECK(std::get<EnumValue>(*dom.getProperty(0, "Material")).value == 256u);
}

TEST_CASE("float special values decode") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<float name=\"A\">INF</float><float name=\"B\">-INF</float>"
        "<float name=\"C\">NAN</float></Properties></Item></roblox>");
    CHECK(std::isinf(std::get<float>(*dom.getProperty(0, "A"))));
    CHECK(std::get<float>(*dom.getProperty(0, "B")) < 0);
    CHECK(std::isnan(std::get<float>(*dom.getProperty(0, "C"))));
}

TEST_CASE("composite type elements decode") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<Vector3 name=\"Size\"><X>4</X><Y>1</Y><Z>2</Z></Vector3>"
        "<UDim2 name=\"Pos\"><XS>0.5</XS><XO>10</XO><YS>0.25</YS><YO>-4</YO></UDim2>"
        "<CoordinateFrame name=\"CFrame\"><X>1</X><Y>2</Y><Z>3</Z>"
        "<R00>1</R00><R01>0</R01><R02>0</R02>"
        "<R10>0</R10><R11>1</R11><R12>0</R12>"
        "<R20>0</R20><R21>0</R21><R22>1</R22></CoordinateFrame>"
        "<Color3uint8 name=\"Color\">4294901760</Color3uint8>"
        "<NumberRange name=\"Range\">0 0.5</NumberRange>"
        "<NumberSequence name=\"Seq\">0 0 0 1 1 0 </NumberSequence>"
        "</Properties></Item></roblox>");
    CHECK(std::get<Vector3>(*dom.getProperty(0, "Size")).x == 4.0f);
    auto udim2 = std::get<UDim2>(*dom.getProperty(0, "Pos"));
    CHECK(udim2.x.scale == 0.5f);
    CHECK(udim2.x.offset == 10);
    CHECK(udim2.y.offset == -4);
    auto cf = std::get<CFrame>(*dom.getProperty(0, "CFrame"));
    CHECK(cf.position.z == 3.0f);
    CHECK(cf.rotation[0] == 1.0f);
    // 4294901760 == 0xFFFF0000: alpha FF, red FF, green 00, blue 00.
    auto color = std::get<Color3uint8>(*dom.getProperty(0, "Color"));
    CHECK(color.r == 255); CHECK(color.g == 0); CHECK(color.b == 0);
    CHECK(std::get<NumberRange>(*dom.getProperty(0, "Range")).max == 0.5f);
    CHECK(std::get<NumberSequence>(*dom.getProperty(0, "Seq")).keypoints.size() == 2);
}

TEST_CASE("Content is distinguished from ContentId by its child element") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        // Legacy shape, written under the modern element name by third-party tools.
        "<Content name=\"AnimationId\"><url>rbxassetid://123</url></Content>"
        // Modern shape.
        "<Content name=\"Image\"><uri>rbxassetid://456</uri></Content>"
        "<Content name=\"Empty\"><null></null></Content>"
        "</Properties></Item></roblox>");
    const Variant* legacy = dom.getProperty(0, "AnimationId");
    REQUIRE(legacy);
    CHECK(variantTypeOf(*legacy) == VariantType::ContentId);
    CHECK(std::get<ContentId>(*legacy).url == "rbxassetid://123");

    const Variant* modern = dom.getProperty(0, "Image");
    REQUIRE(modern);
    CHECK(variantTypeOf(*modern) == VariantType::Content);
    CHECK(std::get<Content>(*modern).sourceType == Content::SourceType::Uri);

    CHECK(std::get<Content>(*dom.getProperty(0, "Empty")).sourceType ==
          Content::SourceType::None);
}

TEST_CASE("ProtectedString keeps whitespace and CDATA contents exactly") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Script\" referent=\"0\"><Properties>"
        "<ProtectedString name=\"Source\"><![CDATA[local x = 1\n\n  print(x)\n]]>"
        "</ProtectedString></Properties></Item></roblox>");
    CHECK(std::get<ProtectedString>(*dom.getProperty(0, "Source")).value ==
          "local x = 1\n\n  print(x)\n");
}

TEST_CASE("Ref elements resolve, and null means no value") {
    Dom dom = decodeOk(
        "<roblox version=\"4\">"
        "<Item class=\"ObjectValue\" referent=\"RBXA\"><Properties>"
        "<Ref name=\"Value\">RBXB</Ref></Properties></Item>"
        "<Item class=\"ObjectValue\" referent=\"RBXB\"><Properties>"
        "<Ref name=\"Value\">null</Ref></Properties></Item></roblox>");
    InstanceId target = std::get<Ref>(*dom.getProperty(0, "Value")).target;
    REQUIRE(dom.valid(target));
    CHECK(dom.at(target).className == "ObjectValue");
    CHECK(std::get<Ref>(*dom.getProperty(1, "Value")).target == kNoInstance);
}

TEST_CASE("SharedStrings resolve through the file's table") {
    Dom dom = decodeOk(
        "<roblox version=\"4\">"
        "<Item class=\"Part\" referent=\"0\"><Properties>"
        "<SharedString name=\"Blob\">a2V5</SharedString></Properties></Item>"
        "<SharedStrings><SharedString md5=\"a2V5\">Zm9vYmFy</SharedString></SharedStrings>"
        "</roblox>");
    CHECK(std::get<SharedString>(*dom.getProperty(0, "Blob")).value == "foobar");
}

TEST_CASE("BinaryString contents are base64-decoded") {
    Dom dom = decodeOk(
        "<roblox version=\"4\"><Item class=\"Part\" referent=\"0\"><Properties>"
        "<BinaryString name=\"Tags\">Um9qbyBpcyBjb29sIQ==</BinaryString>"
        "</Properties></Item></roblox>");
    const auto& data = std::get<BinaryString>(*dom.getProperty(0, "Tags")).data;
    CHECK(std::string(data.begin(), data.end()) == "Rojo is cool!");
}

TEST_CASE("Meta elements land in the Dom metadata") {
    Dom dom = decodeOk("<roblox version=\"4\">"
                       "<Meta name=\"ExplicitAutoJoints\">true</Meta></roblox>");
    REQUIRE(dom.metadata().size() == 1);
    CHECK(dom.metadata()[0].first == "ExplicitAutoJoints");
    CHECK(dom.metadata()[0].second == "true");
}

TEST_CASE("a missing or wrong root element is an error") {
    auto r = rbxl::xml::decode("<nope/>", 7);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Malformed);
}

TEST_CASE("corpus: the large XML place decodes") {
    std::ifstream in(std::string(RBXL_TEST_DATA_DIR) +
                     "/place 101949297449238 Build An Island.rbxlx", std::ios::binary);
    if (!in) return;   // corpus not present
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto r = rbxl::xml::decode(data.data(), data.size());
    REQUIRE_MESSAGE(r, r.hasValue() ? "" : r.error().toString());
    Dom& dom = r.value();
    CHECK(dom.instanceCount() > 1000);
    // This file is the project's gate on modern types; all four must appear.
    bool sawContentId = false, sawFont = false, sawSecurity = false, sawOptionalCFrame = false;
    for (InstanceId id = 0; id < dom.instanceCount(); ++id) {
        for (const auto& prop : dom.at(id).properties) {
            switch (variantTypeOf(prop.second)) {
                case VariantType::ContentId: sawContentId = true; break;
                case VariantType::Font: sawFont = true; break;
                case VariantType::SecurityCapabilities: sawSecurity = true; break;
                case VariantType::OptionalCFrame: sawOptionalCFrame = true; break;
                default: break;
            }
        }
    }
    CHECK(sawContentId);
    CHECK(sawFont);
    CHECK(sawSecurity);
    CHECK(sawOptionalCFrame);
}
```

- [ ] **Step 4: Run to verify it fails, then implement `src/xml/decode.cpp`**

Build the element-name to type mapping as a `static const std::unordered_map<std::string_view, VariantType>` from Appendix A.3, with `Content` routed through the structural check above. Parse floats with `std::strtof`/`std::strtod` after handling `INF`, `-INF`, and `NAN` explicitly, since those spellings are not what the C library accepts by default.

- [ ] **Step 5: Run the tests**

Expected: PASS, including all four modern-type flags on the corpus file.

- [ ] **Step 6: Commit**

```bash
git add src/xml tests/test_base64.cpp tests/test_xml_decode.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: decode rbxlx and rbxmx files into a Dom"
```

---

## Task 11: XML encoder (`Dom` to `.rbxlx` / `.rbxmx`)

**Files:**
- Create: `src/xml/encode.hpp`, `src/xml/encode.cpp`
- Create: `tests/test_xml_encode.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Dom`, pugixml, `base64Encode`.
- Produces: `Result<std::string> rbxl::xml::encode(const Dom&, bool pretty = true);`

**Rules:**
- Root is `<roblox version="4">`. Emit `<Meta>` elements first, then `<Item>` elements in `Dom` root order, then `<SharedStrings>` last.
- Referents are the instance's `InstanceId` rendered as `RBX` plus its decimal value, which is unique by construction. A `Ref` to `kNoInstance` writes the literal `null`.
- Write the canonical element name for each type from Appendix A.3. Always write `CoordinateFrame` for `CFrame`, `token` for enums, `Ref` for referents, and `Rect2D` for rects, because Roblox's reader keys off these names for some properties.
- `ContentId` writes a `<url>` child; the modern `Content` writes `<uri>`, `<null>`, or `<Ref>` per its source type.
- `ProtectedString` contents go in a CDATA section. If the value itself contains `]]>`, fall back to normal escaping rather than producing a malformed document.
- `Font` omits `<CachedFaceId>` when it is empty, and writes `<Style>` as the *name* (`Normal` / `Italic`) while `<Weight>` stays numeric. This asymmetry is real and required.
- Floats print with enough digits to round-trip exactly: use `%.9g` for `float` and `%.17g` for `double`, and emit `INF`, `-INF`, `NAN` for the non-finite cases.
- `Dom::unknownChunks()` has no XML representation. Drop it and, when non-empty, report it through the diagnostics channel so `convert` can warn.

- [ ] **Step 1: Write the failing test**

`tests/test_xml_encode.cpp`:

```cpp
#include <doctest.h>
#include "xml/encode.hpp"
#include "xml/decode.hpp"

using namespace rbxl;

static Dom roundTrip(const Dom& in) {
    auto text = rbxl::xml::encode(in);
    REQUIRE(text);
    auto out = rbxl::xml::decode(text.value().data(), text.value().size());
    REQUIRE_MESSAGE(out, out.hasValue() ? "" : out.error().toString());
    return std::move(out.value());
}

TEST_CASE("encoded XML has the required root and version") {
    Dom dom;
    dom.create("Part");
    auto text = rbxl::xml::encode(dom);
    REQUIRE(text);
    CHECK(text.value().find("<roblox version=\"4\"") != std::string::npos);
}

TEST_CASE("hierarchy and names survive a round-trip") {
    Dom dom;
    auto model = dom.create("Model");
    dom.setProperty(model, "Name", std::string("Rig"));
    auto part = dom.create("Part");
    dom.setProperty(part, "Name", std::string("Head"));
    dom.setParent(part, model);

    Dom back = roundTrip(dom);
    REQUIRE(back.roots().size() == 1);
    CHECK(back.nameOf(back.roots()[0]) == "Rig");
    REQUIRE(back.at(back.roots()[0]).children.size() == 1);
}

TEST_CASE("every value type survives a round-trip") {
    Dom dom;
    auto id = dom.create("Everything");
    dom.setProperty(id, "S", std::string("text"));
    dom.setProperty(id, "B", true);
    dom.setProperty(id, "I32", int32_t{-9});
    dom.setProperty(id, "I64", int64_t{-9000000000LL});
    dom.setProperty(id, "F32", 0.125f);
    dom.setProperty(id, "F64", 0.1);
    dom.setProperty(id, "V2", Vector2{1, 2});
    dom.setProperty(id, "V3", Vector3{1, 2, 3});
    dom.setProperty(id, "V3i", Vector3int16{-1, 2, -3});
    dom.setProperty(id, "C3", Color3{0.25f, 0.5f, 0.75f});
    dom.setProperty(id, "C3u8", Color3uint8{1, 2, 3});
    dom.setProperty(id, "UD", UDim{0.5f, 7});
    dom.setProperty(id, "UD2", UDim2{{0.5f, 7}, {0.25f, -7}});
    dom.setProperty(id, "R", Rect{{0, 1}, {2, 3}});
    dom.setProperty(id, "Ray_", Ray{{1, 2, 3}, {4, 5, 6}});
    dom.setProperty(id, "NR", NumberRange{0.0f, 1.0f});
    dom.setProperty(id, "Fc", Faces{0x26});
    dom.setProperty(id, "Ax", Axes{0x05});
    dom.setProperty(id, "En", EnumValue{256});
    dom.setProperty(id, "Sec", SecurityCapabilities{0x0102030405060708ull});
    CFrame cf; cf.position = {1, 2, 3};
    dom.setProperty(id, "CF", cf);
    OptionalCFrame ocf; ocf.hasValue = true; ocf.value = cf;
    dom.setProperty(id, "OCF", ocf);
    NumberSequence ns; ns.keypoints = {{0, 0, 0}, {1, 1, 0.5f}};
    dom.setProperty(id, "NS", ns);
    ColorSequence cs; cs.keypoints = {{0, Color3{1, 1, 1}, 0}, {1, Color3{0, 0, 0}, 0}};
    dom.setProperty(id, "CS", cs);
    PhysicalProperties pp; pp.custom = true; pp.density = 0.7f; pp.friction = 0.3f;
    dom.setProperty(id, "PP", pp);
    Font font; font.family = "rbxasset://fonts/families/Arial.json";
    font.weight = 700; font.style = 1;
    dom.setProperty(id, "Ft", font);
    dom.setProperty(id, "CId", ContentId{"rbxassetid://1"});
    Content content; content.sourceType = Content::SourceType::Uri; content.uri = "rbxassetid://2";
    dom.setProperty(id, "Cn", content);
    dom.setProperty(id, "PS", ProtectedString{"local x = 1\n"});
    dom.setProperty(id, "BS", BinaryString{{0x00, 0xFF, 0x10}});

    Dom back = roundTrip(dom);
    for (const auto& prop : dom.at(id).properties) {
        const std::string& name = dom.names().name(prop.first);
        CAPTURE(name);
        const Variant* got = back.getProperty(0, name);
        REQUIRE(got != nullptr);
        CHECK(variantTypeOf(*got) == variantTypeOf(prop.second));
        CHECK(variantEqual(*got, prop.second));
    }
}

TEST_CASE("Font writes Style as a name and omits an empty CachedFaceId") {
    Dom dom;
    auto id = dom.create("TextLabel");
    Font f; f.family = "rbxasset://fonts/families/Arial.json"; f.weight = 700; f.style = 1;
    dom.setProperty(id, "FontFace", f);
    auto text = rbxl::xml::encode(dom);
    REQUIRE(text);
    CHECK(text.value().find("<Style>Italic</Style>") != std::string::npos);
    CHECK(text.value().find("<Weight>700</Weight>") != std::string::npos);
    CHECK(text.value().find("CachedFaceId") == std::string::npos);
}

TEST_CASE("ProtectedString is written as CDATA") {
    Dom dom;
    auto id = dom.create("Script");
    dom.setProperty(id, "Source", ProtectedString{"if a < b and c > d then end"});
    auto text = rbxl::xml::encode(dom);
    REQUIRE(text);
    CHECK(text.value().find("<![CDATA[") != std::string::npos);
    Dom back = roundTrip(dom);
    CHECK(std::get<ProtectedString>(*back.getProperty(0, "Source")).value ==
          "if a < b and c > d then end");
}

TEST_CASE("a ProtectedString containing the CDATA terminator still round-trips") {
    Dom dom;
    auto id = dom.create("Script");
    dom.setProperty(id, "Source", ProtectedString{"local s = \"]]>\""});
    Dom back = roundTrip(dom);
    CHECK(std::get<ProtectedString>(*back.getProperty(0, "Source")).value ==
          "local s = \"]]>\"");
}

TEST_CASE("floats round-trip exactly through their text form") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "A", 0.1f);
    dom.setProperty(id, "B", 3.4028235e38f);
    dom.setProperty(id, "C", 1.2345678901234567);
    Dom back = roundTrip(dom);
    CHECK(std::get<float>(*back.getProperty(0, "A")) == 0.1f);
    CHECK(std::get<float>(*back.getProperty(0, "B")) == 3.4028235e38f);
    CHECK(std::get<double>(*back.getProperty(0, "C")) == 1.2345678901234567);
}
```

- [ ] **Step 2: Run to verify it fails, then implement `src/xml/encode.cpp`**

- [ ] **Step 3: Run the tests**

Expected: PASS. The "every value type" case is the one that matters; it fails loudly if a type is missing from either direction of the mapping.

- [ ] **Step 4: Commit**

```bash
git add src/xml/encode.hpp src/xml/encode.cpp tests/test_xml_encode.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: encode a Dom to rbxlx and rbxmx files"
```

---

## Task 12: Format detection and the public API

**Files:**
- Create: `include/rbxl/format.hpp`, `include/rbxl/throwing.hpp`, `include/rbxl/rbxl.hpp`, `src/format.cpp`
- Create: `tests/test_api.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**

```cpp
namespace rbxl {

enum class Format { Binary, Xml };

// Sniffs the container. Binary files start with "<roblox!"; XML files have a
// <roblox> root, possibly preceded by a BOM, whitespace, comments, or a
// processing instruction.
Result<Format> detectFormat(const uint8_t* data, std::size_t size);

// Infers the target format from a path suffix:
//   .rbxl / .rbxm  -> Binary      .rbxlx / .rbxmx -> Xml
Result<Format> formatFromExtension(const std::string& path);

struct SaveOptions {
    Format format = Format::Binary;
    Compression compression = Compression::Zstd;
    int level = 3;
    bool pretty = true;                               // XML only
    const ReflectionDatabase* reflection = nullptr;   // optional
};

struct Diagnostics { std::vector<std::string> warnings; };

Result<Dom> loadBuffer(const uint8_t* data, std::size_t size);
Result<Dom> loadFile(const std::string& path);

Result<std::vector<uint8_t>> saveBuffer(const Dom&, const SaveOptions&, Diagnostics* = nullptr);
Status saveFile(const Dom&, const std::string& path, SaveOptions = {}, Diagnostics* = nullptr);

}  // namespace rbxl
```

`saveFile` defaults its `format` from the path extension when the caller leaves `SaveOptions` at its default, so `saveFile(dom, "out.rbxmx")` does the obvious thing. `include/rbxl/throwing.hpp` adds `loadFileOrThrow` and `saveFileOrThrow`, which throw `rbxl::Exception` (carrying an `Error`) and are compiled out under `-fno-exceptions`.

- [ ] **Step 1: Write the failing test**

`tests/test_api.cpp`:

```cpp
#include <doctest.h>
#include <rbxl/rbxl.hpp>
#include <cstdio>
#include <string>

using namespace rbxl;

TEST_CASE("format detection recognises both containers") {
    const char binary[] = "<roblox!\x89\xff\x0d\x0a\x1a\x0a";
    auto b = detectFormat(reinterpret_cast<const uint8_t*>(binary), sizeof(binary) - 1);
    REQUIRE(b);
    CHECK(b.value() == Format::Binary);

    const char xml[] = "<roblox version=\"4\"></roblox>";
    auto x = detectFormat(reinterpret_cast<const uint8_t*>(xml), sizeof(xml) - 1);
    REQUIRE(x);
    CHECK(x.value() == Format::Xml);
}

TEST_CASE("format detection skips a BOM, whitespace, comments, and a declaration") {
    const char xml[] = "\xEF\xBB\xBF  <?xml version=\"1.0\"?>\n<!-- note -->\n"
                       "<roblox version=\"4\"></roblox>";
    auto r = detectFormat(reinterpret_cast<const uint8_t*>(xml), sizeof(xml) - 1);
    REQUIRE(r);
    CHECK(r.value() == Format::Xml);
}

TEST_CASE("format detection rejects unrelated data") {
    const char junk[] = "PK\x03\x04 this is a zip";
    auto r = detectFormat(reinterpret_cast<const uint8_t*>(junk), sizeof(junk) - 1);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::BadMagic);
}

TEST_CASE("extension mapping covers all four suffixes") {
    CHECK(formatFromExtension("a.rbxl").value() == Format::Binary);
    CHECK(formatFromExtension("a.rbxm").value() == Format::Binary);
    CHECK(formatFromExtension("a.rbxlx").value() == Format::Xml);
    CHECK(formatFromExtension("a.rbxmx").value() == Format::Xml);
    CHECK(formatFromExtension("A.RBXMX").value() == Format::Xml);
    CHECK_FALSE(formatFromExtension("a.txt"));
}

TEST_CASE("save then load round-trips through every format on disk") {
    Dom dom;
    auto model = dom.create("Model");
    dom.setProperty(model, "Name", std::string("Sample"));
    auto part = dom.create("Part");
    dom.setProperty(part, "Size", Vector3{4, 1, 2});
    dom.setParent(part, model);

    for (const char* suffix : {".rbxl", ".rbxm", ".rbxlx", ".rbxmx"}) {
        std::string path = std::string("rbxl_api_test") + suffix;
        CAPTURE(path);
        REQUIRE(saveFile(dom, path));
        auto loaded = loadFile(path);
        REQUIRE_MESSAGE(loaded, loaded.hasValue() ? "" : loaded.error().toString());
        CHECK(loaded.value().instanceCount() == 2);
        CHECK(loaded.value().nameOf(loaded.value().roots()[0]) == "Sample");
        std::remove(path.c_str());
    }
}

TEST_CASE("cross-format conversion preserves the tree") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Converted"));
    dom.setProperty(id, "Size", Vector3{1, 2, 3});

    SaveOptions toXml; toXml.format = Format::Xml;
    auto xmlBytes = saveBuffer(dom, toXml);
    REQUIRE(xmlBytes);
    auto viaXml = loadBuffer(xmlBytes.value().data(), xmlBytes.value().size());
    REQUIRE(viaXml);

    SaveOptions toBinary; toBinary.format = Format::Binary;
    auto binBytes = saveBuffer(viaXml.value(), toBinary);
    REQUIRE(binBytes);
    auto viaBinary = loadBuffer(binBytes.value().data(), binBytes.value().size());
    REQUIRE(viaBinary);

    CHECK(viaBinary.value().nameOf(0) == "Converted");
    CHECK(std::get<Vector3>(*viaBinary.value().getProperty(0, "Size")).z == 3.0f);
}

TEST_CASE("a missing file reports Io rather than crashing") {
    auto r = loadFile("definitely-not-here.rbxl");
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::Io);
}
```

- [ ] **Step 2: Run to verify it fails, then implement `src/format.cpp`**

- [ ] **Step 3: Run the tests, then commit**

```bash
cmake --build build -j && ./build/tests/rbxl_tests
git add include/rbxl/format.hpp include/rbxl/throwing.hpp include/rbxl/rbxl.hpp src/format.cpp tests/test_api.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add format detection and the public load/save API"
```

---

## Task 13: Blob helpers (Attributes, Tags, MaterialColors)

**Files:**
- Create: `include/rbxl/blob.hpp`, `src/blob/attributes.cpp`, `src/blob/tags.cpp`, `src/blob/materialcolors.cpp`
- Create: `tests/test_blob.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

Roblox hides three private encodings inside `BinaryString` properties. The DOM keeps them as raw bytes so round-trips are lossless; these helpers let a caller opt into structure.

**Interfaces:**

```cpp
namespace rbxl::blob {

// Instance.Attributes, stored in the "AttributesSerialize" BinaryString.
// Note the type ids here are NOT the file format's: String is 0x02, not 0x01.
using AttributeMap = std::vector<std::pair<std::string, Variant>>;
Result<AttributeMap> parseAttributes(const std::vector<uint8_t>&);
std::vector<uint8_t> serializeAttributes(const AttributeMap&);

// CollectionService tags, stored in the "Tags" BinaryString as
// null-separated names with no trailing separator.
Result<std::vector<std::string>> parseTags(const std::vector<uint8_t>&);
std::vector<uint8_t> serializeTags(const std::vector<std::string>&);

// Terrain.MaterialColors: exactly 69 bytes, 23 materials of RGB.
Result<std::vector<Color3uint8>> parseMaterialColors(const std::vector<uint8_t>&);
std::vector<uint8_t> serializeMaterialColors(const std::vector<Color3uint8>&);

}  // namespace rbxl::blob
```

**Attribute blob format:** a `u32` count, then that many records of `String` name (`u32` length prefix plus bytes), a `u8` type, and a value. **All integers and floats are little-endian and untransformed** — this format shares nothing with the chunk format's interleaving or Roblox float layout. The type ids are their own set:

| Type | ID | Type | ID |
|:--|:--|:--|:--|
| String | `0x02` | Vector3 | `0x11` |
| Bool | `0x03` | CFrame | `0x14` |
| Int32 | `0x04` | EnumItem | `0x15` |
| Float32 | `0x05` | NumberSequence | `0x17` |
| Float64 | `0x06` | ColorSequence | `0x19` |
| UDim | `0x09` | NumberRange | `0x1B` |
| UDim2 | `0x0A` | Rect | `0x1C` |
| BrickColor | `0x0E` | Font | `0x21` |
| Color3 | `0x0F` | | |
| Vector2 | `0x10` | | |

- [ ] **Step 1: Write `tests/test_blob.cpp`**

```cpp
#include <doctest.h>
#include <rbxl/blob.hpp>
#include <rbxl/bitutil.hpp>

using namespace rbxl;
using namespace rbxl::blob;

TEST_CASE("tags are null-separated names") {
    // Spec example: Hello, from, Rojo
    std::vector<uint8_t> data{'H','e','l','l','o', 0, 'f','r','o','m', 0, 'R','o','j','o'};
    auto r = parseTags(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 3);
    CHECK(r.value()[0] == "Hello");
    CHECK(r.value()[2] == "Rojo");
    CHECK(serializeTags(r.value()) == data);
}

TEST_CASE("an empty tag blob yields no tags") {
    auto r = parseTags({});
    REQUIRE(r);
    CHECK(r.value().empty());
}

TEST_CASE("material colors are 23 RGB triples") {
    std::vector<uint8_t> data(69, 0);
    data[0] = 10; data[1] = 20; data[2] = 30;
    auto r = parseMaterialColors(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 23);
    CHECK(r.value()[0].r == 10);
    CHECK(r.value()[0].b == 30);
    CHECK(serializeMaterialColors(r.value()) == data);
    CHECK_FALSE(parseMaterialColors(std::vector<uint8_t>(68, 0)));
}

TEST_CASE("attributes parse little-endian with their own type ids") {
    std::vector<uint8_t> data;
    auto putU32 = [&](uint32_t v) {
        data.resize(data.size() + 4);
        bit::writeU32LE(data.data() + data.size() - 4, v);
    };
    auto putStr = [&](const std::string& s) {
        putU32(static_cast<uint32_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    };
    putU32(2);
    putStr("Health"); data.push_back(0x05);            // Float32
    data.resize(data.size() + 4);
    bit::writeF32LE(data.data() + data.size() - 4, 100.0f);
    putStr("Boss"); data.push_back(0x03);              // Bool
    data.push_back(0x01);

    auto r = parseAttributes(data);
    REQUIRE(r);
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0].first == "Health");
    CHECK(std::get<float>(r.value()[0].second) == 100.0f);
    CHECK(r.value()[1].first == "Boss");
    CHECK(std::get<bool>(r.value()[1].second));
    CHECK(serializeAttributes(r.value()) == data);
}

TEST_CASE("a truncated attribute blob is an error, not a crash") {
    std::vector<uint8_t> data{0x05, 0, 0, 0};   // claims 5 attributes, has none
    CHECK_FALSE(parseAttributes(data));
}
```

- [ ] **Step 2: Run to verify it fails, implement, run again, commit**

```bash
git add include/rbxl/blob.hpp src/blob tests/test_blob.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add attribute, tag, and material colour blob helpers"
```

---

## Task 14: Optional reflection database hook

**Files:**
- Create: `include/rbxl/reflection.hpp`, `src/reflection.cpp`
- Create: `tests/test_reflection.cpp`
- Modify: `src/binary/encode.cpp` (consult the database when present), `src/CMakeLists.txt`, `tests/CMakeLists.txt`

No database ships with the library. This task defines the seam so a caller can supply one later without the core ever depending on a Roblox version.

**Interfaces:**

```cpp
namespace rbxl {

// Supplied by the caller. Every method may return "unknown"; the encoder must
// behave correctly when this interface is absent entirely.
class ReflectionDatabase {
public:
    virtual ~ReflectionDatabase() = default;

    // True when the class is a service and needs an INST service marker.
    // Return `false` for "not a service" and use `knowsClass` to distinguish
    // that from "never heard of it".
    virtual bool isService(const std::string& className) const = 0;
    virtual bool knowsClass(const std::string& className) const = 0;

    // The default value for a property, used to fill gaps when instances of a
    // class disagree about which properties they define. Returns Nil when
    // unknown, in which case the encoder falls back to a zero value.
    virtual Variant defaultValue(const std::string& className,
                                 const std::string& propertyName) const = 0;
};

// A trivial in-memory implementation, useful for tests and for callers who only
// need to teach the encoder about a handful of classes.
class SimpleReflectionDatabase : public ReflectionDatabase {
public:
    void addService(std::string className);
    void addDefault(std::string className, std::string propertyName, Variant value);
    bool isService(const std::string&) const override;
    bool knowsClass(const std::string&) const override;
    Variant defaultValue(const std::string&, const std::string&) const override;
private:
    std::set<std::string> services_;
    std::map<std::pair<std::string, std::string>, Variant> defaults_;
};

}  // namespace rbxl
```

- [ ] **Step 1: Write `tests/test_reflection.cpp`**

```cpp
#include <doctest.h>
#include <rbxl/rbxl.hpp>
#include <rbxl/reflection.hpp>
#include "binary/decode.hpp"
#include "binary/encode.hpp"

using namespace rbxl;

TEST_CASE("encoding works with no reflection database at all") {
    Dom dom;
    auto a = dom.create("Part");
    dom.setProperty(a, "Reflectance", 0.5f);
    dom.create("Part");   // missing the property entirely

    binary::EncodeOptions options;   // reflection stays null
    auto encoded = binary::encode(dom, options);
    REQUIRE(encoded);
    auto back = binary::decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    // Gap filled with a zero value, because nothing knows any better.
    CHECK(std::get<float>(*back.value().getProperty(1, "Reflectance")) == 0.0f);
}

TEST_CASE("a supplied database provides real defaults for filled gaps") {
    SimpleReflectionDatabase db;
    db.addDefault("Part", "Reflectance", 0.25f);

    Dom dom;
    auto a = dom.create("Part");
    dom.setProperty(a, "Reflectance", 0.5f);
    dom.create("Part");

    binary::EncodeOptions options;
    options.reflection = &db;
    auto encoded = binary::encode(dom, options);
    REQUIRE(encoded);
    auto back = binary::decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(std::get<float>(*back.value().getProperty(0, "Reflectance")) == 0.5f);
    CHECK(std::get<float>(*back.value().getProperty(1, "Reflectance")) == 0.25f);
}

TEST_CASE("a supplied database marks services the Dom did not flag") {
    SimpleReflectionDatabase db;
    db.addService("Workspace");

    Dom dom;
    dom.create("Workspace");   // isService left false by the caller
    binary::EncodeOptions options;
    options.reflection = &db;
    auto encoded = binary::encode(dom, options);
    REQUIRE(encoded);
    auto back = binary::decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(back.value().at(0).isService);
}

TEST_CASE("an explicit isService flag on the Dom wins over the database") {
    SimpleReflectionDatabase db;   // knows nothing
    Dom dom;
    auto id = dom.create("Workspace");
    dom.at(id).isService = true;
    binary::EncodeOptions options;
    options.reflection = &db;
    auto encoded = binary::encode(dom, options);
    REQUIRE(encoded);
    auto back = binary::decode(encoded.value().data(), encoded.value().size());
    REQUIRE(back);
    CHECK(back.value().at(0).isService);
}
```

- [ ] **Step 2: Implement, run, commit**

```bash
git add include/rbxl/reflection.hpp src/reflection.cpp src/binary/encode.cpp tests/test_reflection.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add an optional pluggable reflection database seam"
```

---

## Task 15: The `rbxl` command line tool

**Files:**
- Create: `tools/rbxl-cli/CMakeLists.txt`, `tools/rbxl-cli/main.cpp`
- Create: `tests/test_cli.sh`
- Modify: `CMakeLists.txt` (re-enable `add_subdirectory(tools/rbxl-cli)`), `tests/CMakeLists.txt`

**Commands:**

```
rbxl info <file>                     Header, chunk table, class histogram, type histogram.
rbxl convert <in> <out> [--lz4|--zstd|--none] [--level N]
                                     Convert between any two of the four formats.
                                     Target format comes from the output extension.
rbxl dump <file> [--path A/B/C] [--props]
                                     Print the instance tree; --props lists property
                                     names, types, and values.
rbxl roundtrip <file>                Decode, re-encode, decode again, and report whether
                                     the two Doms match. Exit code 1 on mismatch.
```

Argument parsing is hand-rolled; do not add a dependency for it. Every command prints errors to `stderr` via `Error::toString()` and returns a non-zero exit code. `convert` prints any `Diagnostics` warnings to `stderr` but still succeeds.

- [ ] **Step 1: Write `tools/rbxl-cli/CMakeLists.txt`**

```cmake
add_executable(rbxl_cli main.cpp)
set_target_properties(rbxl_cli PROPERTIES OUTPUT_NAME rbxl)
target_link_libraries(rbxl_cli PRIVATE rbxl)
target_include_directories(rbxl_cli PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

- [ ] **Step 2: Re-enable the CLI in the top-level `CMakeLists.txt`**

Uncomment the `add_subdirectory(tools/rbxl-cli)` line added in Task 1.

- [ ] **Step 3: Implement `main.cpp`**

`info` reports, in this order: detected format; for binary, the header's class and instance counts, then a table of chunk name, count, compression used, compressed and uncompressed totals; then the top 20 classes by instance count; then a histogram of property types across the file. This is the tool you reach for when a file will not load, so it must work on a partially-readable file: catch the first chunk error, print everything gathered so far, and exit non-zero.

`roundtrip` uses the `domEqual` helper from Task 16.

- [ ] **Step 4: Write `tests/test_cli.sh`**

```bash
#!/usr/bin/env bash
# Smoke-tests the CLI end to end. Usage: test_cli.sh <path-to-rbxl-binary>
set -euo pipefail
CLI="$1"
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

# Build a tiny model through the CLI's own convert path by starting from XML.
cat > "$WORK/in.rbxmx" <<'XML'
<roblox version="4"><Item class="Model" referent="RBX0"><Properties>
<string name="Name">Rig</string></Properties>
<Item class="Part" referent="RBX1"><Properties>
<string name="Name">Head</string>
<Vector3 name="Size"><X>2</X><Y>1</Y><Z>1</Z></Vector3>
</Properties></Item></Item></roblox>
XML

"$CLI" convert "$WORK/in.rbxmx" "$WORK/out.rbxm"
"$CLI" convert "$WORK/out.rbxm" "$WORK/back.rbxmx"
"$CLI" info "$WORK/out.rbxm" | grep -q "Part"
"$CLI" dump "$WORK/out.rbxm" --props | grep -q "Head"
"$CLI" roundtrip "$WORK/out.rbxm"
grep -q "Head" "$WORK/back.rbxmx"

# Every compression mode must produce a readable file.
for mode in --none --lz4 --zstd; do
  "$CLI" convert "$WORK/in.rbxmx" "$WORK/c.rbxm" "$mode"
  "$CLI" roundtrip "$WORK/c.rbxm"
done

# A bad file must fail loudly rather than silently.
echo "not a roblox file" > "$WORK/junk.rbxl"
if "$CLI" info "$WORK/junk.rbxl" 2>/dev/null; then
  echo "expected failure on junk input" >&2
  exit 1
fi
echo "cli smoke tests passed"
```

Register it in `tests/CMakeLists.txt`:

```cmake
if(UNIX AND RBXL_BUILD_CLI)
  add_test(NAME rbxl_cli_smoke
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.sh $<TARGET_FILE:rbxl_cli>)
endif()
```

- [ ] **Step 5: Build and run**

```bash
chmod +x tests/test_cli.sh
cmake --build build -j && ctest --test-dir build --output-on-failure
```
Expected: both `rbxl_tests` and `rbxl_cli_smoke` pass.

- [ ] **Step 6: Run it against the real corpus by hand**

```bash
./build/tools/rbxl-cli/rbxl info "temp/RaceAPet.rbxl"
./build/tools/rbxl-cli/rbxl roundtrip "temp/FusionCore.rbxl"
./build/tools/rbxl-cli/rbxl convert "temp/Bladeborne Floor 0.rbxl" /tmp/floor0.rbxlx
```

- [ ] **Step 7: Commit**

```bash
git add tools/rbxl-cli tests/test_cli.sh CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add the rbxl command line tool"
```

---

## Task 16: Corpus round-trip suite, hardening, and documentation

**Files:**
- Create: `tests/domcompare.hpp`, `tests/domcompare.cpp`, `tests/test_roundtrip.cpp`
- Create: `tests/test_cframe_table.cpp`
- Modify: `README.md`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `bool domEqual(const Dom& a, const Dom& b, std::string* difference);` for tests and the CLI's `roundtrip` command.

**Equality rules.** Two `Dom`s are equal when they have the same instance count, the same tree shape and class names in post-order, and, for each corresponding instance, the same set of property names with values that satisfy `variantEqual`. Two deliberate exemptions:
- `SharedString` and `NetAssetRef` compare by `value` only. The `key` is an arbitrary identifier that Roblox ignores and that the library regenerates when converting between formats.
- `Dom::unknownChunks()` is compared only for binary-to-binary round-trips, since XML has no representation for it.

- [ ] **Step 1: Write `tests/test_cframe_table.cpp`**

This completes the CFrame work from Task 7 by proving the 24-entry rotation table is what it claims to be.

```cpp
#include <doctest.h>
#include "binary/valuecodec.hpp"
#include <set>
#include <vector>

using namespace rbxl;
using namespace rbxl::binary;

// Exposed from valuecodec_struct.cpp for testing.
extern const float* cframeRotationMatrix(uint8_t id);   // null when id is not special
extern const uint8_t* cframeRotationIds();              // the 24 valid ids
extern std::size_t cframeRotationIdCount();

TEST_CASE("the CFrame rotation table has exactly 24 entries") {
    CHECK(cframeRotationIdCount() == 24);
}

TEST_CASE("every rotation id maps to a proper rotation matrix") {
    for (std::size_t i = 0; i < cframeRotationIdCount(); ++i) {
        const uint8_t id = cframeRotationIds()[i];
        CAPTURE(static_cast<int>(id));
        const float* m = cframeRotationMatrix(id);
        REQUIRE(m != nullptr);
        // Entries are exactly -1, 0, or 1: these are axis-aligned rotations,
        // so no floating point error may creep in.
        for (int k = 0; k < 9; ++k) {
            CHECK((m[k] == -1.0f || m[k] == 0.0f || m[k] == 1.0f));
        }
        // Rows are orthonormal and the determinant is +1 (a rotation, not a
        // reflection). Any sign or transposition slip fails here.
        for (int r = 0; r < 3; ++r) {
            float norm = m[r * 3] * m[r * 3] + m[r * 3 + 1] * m[r * 3 + 1] +
                         m[r * 3 + 2] * m[r * 3 + 2];
            CHECK(norm == 1.0f);
        }
        float det = m[0] * (m[4] * m[8] - m[5] * m[7]) -
                    m[1] * (m[3] * m[8] - m[5] * m[6]) +
                    m[2] * (m[3] * m[7] - m[4] * m[6]);
        CHECK(det == 1.0f);
    }
}

TEST_CASE("the 24 rotation matrices are all distinct") {
    std::set<std::vector<float>> seen;
    for (std::size_t i = 0; i < cframeRotationIdCount(); ++i) {
        const float* m = cframeRotationMatrix(cframeRotationIds()[i]);
        seen.insert(std::vector<float>(m, m + 9));
    }
    CHECK(seen.size() == 24);
}

TEST_CASE("id 0x02 is the identity") {
    const float* m = cframeRotationMatrix(0x02);
    REQUIRE(m != nullptr);
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int k = 0; k < 9; ++k) CHECK(m[k] == identity[k]);
}

TEST_CASE("ids outside the table are rejected") {
    CHECK(cframeRotationMatrix(0x00) == nullptr);
    CHECK(cframeRotationMatrix(0x01) == nullptr);
    CHECK(cframeRotationMatrix(0x04) == nullptr);
    CHECK(cframeRotationMatrix(0xFF) == nullptr);
}
```

- [ ] **Step 2: Run it, and fix the table if it fails**

The table must be built from the angle list in Appendix A.2 using exact values (`cos 0 = 1`, `cos ±90 = 0`, `cos ±180 = -1`, `sin 0 = 0`, `sin 90 = 1`, `sin -90 = -1`, `sin ±180 = 0`), composed in the order Y then X then Z. Never call `std::cos`/`std::sin` here; a matrix entry of `6.1e-17` instead of `0` breaks the encoder's exact-match test and silently disables the whole special-case optimisation.

- [ ] **Step 3: Write `tests/domcompare.hpp` / `.cpp` and `tests/test_roundtrip.cpp`**

```cpp
#include <doctest.h>
#include <rbxl/rbxl.hpp>
#include "domcompare.hpp"
#include <fstream>
#include <string>
#include <vector>

using namespace rbxl;

static const char* kCorpus[] = {
    "Bladeborne Assets.rbxl",
    "Bladeborne Floor 0.rbxl",
    "Bladeborne Floor 1.rbxl",
    "FusionCore.rbxl",
    "RaceAPet.rbxl",
    "place 101949297449238 Build An Island.rbxlx",
};

static bool loadCorpus(const char* name, Dom& out) {
    std::string path = std::string(RBXL_TEST_DATA_DIR) + "/" + name;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    auto result = loadFile(path);
    REQUIRE_MESSAGE(result, result.hasValue() ? "" : result.error().toString());
    out = std::move(result.value());
    return true;
}

TEST_CASE("corpus: binary round-trip is semantically stable") {
    for (const char* name : kCorpus) {
        Dom original;
        if (!loadCorpus(name, original)) continue;
        CAPTURE(name);
        SaveOptions options; options.format = Format::Binary;
        auto bytes = saveBuffer(original, options);
        REQUIRE(bytes);
        auto reloaded = loadBuffer(bytes.value().data(), bytes.value().size());
        REQUIRE_MESSAGE(reloaded, reloaded.hasValue() ? "" : reloaded.error().toString());
        std::string difference;
        CHECK_MESSAGE(domEqual(original, reloaded.value(), &difference), difference);
    }
}

TEST_CASE("corpus: XML round-trip is semantically stable") {
    for (const char* name : kCorpus) {
        Dom original;
        if (!loadCorpus(name, original)) continue;
        CAPTURE(name);
        SaveOptions options; options.format = Format::Xml;
        auto bytes = saveBuffer(original, options);
        REQUIRE(bytes);
        auto reloaded = loadBuffer(bytes.value().data(), bytes.value().size());
        REQUIRE_MESSAGE(reloaded, reloaded.hasValue() ? "" : reloaded.error().toString());
        std::string difference;
        CHECK_MESSAGE(domEqual(original, reloaded.value(), &difference), difference);
    }
}

TEST_CASE("corpus: binary to XML to binary preserves the model") {
    for (const char* name : kCorpus) {
        Dom original;
        if (!loadCorpus(name, original)) continue;
        CAPTURE(name);
        SaveOptions toXml; toXml.format = Format::Xml;
        auto xmlBytes = saveBuffer(original, toXml);
        REQUIRE(xmlBytes);
        auto viaXml = loadBuffer(xmlBytes.value().data(), xmlBytes.value().size());
        REQUIRE(viaXml);

        SaveOptions toBinary; toBinary.format = Format::Binary;
        auto binBytes = saveBuffer(viaXml.value(), toBinary);
        REQUIRE(binBytes);
        auto viaBinary = loadBuffer(binBytes.value().data(), binBytes.value().size());
        REQUIRE(viaBinary);

        std::string difference;
        CHECK_MESSAGE(domEqual(original, viaBinary.value(), &difference), difference);
    }
}

TEST_CASE("corpus: every compression mode produces an equivalent file") {
    Dom original;
    if (!loadCorpus("Bladeborne Floor 0.rbxl", original)) return;
    for (auto mode : {Compression::None, Compression::Lz4, Compression::Zstd}) {
        SaveOptions options; options.format = Format::Binary; options.compression = mode;
        auto bytes = saveBuffer(original, options);
        REQUIRE(bytes);
        auto reloaded = loadBuffer(bytes.value().data(), bytes.value().size());
        REQUIRE(reloaded);
        std::string difference;
        CHECK_MESSAGE(domEqual(original, reloaded.value(), &difference), difference);
    }
}
```

- [ ] **Step 4: Run the whole suite**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure
```
Expected: PASS. When a corpus case fails, `domEqual`'s `difference` string names the instance and property, which is the fastest route to the offending codec.

- [ ] **Step 5: Measure, and record the numbers**

```bash
/usr/bin/time -v ./build/tools/rbxl-cli/rbxl roundtrip "temp/RaceAPet.rbxl" 2>&1 | \
  grep -E "Maximum resident|Elapsed"
```

Write the observed peak RSS and wall time into the README. If peak RSS exceeds roughly 2 GB for `RaceAPet.rbxl`, that is the measurement that justifies boxing `Font` behind an indirection to shrink `Variant`; open it as a follow-up rather than doing it speculatively.

- [ ] **Step 6: Confirm the no-exceptions constraint holds**

```bash
cmake -S . -B build-noexcept -DCMAKE_CXX_FLAGS="-fno-exceptions" -DRBXL_BUILD_TESTS=OFF
cmake --build build-noexcept -j
```
Expected: the library and CLI build clean. `throwing.hpp` must compile to nothing under this flag.

- [ ] **Step 7: Write the README**

Replace `README.md` with: what the library is; the four supported formats; a build snippet; a ten-line usage example covering load, mutate, save; the CLI command list; the measured performance numbers from Step 5; a "what this does not do" section naming the absent reflection database, the regenerated `SharedString` keys, and the unverified `UniqueId` XML rotation; and an attribution line crediting the rbx-dom project for the format documentation.

- [ ] **Step 8: Commit**

```bash
git add tests/domcompare.hpp tests/domcompare.cpp tests/test_roundtrip.cpp tests/test_cframe_table.cpp tests/CMakeLists.txt README.md
git commit -m "test: add corpus round-trip suite and CFrame table validation"
```

---

# Appendix A: Format Reference

## A.1 Binary property type ids

| Id | Type | Interleaved | Notes |
|:--|:--|:--|:--|
| `0x01` | String | no | `u32` LE length + bytes |
| `0x02` | Bool | no | one byte per value |
| `0x03` | Int32 | yes (4) | zigzag, big-endian |
| `0x04` | Float32 | yes (4) | Roblox float layout, big-endian |
| `0x05` | Float64 | no | IEEE-754 little-endian |
| `0x06` | UDim | yes | component arrays: Scale, Offset |
| `0x07` | UDim2 | yes | component arrays: X.Scale, Y.Scale, X.Offset, Y.Offset |
| `0x08` | Ray | no | six plain LE `f32` |
| `0x09` | Faces | no | one byte bitfield |
| `0x0a` | Axes | no | one byte bitfield |
| `0x0b` | BrickColor | yes (4) | untransformed big-endian `u32` |
| `0x0c` | Color3 | yes | component arrays: R, G, B |
| `0x0d` | Vector2 | yes | component arrays: X, Y |
| `0x0e` | Vector3 | yes | component arrays: X, Y, Z |
| `0x0f` | *(unused)* | | never observed |
| `0x10` | CFrame | partly | id bytes and matrices in sequence, then an interleaved Vector3 array |
| `0x11` | *(unused)* | | reserved for Quaternion, never observed |
| `0x12` | Enum | yes (4) | untransformed big-endian `u32` |
| `0x13` | Referent | yes (4) | zigzag big-endian, **accumulated**; `-1` is null |
| `0x14` | Vector3int16 | no | three LE `i16` |
| `0x15` | NumberSequence | no | `u32` count + 3 LE `f32` per keypoint |
| `0x16` | ColorSequence | no | `u32` count + 5 LE `f32` per keypoint |
| `0x17` | NumberRange | no | two LE `f32` |
| `0x18` | Rect | yes | component arrays: Min.X, Min.Y, Max.X, Max.Y |
| `0x19` | PhysicalProperties | no | `u8` bitfield + 0, 5, or 6 LE `f32` |
| `0x1a` | Color3uint8 | no | three separate byte arrays: R, G, B |
| `0x1b` | Int64 | yes (8) | zigzag, big-endian |
| `0x1c` | SharedString | yes (4) | big-endian `u32` index into SSTR; `NetAssetRef` shares this id |
| `0x1d` | Bytecode | no | as String; never interpret the contents |
| `0x1e` | OptionalCFrame | partly | `0x10`, CFrame array, `0x02`, Bool array |
| `0x1f` | UniqueId | yes (16) | index BE `u32`, time BE `u32`, random BE `i64` rotated right 1 |
| `0x20` | Font | no | String family, LE `u16` weight, `u8` style, String cachedFaceId |
| `0x21` | SecurityCapabilities | yes (8) | Int64 encoding, reinterpreted as `u64`. Not in the published spec; confirmed against the rbx_binary implementation |
| `0x22` | Content | mixed | Enum array of source types, then counted URI / object / external sections |

## A.2 CFrame special rotation ids

Rotations are in degrees, applied in the order Y, then X, then Z. Build the matrices at compile time from exact trigonometric values; never call `std::cos` or `std::sin`.

| Id | Y, X, Z | Id | Y, X, Z |
|:--|:--|:--|:--|
| `0x02` | 0, 0, 0 | `0x14` | 0, 180, 0 |
| `0x03` | 90, 0, 0 | `0x15` | -90, -180, 0 |
| `0x05` | 0, 180, 180 | `0x17` | 0, 0, 180 |
| `0x06` | -90, 0, 0 | `0x18` | 90, 180, 0 |
| `0x07` | 0, 180, 90 | `0x19` | 0, 0, -90 |
| `0x09` | 0, 90, 90 | `0x1b` | 0, -90, -90 |
| `0x0a` | 0, 0, 90 | `0x1c` | 0, -180, -90 |
| `0x0c` | 0, -90, 90 | `0x1e` | 0, 90, -90 |
| `0x0d` | -90, -90, 0 | `0x1f` | 90, 90, 0 |
| `0x0e` | 0, -90, 0 | `0x20` | 0, 90, 0 |
| `0x10` | 90, -90, 0 | `0x22` | -90, 90, 0 |
| `0x11` | 0, 90, 180 | `0x23` | 0, -90, 180 |

Id `0x00` means no special case: nine plain IEEE-754 `f32` values follow, in the order `R00 R01 R02 R10 R11 R12 R20 R21 R22`.

## A.3 XML element names

Root is `<roblox version="4">`. Children: `Meta` (with `name`), `External` (legacy, ignorable), `Item` (with `class` and `referent`), and at most one `SharedStrings`. Each `Item` holds exactly one `Properties` element plus nested `Item`s. Each property element carries a `name` attribute.

| Element | Type | Representation |
|:--|:--|:--|
| `string` | String | text |
| `ProtectedString` | ProtectedString | text, whitespace-exact, CDATA recommended |
| `BinaryString` | BinaryString | base64 |
| `bool` | Bool | `true` / `false` |
| `int` | Int32 | text; also used for `BrickColor` properties |
| `int64` | Int64 | text |
| `float` | Float32 | text; `INF`, `-INF`, `NAN` |
| `double` | Float64 | text |
| `token` | Enum | text integer |
| `Ref` | Referent | referent string, or `null` |
| `Vector2` | Vector2 | `<X> <Y>` |
| `Vector3` | Vector3 | `<X> <Y> <Z>` |
| `Vector3int16` | Vector3int16 | `<X> <Y> <Z>` |
| `Color3` | Color3 | `<R> <G> <B>` |
| `Color3uint8` | Color3uint8 | one integer, ARGB packed, alpha `FF` |
| `CoordinateFrame` | CFrame | `<X> <Y> <Z>` then `<R00>`..`<R22>` |
| `OptionalCoordinateFrame` | OptionalCFrame | zero or one `<CFrame>` child |
| `UDim` | UDim | `<S> <O>` |
| `UDim2` | UDim2 | `<XS> <XO> <YS> <YO>` |
| `Rect2D` | Rect | `<min>` and `<max>`, each a Vector2 |
| `Ray` | Ray | `<origin>` and `<direction>`, each a Vector3 |
| `Faces` | Faces | `<faces>` integer |
| `Axes` | Axes | `<axes>` integer |
| `NumberRange` | NumberRange | two space-separated floats |
| `NumberSequence` | NumberSequence | space-separated floats, three per keypoint |
| `ColorSequence` | ColorSequence | space-separated floats, five per keypoint |
| `PhysicalProperties` | PhysicalProperties | `<CustomPhysics>` plus six floats when true |
| `Font` | Font | `<Family>` (Content), `<Weight>` (int), `<Style>` (name), optional `<CachedFaceId>` |
| `UniqueId` | UniqueId | 16 hex bytes: Random `u64`, Time `u32`, Index `u32` |
| `SecurityCapabilities` | SecurityCapabilities | text `u64` |
| `ContentId` | ContentId | `<url>` or `<null>`; legacy `<binary>` / `<hash>` read as empty |
| `Content` | Content **or** ContentId | disambiguate on the child: `url` means ContentId; `uri`, `null`, or `Ref` means Content |
| `SharedString` | SharedString | the `md5` key of a `SharedStrings` entry |
| `NetAssetRef` | NetAssetRef | as SharedString |

## A.4 Sources

- rbx-dom binary format specification: <https://github.com/rojo-rbx/rbx-dom/blob/master/docs/binary.md>
- rbx-dom XML format specification: <https://github.com/rojo-rbx/rbx-dom/blob/master/docs/xml.md>
- rbx-dom attributes specification: <https://github.com/rojo-rbx/rbx-dom/blob/master/docs/attributes.md>
- rbx-dom binary string blobs: <https://github.com/rojo-rbx/rbx-dom/blob/master/docs/binary-strings.md>
- `rbx_binary` reference implementation, consulted for `SecurityCapabilities` and `UniqueId`: <https://github.com/rojo-rbx/rbx-dom/tree/master/rbx_binary>
- rbxmk, which uses `robloxapi/rbxfile` for its format support: <https://github.com/Anaminus/rbxmk>
