#include <doctest.h>
#include <rbxl/version.hpp>
#include <string>

TEST_CASE("version reports the project version") {
    CHECK(std::string(rbxl::version()) == "0.1.0");
}
