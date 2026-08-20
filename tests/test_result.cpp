#include <doctest.h>
#include <rbxl/result.hpp>
#include <rbxl/version.hpp>
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

TEST_CASE("Result copy-assignment and move-assignment swap correctly") {
    Result<int> a = 1;
    Result<int> b = makeError(ErrorCode::Malformed, "bad");
    a = b;
    CHECK_FALSE(a);
    CHECK(a.error().code == ErrorCode::Malformed);
    CHECK(b.error().code == ErrorCode::Malformed);

    Result<int> c = 5;
    Result<int> d = 9;
    c = std::move(d);
    CHECK(c.value() == 9);

    // Self-assignment must not corrupt the value.
    Result<std::string> s = std::string("hello");
    s = s;
    CHECK(s.value() == "hello");
    s = std::move(s);
    CHECK(s.value() == "hello");
}

TEST_CASE("version reports the project version") {
    CHECK(std::string(version()) == "0.1.0");
}
