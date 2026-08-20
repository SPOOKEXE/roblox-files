#pragma once

namespace rbxl {

// The three ways a binary chunk's body can be stored. Sniffed on read from
// the compressed body's leading bytes; declared explicitly on write.
enum class Compression { None, Lz4, Zstd };

}  // namespace rbxl
