#pragma once

#include "orderbook/itch/reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// One pass over a whole ITCH file: count every message type, run our 8 decoders on
// their messages, and report how the file ended. The first check on real data:
// sensible counts, 0 decode failures, and a clean End.

namespace ob::itch {

struct ScanStats {
  // Indexed by the type byte itself ('A' = 65), so counting is one array add, no
  // lookup. 256 slots cover every possible byte, including types we don't know.
  std::array<std::uint64_t, 256> count_by_type{};
  std::uint64_t messages = 0;
  std::uint64_t bytes = 0;             // consumed, length prefixes included
  std::uint64_t decode_failures = 0;   // our types (A F E C X D U P) the decoder rejected
  ReadStatus end_status = ReadStatus::End;
};

[[nodiscard]] ScanStats scan(const std::uint8_t* data, std::size_t size);

}  // namespace ob::itch
