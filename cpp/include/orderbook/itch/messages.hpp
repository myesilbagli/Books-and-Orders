#pragma once

#include "orderbook/order.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Nasdaq ITCH 5.0 messages that change the order book, decoded from raw bytes into
// plain structs. Only the 8 types the book needs; the rest (system events, trading
// halts, auctions...) are skipped by the caller.
//
// Every decoder takes the whole message (`p` points at the type byte, `len` is its
// length) and returns std::nullopt instead of guessing when the bytes don't fit:
// wrong length, wrong type byte, or a side byte that isn't 'B'/'S'.

namespace ob::itch {

// Fixed-size, space-padded text ("AAPL    "). std::array instead of std::string:
// no heap allocation, and there are tens of millions of these in one day's file.
using Symbol = std::array<char, 8>;
using Mpid = std::array<char, 4>;  // market participant id, e.g. "GSCO"

// The first 11 bytes of every message.
struct Header {
  char type;                    // 'A', 'D', ...
  std::uint16_t stock_locate;   // the stock's number for today; routes E/X/D/U to a book
  std::uint16_t tracking;       // Nasdaq internal, unused by us
  std::uint64_t timestamp_ns;   // nanoseconds since midnight
};

// 'A' (anonymous) and 'F' (attributed): a new displayed order rests on the book.
struct AddOrder {
  Header header;
  OrderId ref;
  Side side;
  Quantity shares;
  Symbol stock;
  Price price;
  Mpid mpid;  // 'F' only; four spaces for 'A'
};

// 'E': a resting order traded at its own price.
struct OrderExecuted {
  Header header;
  OrderId ref;
  Quantity executed_shares;
  std::uint64_t match_number;
};

// 'C': a resting order traded at a different price than it rested at (e.g. in a cross).
struct OrderExecutedWithPrice {
  Header header;
  OrderId ref;
  Quantity executed_shares;
  std::uint64_t match_number;
  bool printable;  // false: don't count it in volume (it was already counted elsewhere)
  Price execution_price;
};

// 'X': part of an order is cancelled; the rest keeps its queue position.
struct OrderCancel {
  Header header;
  OrderId ref;
  Quantity cancelled_shares;
};

// 'D': the whole order is removed.
struct OrderDelete {
  Header header;
  OrderId ref;
};

// 'U': cancel the old order and add a new one (new id, new size/price, back of the queue).
// Side and stock are not repeated; they are the original order's.
struct OrderReplace {
  Header header;
  OrderId original_ref;
  OrderId new_ref;
  Quantity shares;
  Price price;
};

// 'P' (Trade, non-cross): a trade against a non-displayed (hidden) order. It never touches our book,
// but it is real volume and the only trace hidden liquidity leaves.
struct NonCrossTrade {
  Header header;
  OrderId ref;
  Side side;
  Quantity shares;
  Symbol stock;
  Price price;
  std::uint64_t match_number;
};

// Expected length of each message type in bytes, or 0 for a type we don't decode.
// The caller uses it to check a message before decoding it.
[[nodiscard]] std::size_t message_length(char type);

[[nodiscard]] std::optional<AddOrder> decode_add_order(const std::uint8_t* p, std::size_t len);
[[nodiscard]] std::optional<OrderExecuted> decode_order_executed(const std::uint8_t* p,
                                                                 std::size_t len);
[[nodiscard]] std::optional<OrderExecutedWithPrice> decode_order_executed_with_price(
    const std::uint8_t* p, std::size_t len);
[[nodiscard]] std::optional<OrderCancel> decode_order_cancel(const std::uint8_t* p, std::size_t len);
[[nodiscard]] std::optional<OrderDelete> decode_order_delete(const std::uint8_t* p, std::size_t len);
[[nodiscard]] std::optional<OrderReplace> decode_order_replace(const std::uint8_t* p,
                                                               std::size_t len);
[[nodiscard]] std::optional<NonCrossTrade> decode_non_cross_trade(const std::uint8_t* p, std::size_t len);

}  // namespace ob::itch
