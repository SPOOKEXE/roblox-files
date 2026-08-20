#pragma once
#include <rbxl/format.hpp>
#include <rbxl/result.hpp>

// Opt-in exception-throwing sugar over the Result-based API in format.hpp.
// Not pulled in by rbxl.hpp: the library and its core headers never throw,
// so this stays a separate include for callers who prefer exceptions to
// checking a Result. Guarded so the whole header compiles to nothing under
// -fno-exceptions, which the library itself is required to build under.
#ifdef __cpp_exceptions

#include <stdexcept>
#include <string>
#include <utility>

namespace rbxl {

// Thrown by the *OrThrow helpers below. Carries the original Error so a
// catch handler keeps the ErrorCode, message and offset rather than only a
// formatted string.
class Exception : public std::runtime_error {
public:
    explicit Exception(Error error)
        : std::runtime_error(error.toString()), error_(std::move(error)) {}

    const Error& error() const { return error_; }

private:
    Error error_;
};

inline Dom loadFileOrThrow(const std::string& path) {
    auto result = loadFile(path);
    if (!result) throw Exception(result.error());
    return std::move(result.value());
}

inline void saveFileOrThrow(const Dom& dom, const std::string& path, SaveOptions options = {},
                             Diagnostics* diagnostics = nullptr) {
    auto status = saveFile(dom, path, options, diagnostics);
    if (!status) throw Exception(status.error());
}

}  // namespace rbxl

#endif  // __cpp_exceptions
