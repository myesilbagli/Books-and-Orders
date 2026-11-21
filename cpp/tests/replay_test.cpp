#include "orderbook/replay.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace ob {
namespace {

using Ids = std::vector<OrderId>;
using Bytes = std::vector<std::uint8_t>;

// --- packet builders (same byte layouts as the decoder tests) ---------------------

void put_be(Bytes& b, std::size_t at, std::uint64_t v, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    b[at + n - 1 - i] = static_cast<std::uint8_t>(v >> (8 * i));
  }
}

Bytes message(char type, std::size_t len, std::uint16_t locate, std::uint64_t ts = 1000) {
  Bytes b(len, 0);
  b[0] = static_cast<std::uint8_t>(type);
  put_be(b, 1, locate, 2);
  put_be(b, 5, ts, 6);
  return b;
}

Bytes directory(std::uint16_t locate, const char* symbol) {
  Bytes b = message('R', 39, locate);
  for (std::size_t i = 0; i < 8; ++i) {
    b[11 + i] = static_cast<std::uint8_t>(i < std::strlen(symbol) ? symbol[i] : ' ');
  }
  return b;
}

Bytes add(std::uint16_t locate, OrderId ref, char side, Quantity qty, Price price,
          std::uint64_t ts = 1000) {
  Bytes b = message('A', 36, locate, ts);
  put_be(b, 11, ref, 8);
  b[19] = static_cast<std::uint8_t>(side);
  put_be(b, 20, qty, 4);
  put_be(b, 32, static_cast<std::uint64_t>(price), 4);
  return b;
}

Bytes executed(std::uint16_t locate, OrderId ref, Quantity qty) {
  Bytes b = message('E', 31, locate);
  put_be(b, 11, ref, 8);
  put_be(b, 19, qty, 4);
  return b;
}

Bytes executed_with_price(std::uint16_t locate, OrderId ref, Quantity qty) {
  Bytes b = message('C', 36, locate);
  put_be(b, 11, ref, 8);
  put_be(b, 19, qty, 4);
  b[31] = 'Y';
  return b;
}

Bytes cancelled(std::uint16_t locate, OrderId ref, Quantity qty) {
  Bytes b = message('X', 23, locate);
  put_be(b, 11, ref, 8);
  put_be(b, 19, qty, 4);
  return b;
}

Bytes deleted(std::uint16_t locate, OrderId ref) {
  Bytes b = message('D', 19, locate);
  put_be(b, 11, ref, 8);
  return b;
}

Bytes replaced(std::uint16_t locate, OrderId old_ref, OrderId new_ref, Quantity qty, Price price) {
  Bytes b = message('U', 35, locate);
  put_be(b, 11, old_ref, 8);
  put_be(b, 19, new_ref, 8);
  put_be(b, 27, qty, 4);
  put_be(b, 31, static_cast<std::uint64_t>(price), 4);
  return b;
}

Bytes trading_action(std::uint16_t locate, char state) {
  Bytes b = message('H', 25, locate);
  b[19] = static_cast<std::uint8_t>(state);
  return b;
}

// Not named apply: Bytes is a std::vector, so the compiler would also consider std::apply.
void feed(Replayer& r, const Bytes& b) { r.apply(b.data(), b.size()); }

// --- routing -------------------------------------------------------------------------

TEST(ReplayTest, StockDirectoryNamesALocate) {
  Replayer r;
  feed(r, directory(13, "AAPL"));
  feed(r, directory(6556, "QQQ"));
  EXPECT_EQ(r.locate_of("AAPL"), 13);
  EXPECT_EQ(r.locate_of("QQQ"), 6556);
  EXPECT_FALSE(r.locate_of("MSFT").has_value());

  // And back: locate -> symbol, padding trimmed; empty for a locate never named.
  EXPECT_EQ(r.symbol_of(13), "AAPL");
  EXPECT_EQ(r.symbol_of(6556), "QQQ");
  EXPECT_EQ(r.symbol_of(14), "");
}

// Each message goes to its own stock's book, chosen by locate.
TEST(ReplayTest, AddsGoToTheirOwnStocksBook) {
  Replayer r;
  feed(r, add(13, 1, 'B', 100, 2'890'000));
  feed(r, add(14, 2, 'S', 200, 1'500'000));

  EXPECT_EQ(r.book(13).best_bid(), 2'890'000);
  EXPECT_FALSE(r.book(13).best_ask().has_value());
  EXPECT_EQ(r.book(14).best_ask(), 1'500'000);
  EXPECT_EQ(r.book(15).order_count(), 0u);
}

// --- the book operations ---------------------------------------------------------------

TEST(ReplayTest, ExecutionsShrinkInPlace) {
  Replayer r;
  feed(r, add(13, 1, 'B', 500, 10'000));
  feed(r, add(13, 2, 'B', 300, 10'000));
  feed(r, executed(13, 1, 200));
  feed(r, executed_with_price(13, 1, 100));

  EXPECT_EQ(r.book(13).queue_at(Side::Buy, 10'000), (Ids{1, 2}));
  EXPECT_EQ(r.book(13).volume_at(Side::Buy, 10'000), 500u);  // 200 + 300
}

TEST(ReplayTest, CancelShrinksAndDeleteRemoves) {
  Replayer r;
  feed(r, add(13, 1, 'S', 500, 10'010));
  feed(r, add(13, 2, 'S', 300, 10'010));
  feed(r, cancelled(13, 1, 100));
  feed(r, deleted(13, 2));

  EXPECT_EQ(r.book(13).queue_at(Side::Sell, 10'010), (Ids{1}));
  EXPECT_EQ(r.book(13).volume_at(Side::Sell, 10'010), 400u);
}

TEST(ReplayTest, ReplaceRequeuesAtTheNewPrice) {
  Replayer r;
  feed(r, add(13, 1, 'B', 500, 10'000));
  feed(r, replaced(13, 1, 7, 200, 10'001));

  EXPECT_EQ(r.book(13).best_bid(), 10'001);
  EXPECT_EQ(r.book(13).queue_at(Side::Buy, 10'001), (Ids{7}));
  EXPECT_EQ(r.book(13).order_count(), 1u);
}

// Messages that don't change a book (system events, hidden trades...) are only counted.
TEST(ReplayTest, OtherMessagesAreCountedNotApplied) {
  Replayer r;
  feed(r, message('S', 12, 0));
  feed(r, message('P', 44, 13));
  EXPECT_EQ(r.stats().messages, 2u);
  EXPECT_EQ(r.book(13).order_count(), 0u);
}

TEST(ReplayTest, CleanDayHasNoProblems) {
  Replayer r;
  feed(r, add(13, 1, 'B', 500, 10'000));
  feed(r, add(13, 2, 'S', 500, 10'001));
  feed(r, executed(13, 1, 500));
  const ReplayStats& s = r.stats();
  EXPECT_EQ(s.messages, 3u);
  EXPECT_EQ(s.decode_failures, 0u);
  EXPECT_EQ(s.unknown_order, 0u);
  EXPECT_EQ(s.rejected, 0u);
  EXPECT_EQ(s.crossed, 0u);
}

// --- the checks: on a real day every one of these should stay 0 ------------------------

// An order we never saw: we missed or misread a message.
TEST(ReplayTest, UnknownOrderIsCounted) {
  Replayer r;
  feed(r, deleted(13, 99));
  feed(r, executed(13, 98, 100));
  feed(r, replaced(13, 97, 7, 100, 10'000));
  EXPECT_EQ(r.stats().unknown_order, 3u);
}

// The order exists but the book refuses: our share count has drifted.
TEST(ReplayTest, OverExecutionIsCountedAsRejected) {
  Replayer r;
  feed(r, add(13, 1, 'B', 100, 10'000));
  feed(r, executed(13, 1, 101));
  EXPECT_EQ(r.stats().rejected, 1u);
  EXPECT_EQ(r.stats().unknown_order, 0u);
  EXPECT_EQ(r.book(13).volume_at(Side::Buy, 10'000), 100u);
}

TEST(ReplayTest, DuplicateAddIsCountedAsRejected) {
  Replayer r;
  feed(r, add(13, 1, 'B', 100, 10'000));
  feed(r, add(13, 1, 'B', 100, 10'000));
  EXPECT_EQ(r.stats().rejected, 1u);
}

// Bid >= ask on one venue's displayed book should never last: those orders would have
// traded. Record how many times, and where it happened first, to investigate.
TEST(ReplayTest, CrossedBookIsDetected) {
  Replayer r;
  feed(r, add(13, 1, 'B', 100, 10'010));
  feed(r, add(13, 2, 'S', 100, 10'000, 5555));
  EXPECT_EQ(r.stats().crossed, 1u);
  EXPECT_EQ(r.stats().first_cross_locate, 13u);
  EXPECT_EQ(r.stats().first_cross_ns, 5555u);
}

// Bid == ask (a locked book) is just as impossible, so it counts too.
TEST(ReplayTest, LockedBookCountsAsCrossed) {
  Replayer r;
  feed(r, add(13, 1, 'B', 100, 10'000));
  feed(r, add(13, 2, 'S', 100, 10'000));
  EXPECT_EQ(r.stats().crossed, 1u);
}

// While halted, Nasdaq accepts orders but doesn't match them, so a crossed book is
// real, not a bug. It's counted on its own so `crossed` stays a pure error signal.
TEST(ReplayTest, CrossWhileHaltedIsCountedSeparately) {
  Replayer r;
  feed(r, trading_action(13, 'H'));
  feed(r, add(13, 1, 'B', 100, 10'010));
  feed(r, add(13, 2, 'S', 100, 10'000));
  EXPECT_EQ(r.stats().crossed, 0u);
  EXPECT_EQ(r.stats().crossed_not_trading, 1u);
}

// Paused (LULD) and quotation-only don't match either.
TEST(ReplayTest, PausedAndQuotationOnlyAreNotTrading) {
  Replayer r;
  feed(r, trading_action(13, 'P'));
  feed(r, add(13, 1, 'B', 100, 10'010));
  feed(r, add(13, 2, 'S', 100, 10'000));
  feed(r, trading_action(14, 'Q'));
  feed(r, add(14, 3, 'B', 100, 10'010));
  feed(r, add(14, 4, 'S', 100, 10'000));
  EXPECT_EQ(r.stats().crossed, 0u);
  EXPECT_EQ(r.stats().crossed_not_trading, 2u);
}

// Once trading resumes, a cross is an error again.
TEST(ReplayTest, CrossAfterResumingIsAnError) {
  Replayer r;
  feed(r, trading_action(13, 'H'));
  feed(r, trading_action(13, 'T'));
  feed(r, add(13, 1, 'B', 100, 10'010));
  feed(r, add(13, 2, 'S', 100, 10'000));
  EXPECT_EQ(r.stats().crossed, 1u);
  EXPECT_EQ(r.stats().crossed_not_trading, 0u);
}

TEST(ReplayTest, DecodeFailureIsCounted) {
  Replayer r;
  feed(r, add(13, 1, 'X', 100, 10'000));  // side 'X' is not B/S
  EXPECT_EQ(r.stats().decode_failures, 1u);
  EXPECT_EQ(r.book(13).order_count(), 0u);
}

}  // namespace
}  // namespace ob
