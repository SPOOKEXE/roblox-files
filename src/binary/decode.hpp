#pragma once
#include <rbxl/result.hpp>
#include <rbxl/dom.hpp>
#include <cstdint>
#include <cstddef>

// Turns the binary container's chunk stream into a fully realised `Dom`:
// instances, properties, and hierarchy. See decode.cpp for the chunk-by-chunk
// order this follows.
namespace rbxl {
namespace binary {

Result<Dom> decode(const uint8_t* data, size_t size);

}  // namespace binary
}  // namespace rbxl
