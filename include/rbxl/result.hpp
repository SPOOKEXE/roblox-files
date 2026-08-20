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
