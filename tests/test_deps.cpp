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
