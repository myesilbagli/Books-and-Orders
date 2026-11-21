#pragma once

#include "orderbook/book.hpp"
#include "orderbook/itch/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Replays ITCH messages into one OrderBook per stock and counts anything that
// shouldn't happen. On a correct replay of a real day the problem counters stay 0.

namespace ob {

struct ReplayStats {
  std::uint64_t messages = 0;         // every message seen, of any type
  std::uint64_t decode_failures = 0;  // one of our types that didn't decode
  std::uint64_t unknown_order = 0;    // E/C/X/D/U for an order the book doesn't have
  std::uint64_t rejected = 0;         // the book refused: duplicate add, over-execution...
  std::uint64_t crossed = 0;          // updates after which best bid >= best ask
  std::uint16_t first_cross_locate = 0;  // where the first one happened, to investigate
  std::uint64_t first_cross_ns = 0;
};

class Replayer {
 public:
  Replayer();

  // One message, as handed out by ItchReader (type byte first).
  void apply(const std::uint8_t* p, std::size_t len);

  [[nodiscard]] const OrderBook& book(std::uint16_t locate) const { return books_[locate]; }
  [[nodiscard]] std::optional<std::uint16_t> locate_of(const std::string& symbol) const;
  [[nodiscard]] const ReplayStats& stats() const { return stats_; }

 private:
  // One book per possible locate (a 2-byte number, so 65,536), created once and never
  // resized. Resizing a vector may copy its elements to new memory; a copied book's
  // index would still point into the old book's lists. Never resizing rules that out.
  std::vector<OrderBook> books_;
  std::vector<itch::Symbol> symbols_;  // filled from 'R' messages, same index
  ReplayStats stats_;

  // E, C and X all shrink an order in place; one function counts the outcome.
  void reduce(std::uint16_t locate, OrderId id, Quantity qty);
  // Called after anything that can add liquidity (A, F, U): adds can cross, removals can't.
  void check_crossed(std::uint16_t locate, std::uint64_t timestamp_ns);
};

}  // namespace ob
