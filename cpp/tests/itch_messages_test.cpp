#include "orderbook/itch/messages.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace ob::itch {
namespace {

// --- helpers that build test packets ------------------------------------------

// Writes `value` into n bytes at buf[at], most significant byte first (the ITCH order).
// The mirror image of read_be, so tests don't depend on the code they check.
void put_be(std::vector<std::uint8_t>& buf, std::size_t at, std::uint64_t value, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    buf[at + n - 1 - i] = static_cast<std::uint8_t>(value >> (8 * i));
  }
}

// Copies `text` and pads with spaces up to n bytes, the way ITCH stores "AAPL    ".
void put_text(std::vector<std::uint8_t>& buf, std::size_t at, const char* text, std::size_t n) {
  const std::size_t len = std::strlen(text);
  for (std::size_t i = 0; i < n; ++i) {
    buf[at + i] = static_cast<std::uint8_t>(i < len ? text[i] : ' ');
  }
}

constexpr std::uint64_t kNineThirty = 34'200'000'000'000;  // 09:30:00 in ns since midnight

// A zeroed message of `len` bytes with the common header filled in: stock 13, 09:30.
std::vector<std::uint8_t> make_message(char type, std::size_t len) {
  std::vector<std::uint8_t> buf(len, 0);
  buf[0] = static_cast<std::uint8_t>(type);
  put_be(buf, 1, 13, 2);           // stock locate
  put_be(buf, 3, 0, 2);            // tracking number
  put_be(buf, 5, kNineThirty, 6);  // timestamp
  return buf;
}

Symbol symbol(const char (&text)[9]) { return {text[0], text[1], text[2], text[3],
                                               text[4], text[5], text[6], text[7]}; }

// --- length table --------------------------------------------------------------

TEST(ItchMessagesTest, KnowsTheLengthOfEachType) {
  EXPECT_EQ(message_length('A'), 36u);
  EXPECT_EQ(message_length('F'), 40u);
  EXPECT_EQ(message_length('E'), 31u);
  EXPECT_EQ(message_length('C'), 36u);
  EXPECT_EQ(message_length('X'), 23u);
  EXPECT_EQ(message_length('D'), 19u);
  EXPECT_EQ(message_length('U'), 35u);
  EXPECT_EQ(message_length('P'), 44u);
}

// 'S' (system event) is real ITCH, just not one we decode.
TEST(ItchMessagesTest, UnknownTypeHasLengthZero) {
  EXPECT_EQ(message_length('S'), 0u);
  EXPECT_EQ(message_length('Z'), 0u);
}

// --- one test per message type ---------------------------------------------------

// The exact 36 bytes from the explanation: buy 100 AAPL at $150.2778, order #7, 09:30.
TEST(ItchMessagesTest, DecodesAddOrderFromRawBytes) {
  const std::uint8_t raw[] = {
      0x41,                                            // 'A'
      0x00, 0x0D,                                      // stock locate 13
      0x00, 0x00,                                      // tracking
      0x1F, 0x1A, 0xCE, 0xD9, 0xF0, 0x00,              // 09:30:00
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,  // order #7
      0x42,                                            // 'B'
      0x00, 0x00, 0x00, 0x64,                          // 100 shares
      0x41, 0x41, 0x50, 0x4C, 0x20, 0x20, 0x20, 0x20,  // "AAPL    "
      0x00, 0x16, 0xEE, 0x3A,                          // 1502778 = $150.2778
  };
  const auto m = decode_add_order(raw, sizeof raw);
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->header.type, 'A');
  EXPECT_EQ(m->header.stock_locate, 13u);
  EXPECT_EQ(m->header.timestamp_ns, kNineThirty);
  EXPECT_EQ(m->ref, 7u);
  EXPECT_EQ(m->side, Side::Buy);
  EXPECT_EQ(m->shares, 100u);
  EXPECT_EQ(m->stock, symbol("AAPL    "));
  EXPECT_EQ(m->price, 1'502'778);
  EXPECT_EQ(m->mpid, (Mpid{' ', ' ', ' ', ' '}));  // anonymous
}

// 'F' = 'A' plus the firm's 4-letter code at the end.
TEST(ItchMessagesTest, DecodesAttributedAddOrder) {
  auto buf = make_message('F', 40);
  put_be(buf, 11, 8, 8);
  buf[19] = 'S';
  put_be(buf, 20, 500, 4);
  put_text(buf, 24, "MSFT", 8);
  put_be(buf, 32, 4'101'500, 4);  // $410.15
  put_text(buf, 36, "GSCO", 4);

  const auto m = decode_add_order(buf.data(), buf.size());
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->header.type, 'F');
  EXPECT_EQ(m->ref, 8u);
  EXPECT_EQ(m->side, Side::Sell);
  EXPECT_EQ(m->shares, 500u);
  EXPECT_EQ(m->stock, symbol("MSFT    "));
  EXPECT_EQ(m->price, 4'101'500);
  EXPECT_EQ(m->mpid, (Mpid{'G', 'S', 'C', 'O'}));
}

TEST(ItchMessagesTest, DecodesOrderExecuted) {
  auto buf = make_message('E', 31);
  put_be(buf, 11, 7, 8);
  put_be(buf, 19, 40, 4);
  put_be(buf, 23, 123'456, 8);

  const auto m = decode_order_executed(buf.data(), buf.size());
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->ref, 7u);
  EXPECT_EQ(m->executed_shares, 40u);
  EXPECT_EQ(m->match_number, 123'456u);
}

TEST(ItchMessagesTest, DecodesOrderExecutedWithPrice) {
  auto buf = make_message('C', 36);
  put_be(buf, 11, 7, 8);
  put_be(buf, 19, 40, 4);
  put_be(buf, 23, 123'457, 8);
  buf[31] = 'N';
  put_be(buf, 32, 1'502'500, 4);

  const auto m = decode_order_executed_with_price(buf.data(), buf.size());
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->ref, 7u);
  EXPECT_EQ(m->executed_shares, 40u);
  EXPECT_EQ(m->match_number, 123'457u);
  EXPECT_FALSE(m->printable);
  EXPECT_EQ(m->execution_price, 1'502'500);
}

TEST(ItchMessagesTest, DecodesOrderCancel) {
  auto buf = make_message('X', 23);
  put_be(buf, 11, 7, 8);
  put_be(buf, 19, 30, 4);

  const auto m = decode_order_cancel(buf.data(), buf.size());
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->ref, 7u);
  EXPECT_EQ(m->cancelled_shares, 30u);
}

// The exact 19 bytes from the explanation: delete order #7.
TEST(ItchMessagesTest, DecodesOrderDeleteFromRawBytes) {
  const std::uint8_t raw[] = {
      0x44,                                            // 'D'
      0x00, 0x0D,                                      // stock locate 13
      0x00, 0x00,                                      // tracking
      0x1F, 0x1A, 0xCE, 0xD9, 0xF0, 0x05,              // 09:30:00 + 5 ns
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,  // order #7
  };
  const auto m = decode_order_delete(raw, sizeof raw);
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->header.type, 'D');
  EXPECT_EQ(m->header.timestamp_ns, kNineThirty + 5);
  EXPECT_EQ(m->ref, 7u);
}

TEST(ItchMessagesTest, DecodesOrderReplace) {
  auto buf = make_message('U', 35);
  put_be(buf, 11, 7, 8);
  put_be(buf, 19, 9, 8);
  put_be(buf, 27, 200, 4);
  put_be(buf, 31, 1'502'800, 4);

  const auto m = decode_order_replace(buf.data(), buf.size());
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->original_ref, 7u);
  EXPECT_EQ(m->new_ref, 9u);
  EXPECT_EQ(m->shares, 200u);
  EXPECT_EQ(m->price, 1'502'800);
}

TEST(ItchMessagesTest, DecodesNonCrossTrade) {
  auto buf = make_message('P', 44);
  put_be(buf, 11, 0, 8);  // Nasdaq sends 0 here: hidden orders aren't identified
  buf[19] = 'B';
  put_be(buf, 20, 300, 4);
  put_text(buf, 24, "AAPL", 8);
  put_be(buf, 32, 1'502'750, 4);
  put_be(buf, 36, 123'458, 8);

  const auto m = decode_non_cross_trade(buf.data(), buf.size());
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(m->ref, 0u);
  EXPECT_EQ(m->side, Side::Buy);
  EXPECT_EQ(m->shares, 300u);
  EXPECT_EQ(m->stock, symbol("AAPL    "));
  EXPECT_EQ(m->price, 1'502'750);
  EXPECT_EQ(m->match_number, 123'458u);
}

// --- rejects: bad input gives nullopt, never a half-decoded message ---------------

// One byte short would read past the end; one byte long means we're out of step.
TEST(ItchMessagesTest, RejectsWrongLength) {
  const auto short_msg = make_message('A', 35);
  const auto long_msg = make_message('A', 37);
  EXPECT_FALSE(decode_add_order(short_msg.data(), short_msg.size()).has_value());
  EXPECT_FALSE(decode_add_order(long_msg.data(), long_msg.size()).has_value());

  // 'A' at the length of an 'F' is still wrong: the length must match the type.
  auto a_as_f = make_message('A', 40);
  a_as_f[19] = 'B';
  EXPECT_FALSE(decode_add_order(a_as_f.data(), a_as_f.size()).has_value());
}

// 'A' and 'C' are both 36 bytes, so the length alone can't tell them apart.
TEST(ItchMessagesTest, RejectsWrongTypeByte) {
  auto exec = make_message('C', 36);
  exec[19] = 'B';  // would even pass the side check
  EXPECT_FALSE(decode_add_order(exec.data(), exec.size()).has_value());

  const auto add = make_message('A', 36);
  EXPECT_FALSE(decode_order_executed_with_price(add.data(), add.size()).has_value());
}

TEST(ItchMessagesTest, RejectsSideOtherThanBOrS) {
  auto add = make_message('A', 36);
  add[19] = 'X';
  EXPECT_FALSE(decode_add_order(add.data(), add.size()).has_value());

  auto trade = make_message('P', 44);
  trade[19] = 0;
  EXPECT_FALSE(decode_non_cross_trade(trade.data(), trade.size()).has_value());
}

TEST(ItchMessagesTest, RejectsPrintableOtherThanYOrN) {
  auto buf = make_message('C', 36);
  buf[31] = 'y';  // lower case is not in the spec
  EXPECT_FALSE(decode_order_executed_with_price(buf.data(), buf.size()).has_value());
}

}  // namespace
}  // namespace ob::itch
