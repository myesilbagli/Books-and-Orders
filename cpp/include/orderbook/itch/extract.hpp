#pragma once

#include "orderbook/itch/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// One stock's slice of an ITCH file, in the same format, so every tool runs on it
// unchanged and in seconds instead of minutes (D43). Kept: every message for that stock
// (its locate), and the system-wide ones (locate 0: start/end of day). Each message is
// copied byte for byte with its length prefix.

namespace ob::itch {

struct ExtractStats {
  std::uint64_t kept = 0;
  std::uint64_t skipped = 0;
  ReadStatus end_status = ReadStatus::End;  // how the input ended
};

[[nodiscard]] ExtractStats extract_stock(const std::uint8_t* data, std::size_t size,
                                         std::uint16_t locate, std::vector<std::uint8_t>& out);

// The stock's locate from its 'R' (stock directory) message, which comes before any
// order; nullopt if it isn't in the file.
[[nodiscard]] std::optional<std::uint16_t> find_locate(const std::uint8_t* data, std::size_t size,
                                                       const std::string& symbol);

}  // namespace ob::itch
