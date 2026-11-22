#pragma once

// Hand-built ITCH 5.0 messages for tests: the same byte layouts as the decoder tests,
// shared by the replay and simulator tests. Every builder takes a timestamp (ns since
// midnight) so tests can control time, which latency tests need.

#include "orderbook/order.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ob::itch_test {

using Bytes = std::vector<std::uint8_t>;

inline void put_be(Bytes& b, std::size_t at, std::uint64_t v, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    b[at + n - 1 - i] = static_cast<std::uint8_t>(v >> (8 * i));
  }
}

inline Bytes message(char type, std::size_t len, std::uint16_t locate, std::uint64_t ts = 1000) {
  Bytes b(len, 0);
  b[0] = static_cast<std::uint8_t>(type);
  put_be(b, 1, locate, 2);
  put_be(b, 5, ts, 6);
  return b;
}

inline Bytes directory(std::uint16_t locate, const char* symbol, std::uint64_t ts = 1000) {
  Bytes b = message('R', 39, locate, ts);
  for (std::size_t i = 0; i < 8; ++i) {
    b[11 + i] = static_cast<std::uint8_t>(i < std::strlen(symbol) ? symbol[i] : ' ');
  }
  return b;
}

inline Bytes add(std::uint16_t locate, OrderId ref, char side, Quantity qty, Price price,
                 std::uint64_t ts = 1000) {
  Bytes b = message('A', 36, locate, ts);
  put_be(b, 11, ref, 8);
  b[19] = static_cast<std::uint8_t>(side);
  put_be(b, 20, qty, 4);
  put_be(b, 32, static_cast<std::uint64_t>(price), 4);
  return b;
}

inline Bytes executed(std::uint16_t locate, OrderId ref, Quantity qty, std::uint64_t ts = 1000) {
  Bytes b = message('E', 31, locate, ts);
  put_be(b, 11, ref, 8);
  put_be(b, 19, qty, 4);
  return b;
}

inline Bytes executed_with_price(std::uint16_t locate, OrderId ref, Quantity qty,
                                 std::uint64_t ts = 1000, Price price = 0, char printable = 'Y') {
  Bytes b = message('C', 36, locate, ts);
  put_be(b, 11, ref, 8);
  put_be(b, 19, qty, 4);
  b[31] = static_cast<std::uint8_t>(printable);
  put_be(b, 32, static_cast<std::uint64_t>(price), 4);
  return b;
}

// 'P': a trade against a hidden order. It never touches the book.
inline Bytes non_cross_trade(std::uint16_t locate, char side, Quantity qty, Price price,
                             std::uint64_t ts = 1000) {
  Bytes b = message('P', 44, locate, ts);
  b[19] = static_cast<std::uint8_t>(side);
  put_be(b, 20, qty, 4);
  put_be(b, 32, static_cast<std::uint64_t>(price), 4);
  return b;
}

inline Bytes cancelled(std::uint16_t locate, OrderId ref, Quantity qty, std::uint64_t ts = 1000) {
  Bytes b = message('X', 23, locate, ts);
  put_be(b, 11, ref, 8);
  put_be(b, 19, qty, 4);
  return b;
}

inline Bytes deleted(std::uint16_t locate, OrderId ref, std::uint64_t ts = 1000) {
  Bytes b = message('D', 19, locate, ts);
  put_be(b, 11, ref, 8);
  return b;
}

inline Bytes replaced(std::uint16_t locate, OrderId old_ref, OrderId new_ref, Quantity qty,
                      Price price, std::uint64_t ts = 1000) {
  Bytes b = message('U', 35, locate, ts);
  put_be(b, 11, old_ref, 8);
  put_be(b, 19, new_ref, 8);
  put_be(b, 27, qty, 4);
  put_be(b, 31, static_cast<std::uint64_t>(price), 4);
  return b;
}

// 'Q': an auction's single print ('O' opening, 'C' closing).
inline Bytes cross_trade(std::uint16_t locate, std::uint64_t qty, Price price, char type,
                         std::uint64_t ts = 1000) {
  Bytes b = message('Q', 40, locate, ts);
  put_be(b, 11, qty, 8);
  put_be(b, 27, static_cast<std::uint64_t>(price), 4);
  b[39] = static_cast<std::uint8_t>(type);
  return b;
}

inline Bytes trading_action(std::uint16_t locate, char state, std::uint64_t ts = 1000) {
  Bytes b = message('H', 25, locate, ts);
  b[19] = static_cast<std::uint8_t>(state);
  return b;
}

}  // namespace ob::itch_test
