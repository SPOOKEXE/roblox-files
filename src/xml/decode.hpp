#pragma once
#include <rbxl/result.hpp>
#include <rbxl/dom.hpp>
#include <cstddef>

// Turns an .rbxlx/.rbxmx document into a fully realised `Dom`. See decode.cpp
// for the two-pass structure this follows: instances and hierarchy first,
// then properties (so a forward `Ref` can resolve against every referent in
// the file, not just the ones seen so far).
namespace rbxl {
namespace xml {

Result<Dom> decode(const char* data, size_t size);

}  // namespace xml
}  // namespace rbxl
