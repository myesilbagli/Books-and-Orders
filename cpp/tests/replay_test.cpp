#include "orderbook/replay.hpp"

#include "itch_builders.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace ob {
namespace {

using namespace itch_test;  // message builders: add(), executed(), deleted(), ...
using Ids = std::vector<OrderId>;

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

  // Every cross while trading is also logged, to investigate them all, not just the first.
  ASSERT_EQ(r.cross_events().size(), 1u);
  EXPECT_EQ(r.cross_events()[0].locate, 13u);
  EXPECT_EQ(r.cross_events()[0].timestamp_ns, 5555u);
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
  EXPECT_TRUE(r.cross_events().empty());  // expected crosses aren't logged
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
