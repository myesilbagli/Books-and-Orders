#include "orderbook/sim/taker.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace ob::sim {

std::optional<double> sweep_price(const OrderBook& book, Side aggressor, Quantity qty,
                                  Price tick, int max_ticks) {
  const bool buying = aggressor == Side::Buy;
  const Side resting = buying ? Side::Sell : Side::Buy;  // a buy takes the asks
  const std::optional<Price> best = buying ? book.best_ask() : book.best_bid();
  if (!best || qty == 0) {
    return std::nullopt;
  }
  // The book has no "next level" lookup, so step a tick at a time; empty prices give 0.
  double paid = 0.0;
  Quantity left = qty;
  for (int k = 0; k < max_ticks && left > 0; ++k) {
    const Price p = *best + (buying ? k : -k) * tick;
    const std::uint64_t there = book.volume_at(resting, p);
    const auto take = static_cast<Quantity>(std::min<std::uint64_t>(left, there));
    paid += static_cast<double>(take) * static_cast<double>(p);
    left -= take;
  }
  if (left > 0) {
    return std::nullopt;  // too thin within reach: no fill rather than an invented price
  }
  return paid / static_cast<double>(qty);
}

TakerBot::TakerBot(TakerConfig config) : config_(config) {}

void TakerBot::on_read(std::uint64_t t_ns, int direction) {
  if (direction == 0 || state_ != State::Flat || t_ns < config_.from_ns ||
      t_ns >= config_.to_ns) {
    return;
  }
  open_ = TakerTrade{.direction = direction > 0 ? 1 : -1, .signal_ns = t_ns, .entry_ns = 0,
                     .entry_price = 0.0, .exit_ns = 0, .exit_price = 0.0,
                     .quantity = config_.size};
  due_ns_ = t_ns + config_.latency_ns;
  state_ = State::Entering;
}

void TakerBot::before_message(std::uint64_t now_ns, const OrderBook& book) {
  if (state_ == State::Flat || now_ns < due_ns_) {
    return;
  }
  if (state_ == State::Entering) {
    const auto price =
        sweep_price(book, open_.direction > 0 ? Side::Buy : Side::Sell, config_.size, config_.tick);
    if (!price) {
      due_ns_ = now_ns;  // nothing to take: try again at the next message
      return;
    }
    open_.entry_ns = due_ns_;  // the book hasn't changed since the order arrived
    open_.entry_price = *price;
    // The exit is sent hold_ns after the entry and reaches the book latency_ns later.
    due_ns_ = open_.entry_ns + config_.hold_ns + config_.latency_ns;
    state_ = State::Holding;
    return;
  }
  if (!close(due_ns_, book)) {
    due_ns_ = now_ns;
  }
}

void TakerBot::finish(std::uint64_t end_ns, const OrderBook& book) {
  if (state_ == State::Holding) {
    close(end_ns, book);
  }
  state_ = State::Flat;  // an entry never filled is simply dropped
}

bool TakerBot::close(std::uint64_t now_ns, const OrderBook& book) {
  const auto price =
      sweep_price(book, open_.direction > 0 ? Side::Sell : Side::Buy, open_.quantity, config_.tick);
  if (!price) {
    return false;
  }
  open_.exit_ns = now_ns;
  open_.exit_price = *price;
  trades_.push_back(open_);
  state_ = State::Flat;
  return true;
}

void write_taker_csv(std::ostream& out, const std::vector<TakerTrade>& trades) {
  out << "signal_ns,direction,entry_ns,entry_price,exit_ns,exit_price,quantity\n";
  for (const TakerTrade& t : trades) {
    // Prices formatted in their own stream, so `out` keeps its default number format.
    std::ostringstream prices;
    prices << std::fixed << std::setprecision(2) << t.entry_price << ',' << t.exit_ns << ','
           << t.exit_price;
    out << t.signal_ns << ',' << t.direction << ',' << t.entry_ns << ',' << prices.str() << ','
        << t.quantity << '\n';
  }
}

}  // namespace ob::sim
