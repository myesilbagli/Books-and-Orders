#include "orderbook/sim/market_state.hpp"

#include <algorithm>
#include <utility>

namespace ob::sim {

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;

void append(std::vector<ChartPoint>& to, const std::vector<ChartPoint>& from) {
  to.insert(to.end(), from.begin(), from.end());
}

// The shared chart settings with one timeframe's own swing size.
ChartSettings with_c(ChartSettings settings, double c) {
  settings.structure.c = c;  // settings is a copy: the caller's stays as it was
  return settings;
}

}  // namespace

MarketState::MarketState(const MarketStateConfig& config)
    : config_(config),
      one_second_(kSecond, with_c(config.settings, config.swing_c.one_second)),
      fifteen_seconds_(15 * kSecond, with_c(config.settings, config.swing_c.fifteen_seconds)),
      one_minute_(60 * kSecond, with_c(config.settings, config.swing_c.one_minute)),
      five_minutes_(300 * kSecond, with_c(config.settings, config.swing_c.five_minutes)),
      vwap_(config.vwap_from_ns),
      day_(config.vwap_from_ns) {}

void MarketState::take(ClosedBars fresh) {
  read_turns(fresh);
  append(pending_.one_second, fresh.one_second);
  append(pending_.fifteen_seconds, fresh.fifteen_seconds);
  append(pending_.one_minute, fresh.one_minute);
  append(pending_.five_minutes, fresh.five_minutes);
}

void MarketState::read_turns(ClosedBars& fresh) {
  // One call can close several 15 s bars and a 1 min bar between them (a quiet stretch).
  // Walk them in time order, so each 15 s bar's context has only the 1 min bars that had
  // closed by its end: no look-ahead.
  std::size_t m = 0;  // the next new 1 min point
  const auto catch_up = [&](std::uint64_t until_ns) {
    while (m < fresh.one_minute.size() && fresh.one_minute[m].bar.end_ns <= until_ns) {
      context_1m_ = fresh.one_minute[m++];
    }
  };
  const std::vector<Swing>& swings = one_minute_.structure().swings();
  for (ChartPoint& p : fresh.fifteen_seconds) {
    // Read this bar with the context as it was when the bar opened.
    p.b1 = b1_.on_bar(p.bar, next_.levels, next_.atr);
    p.b2 = b2_.on_bar(p.bar, next_.trend, next_.levels, next_.atr);
    day_.on_bar(p.bar);

    // Then the context for the next bar, as of this one's end.
    const std::uint64_t now = p.bar.end_ns;
    catch_up(now);
    // Swings are added as they are confirmed, so those known by now are a prefix.
    const auto known = std::partition_point(
        swings.begin(), swings.end(), [now](const Swing& s) { return s.confirmed_ns <= now; });
    const ChartPoint none;
    const ChartPoint& ctx = context_1m_ ? *context_1m_ : none;
    next_ = ReadContext{
        .levels = levels_at(std::span<const Swing>(swings.begin(), known), day_, vwap_.value(),
                            ctx.ema_mid, static_cast<double>(p.bar.close_x2) / 2.0),
        .atr = ctx.atr,
        .trend = ema_stack(ctx)};
  }
  catch_up(UINT64_MAX);  // 1 min bars after the last 15 s one: next time's context
}

void MarketState::start_if_due(std::uint64_t t_ns) {
  if (started_ || t_ns < config_.from_ns) {
    return;
  }
  started_ = true;
  if (carried_mid_x2_) {
    take(ClosedBars{
        .one_second = one_second_.on_mid(config_.from_ns, *carried_mid_x2_),
        .fifteen_seconds = fifteen_seconds_.on_mid(config_.from_ns, *carried_mid_x2_),
        .one_minute = one_minute_.on_mid(config_.from_ns, *carried_mid_x2_),
        .five_minutes = five_minutes_.on_mid(config_.from_ns, *carried_mid_x2_)});
  }
}

ClosedBars MarketState::advance(std::uint64_t t_ns) {
  start_if_due(t_ns);
  if (t_ns >= config_.from_ns) {
    // Past the end, close only up to the end: the bars after it don't exist.
    const std::uint64_t until = std::min(t_ns, config_.to_ns);
    take(ClosedBars{.one_second = one_second_.advance(until),
                    .fifteen_seconds = fifteen_seconds_.advance(until),
                    .one_minute = one_minute_.advance(until),
                    .five_minutes = five_minutes_.advance(until)});
  }
  // Hand out everything closed since the last call, including bars an on_trade or on_mid
  // closed on the way (if advance wasn't called first), so none is ever lost.
  ClosedBars out = pending_;  // a copy: a handful of bars at most
  pending_ = {};
  return out;
}

void MarketState::on_trade(const Trade& trade) {
  start_if_due(trade.timestamp_ns);
  if (!inside(trade.timestamp_ns)) {
    return;
  }
  // The bars first (a trade at a bar's end belongs to the next bar), then the VWAP, so a
  // bar closed here is read with the VWAP of the trades before it.
  ClosedBars fresh{
      .one_second = one_second_.on_trade(trade.timestamp_ns, trade.shares),
      .fifteen_seconds = fifteen_seconds_.on_trade(trade.timestamp_ns, trade.shares),
      .one_minute = one_minute_.on_trade(trade.timestamp_ns, trade.shares),
      .five_minutes = five_minutes_.on_trade(trade.timestamp_ns, trade.shares)};
  take(std::move(fresh));
  vwap_.on_trade(trade.timestamp_ns, trade.price, trade.shares);
}

void MarketState::on_mid(std::uint64_t t_ns, Price mid_x2) {
  if (t_ns < config_.from_ns) {
    carried_mid_x2_ = mid_x2;  // not charted yet, but it may still be in force at the start
    return;
  }
  start_if_due(t_ns);
  if (!inside(t_ns)) {
    return;
  }
  take(ClosedBars{.one_second = one_second_.on_mid(t_ns, mid_x2),
                  .fifteen_seconds = fifteen_seconds_.on_mid(t_ns, mid_x2),
                  .one_minute = one_minute_.on_mid(t_ns, mid_x2),
                  .five_minutes = five_minutes_.on_mid(t_ns, mid_x2)});
}

}  // namespace ob::sim
