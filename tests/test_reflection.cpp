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
