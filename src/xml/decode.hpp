#pragma once
#include <rbxl/result.hpp>
#include <rbxl/dom.hpp>
#include <rbxl/variant.hpp>
#include <cstddef>
#include <string_view>
#include <unordered_map>

// Turns an .rbxlx/.rbxmx document into a fully realised `Dom`. See decode.cpp
// for the two-pass structure this follows: instances and hierarchy first,
// then properties (so a forward `Ref` can resolve against every referent in
// the file, not just the ones seen so far).
namespace rbxl {
namespace xml {

Result<Dom> decode(const char* data, size_t size);

// The element-name to VariantType table decode.cpp uses to dispatch every
// property except Content/ContentId (which it disambiguates structurally
// instead; see decodeContentLike in decode.cpp). Exposed here, rather than
// kept file-local, solely so tests -- in particular encode.cpp's own
// name-mapping test -- can check themselves against this table directly
// instead of maintaining a second copy of it. Not part of the library's
// public interface; nothing outside this source tree should call it.
const std::unordered_map<std::string_view, VariantType>& elementTypeTable();

}  // namespace xml
}  // namespace rbxl
