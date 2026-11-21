#include "orderbook/itch/messages.hpp"

#include "orderbook/itch/bytes.hpp"

// Byte offsets below are from the Nasdaq TotalView-ITCH 5.0 specification.
// Every message starts with the same 11 bytes:
//   0 type (1) | 1 stock locate (2) | 3 tracking (2) | 5 timestamp (6)

namespace ob::itch {

namespace {  // helpers visible only in this file

// The one gate before any field is read: the type byte is the one we expect and the
// length is exactly that type's length. The length is checked first, so a zero-length
// message never has its type byte read.
bool fits(const std::uint8_t* p, std::size_t len, char type) {
  return len == message_length(type) && p[0] == static_cast<std::uint8_t>(type);
}

Header read_header(const std::uint8_t* p) {
  return Header{.type = static_cast<char>(p[0]),
                .stock_locate = read_u16(p + 1),
                .tracking = read_u16(p + 3),
                .timestamp_ns = read_u48(p + 5)};
}

// 'B'/'S' -> Side; anything else is a corrupt message, so no Side at all.
std::optional<Side> read_side(std::uint8_t b) {
  switch (b) {
    case 'B':
      return Side::Buy;
    case 'S':
      return Side::Sell;
    default:
      return std::nullopt;
  }
}

// Copies fixed-size text as is (padding spaces included), no allocation.
template <std::size_t N>
std::array<char, N> read_text(const std::uint8_t* p) {
  std::array<char, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    out[i] = static_cast<char>(p[i]);
  }
  return out;
}

// ITCH prices are unsigned 32-bit with 4 implied decimals, the same unit as our
// Price (D1); widening to int64 never loses anything.
Price read_price(const std::uint8_t* p) { return static_cast<Price>(read_u32(p)); }

}  // namespace

std::size_t message_length(char type) {
  switch (type) {
    case 'A': return 36;
    case 'F': return 40;
    case 'E': return 31;
    case 'C': return 36;
    case 'X': return 23;
    case 'D': return 19;
    case 'U': return 35;
    case 'P': return 44;
    default: return 0;  // a type we don't decode
  }
}

std::optional<AddOrder> decode_add_order(const std::uint8_t* p, std::size_t len) {
  const bool attributed = fits(p, len, 'F');
  if (!attributed && !fits(p, len, 'A')) {
    return std::nullopt;
  }
  const auto side = read_side(p[19]);
  if (!side) {
    return std::nullopt;
  }
  return AddOrder{.header = read_header(p),
                  .ref = read_u64(p + 11),
                  .side = *side,  // * takes the value out of the optional; checked above
                  .shares = read_u32(p + 20),
                  .stock = read_text<8>(p + 24),
                  .price = read_price(p + 32),
                  .mpid = attributed ? read_text<4>(p + 36) : Mpid{' ', ' ', ' ', ' '}};
}

std::optional<OrderExecuted> decode_order_executed(const std::uint8_t* p, std::size_t len) {
  if (!fits(p, len, 'E')) {
    return std::nullopt;
  }
  return OrderExecuted{.header = read_header(p),
                       .ref = read_u64(p + 11),
                       .executed_shares = read_u32(p + 19),
                       .match_number = read_u64(p + 23)};
}

std::optional<OrderExecutedWithPrice> decode_order_executed_with_price(const std::uint8_t* p,
                                                                       std::size_t len) {
  // The spec allows only 'Y' or 'N'; anything else means we're misreading the stream.
  if (!fits(p, len, 'C') || (p[31] != 'Y' && p[31] != 'N')) {
    return std::nullopt;
  }
  return OrderExecutedWithPrice{.header = read_header(p),
                                .ref = read_u64(p + 11),
                                .executed_shares = read_u32(p + 19),
                                .match_number = read_u64(p + 23),
                                .printable = p[31] == 'Y',
                                .execution_price = read_price(p + 32)};
}

std::optional<OrderCancel> decode_order_cancel(const std::uint8_t* p, std::size_t len) {
  if (!fits(p, len, 'X')) {
    return std::nullopt;
  }
  return OrderCancel{
      .header = read_header(p), .ref = read_u64(p + 11), .cancelled_shares = read_u32(p + 19)};
}

std::optional<OrderDelete> decode_order_delete(const std::uint8_t* p, std::size_t len) {
  if (!fits(p, len, 'D')) {
    return std::nullopt;
  }
  return OrderDelete{.header = read_header(p), .ref = read_u64(p + 11)};
}

std::optional<OrderReplace> decode_order_replace(const std::uint8_t* p, std::size_t len) {
  if (!fits(p, len, 'U')) {
    return std::nullopt;
  }
  return OrderReplace{.header = read_header(p),
                      .original_ref = read_u64(p + 11),
                      .new_ref = read_u64(p + 19),
                      .shares = read_u32(p + 27),
                      .price = read_price(p + 31)};
}

std::optional<NonCrossTrade> decode_non_cross_trade(const std::uint8_t* p, std::size_t len) {
  if (!fits(p, len, 'P')) {
    return std::nullopt;
  }
  const auto side = read_side(p[19]);
  if (!side) {
    return std::nullopt;
  }
  return NonCrossTrade{.header = read_header(p),
                       .ref = read_u64(p + 11),
                       .side = *side,
                       .shares = read_u32(p + 20),
                       .stock = read_text<8>(p + 24),
                       .price = read_price(p + 32),
                       .match_number = read_u64(p + 36)};
}

}  // namespace ob::itch
