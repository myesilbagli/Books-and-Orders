#pragma once

#include <cstddef>
#include <cstdint>

// Big-endian integer readers for Nasdaq ITCH 5.0. ITCH sends the most significant
// byte first; this Mac (ARM) stores integers least significant byte first, so the
// bytes can't be used as a number directly. We assemble each value byte by byte.
//
// `p` must point at least N valid bytes. The caller checks the message length once,
// so these stay branch-free on the hot path.

namespace ob::itch {

// Shared logic: shift what we have one byte left, put the next byte in the low end.
// Bytes are uint8_t (never char, whose sign is compiler-defined): 0xFF stays 255.
[[nodiscard]] inline std::uint64_t read_be(const std::uint8_t* p, std::size_t n) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < n; ++i) {
    value = (value << 8) | p[i];
  }
  return value;
}

// Fixed widths: n is a constant, so the compiler unrolls the loop (often into a
// single byte-swap instruction). The casts are safe: n bytes fit the return type.
[[nodiscard]] inline std::uint16_t read_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(read_be(p, 2));
}
[[nodiscard]] inline std::uint32_t read_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(read_be(p, 4));
}
// 6 bytes = ITCH timestamp, nanoseconds since midnight. No 48-bit type exists, so 64-bit.
[[nodiscard]] inline std::uint64_t read_u48(const std::uint8_t* p) { return read_be(p, 6); }
[[nodiscard]] inline std::uint64_t read_u64(const std::uint8_t* p) { return read_be(p, 8); }

}  // namespace ob::itch
