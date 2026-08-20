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
