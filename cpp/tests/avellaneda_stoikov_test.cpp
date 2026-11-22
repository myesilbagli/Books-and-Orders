#include "orderbook/sim/avellaneda_stoikov.hpp"

#include <gtest/gtest.h>

namespace ob::sim {
namespace {

// AAPL-like market: bid 285.48 / ask 285.49 (ticks of $0.0001), mid 285.485.
constexpr Price kBid = 2'854'800;
constexpr Price kAsk = 2'854'900;
constexpr Price kMidX2 = kBid + kAsk;

// Average AAPL variance on 30 Dec 2019 (D29): about 40'000 ticks^2/s (2 cents/sqrt s).
constexpr double kAverageVar = 40'000.0;

// gamma chosen so that at average volatility one lot costs 20 ticks (0.2 cents):
// risk = gamma * sigma^2 * H = 5e-5 * 40'000 * 10 = 20.
AsConfig config() { return AsConfig{.gamma = 5e-5}; }

// --- the formula -----------------------------------------------------------------------

// Flat: reservation = mid. Half spread 50 + 20/2 = 60 ticks: 285.4790 / 285.4910,
// which round to the touch, 285.48 / 285.49.
TEST(AvellanedaStoikovTest, FlatQuotesAreSymmetricAroundTheMid) {
  const Quotes q = as_quotes(kMidX2, 0.0, kAverageVar, config());
  EXPECT_EQ(q.bid, kBid);
  EXPECT_EQ(q.ask, kAsk);
}

// Long 4 lots: reservation 285.485 - 4 x 0.0020 = 285.4770. Bid 285.4710 -> 285.47,
// one cent back. Ask 285.4830 -> 285.48: the strategy would sell more eagerly (the
// simulator then holds it at the touch, D30).
TEST(AvellanedaStoikovTest, LongLeansBothQuotesDown) {
  const Quotes q = as_quotes(kMidX2, 4.0, kAverageVar, config());
  EXPECT_EQ(q.bid, kBid - 100);
  EXPECT_EQ(q.ask, kAsk - 100);
}

TEST(AvellanedaStoikovTest, ShortLeansBothQuotesUp) {
  const Quotes q = as_quotes(kMidX2, -4.0, kAverageVar, config());
  EXPECT_EQ(q.bid, kBid + 100);
  EXPECT_EQ(q.ask, kAsk + 100);
}

// Volatile morning, twice the variance: risk 40 ticks a lot. Long 4: reservation
// 285.4690, half spread 70 -> bid 285.4620 -> 285.46, two cents back.
TEST(AvellanedaStoikovTest, HigherVolatilityLeansHarder) {
  const Quotes q = as_quotes(kMidX2, 4.0, 2 * kAverageVar, config());
  EXPECT_EQ(q.bid, kBid - 200);
}

// Calm afternoon, 0.3x the variance: risk 6 ticks a lot. Long 4: reservation 285.4826,
// half spread 53 -> 285.4773 / 285.4879 -> still the touch. Holding is cheap: no lean.
TEST(AvellanedaStoikovTest, CalmMarketBarelyLeans) {
  const Quotes q = as_quotes(kMidX2, 4.0, 12'000.0, config());
  EXPECT_EQ(q.bid, kBid);
  EXPECT_EQ(q.ask, kAsk);
}

// No risk aversion: no lean and no extra width, whatever we hold. That's the naive quote.
TEST(AvellanedaStoikovTest, ZeroGammaIsTheNaiveQuote) {
  AsConfig c = config();
  c.gamma = 0.0;
  const Quotes q = as_quotes(kMidX2, 10.0, kAverageVar, c);
  EXPECT_EQ(q.bid, kBid);
  EXPECT_EQ(q.ask, kAsk);
}

// Prices land on the nearest tick of the grid, not always down or always up.
TEST(AvellanedaStoikovTest, RoundsToTheNearestTick) {
  AsConfig c = config();
  c.gamma = 0.0;
  c.base_half_spread_ticks = 40.0;  // bid 285.4810 -> 285.48 (down)
  EXPECT_EQ(as_quotes(kMidX2, 0.0, kAverageVar, c).bid, kBid);
  c.base_half_spread_ticks = 60.0;  // bid 285.4790 -> 285.48 (up)
  EXPECT_EQ(as_quotes(kMidX2, 0.0, kAverageVar, c).bid, kBid);
  c.base_half_spread_ticks = 140.0;  // bid 285.4710 -> 285.47
  EXPECT_EQ(as_quotes(kMidX2, 0.0, kAverageVar, c).bid, kBid - 100);
}

// --- the strategy object ---------------------------------------------------------------

constexpr std::uint64_t kSecond = 1'000'000'000;

MarketView view(std::uint64_t t, Price bid, std::int64_t inventory = 0) {
  return MarketView{.now_ns = t,
                    .best_bid = bid,
                    .best_ask = bid + 100,
                    .bid_size = 100,
                    .ask_size = 100,
                    .inventory = inventory};
}

AsConfig small_window() {
  AsConfig c = config();
  c.vol_window = 2;  // ready after 2 one-second changes
  return c;
}

TEST(AvellanedaStoikovTest, NoQuotesUntilVolatilityIsReady) {
  AvellanedaStoikov as(small_window());
  const std::uint64_t half = kSecond / 2;
  EXPECT_FALSE(as.quote(view(half, kBid)).bid.has_value());
  EXPECT_FALSE(as.quote(view(kSecond + half, kBid)).bid.has_value());      // first grid point
  EXPECT_FALSE(as.quote(view(2 * kSecond + half, kBid)).bid.has_value());  // change 1
  EXPECT_TRUE(as.quote(view(3 * kSecond + half, kBid)).bid.has_value());   // change 2: ready
}

// The object feeds its own volatility estimate and converts shares to lots.
TEST(AvellanedaStoikovTest, QuotesFromItsOwnVolatilityAndInventory) {
  AvellanedaStoikov as(small_window());
  const Price path[] = {kBid, kBid + 300, kBid, kBid + 300};  // a volatile few seconds
  for (std::uint64_t i = 0; i < 4; ++i) {
    (void)as.quote(view(i * kSecond + kSecond / 2, path[i]));
  }
  const MarketView now = view(4 * kSecond + kSecond / 2, kBid, 400);
  const Quotes got = as.quote(now);

  const auto var = as.volatility().variance_per_second();
  ASSERT_TRUE(var.has_value());
  EXPECT_GT(*var, 0.0);
  const Quotes want = as_quotes(now.best_bid + now.best_ask, 4.0, *var, small_window());
  ASSERT_TRUE(want.bid.has_value());
  EXPECT_EQ(got.bid, want.bid);
  EXPECT_EQ(got.ask, want.ask);
  EXPECT_LT(*got.bid, kBid);  // long and volatile: the bid steps back
}

}  // namespace
}  // namespace ob::sim
