#include "orderbook/sim/bars.hpp"

#include <algorithm>

namespace ob::sim {

BarBuilder::BarBuilder(std::uint64_t interval_ns)
    // A zero interval would make advance() loop forever.
    : interval_ns_(std::max<std::uint64_t>(interval_ns, 1)) {}

std::vector<Bar> BarBuilder::advance(std::uint64_t t_ns) {
  std::vector<Bar> closed;  // no allocation unless a bar actually closes
  if (!current_) {
    return closed;
  }
  // `<=`: a bar is [start, end), so at t == end it's over and t belongs to the next one.
  while (current_->end_ns <= t_ns) {
    closed.push_back(*current_);
    // The next bar starts flat at the last mid. If nothing happens in it, it stays flat
    // with no volume: a quiet second is still a second (the same rule as D29).
    const Price last = current_->close_x2;
    const std::uint64_t start = current_->end_ns;
    current_ = Bar{.start_ns = start,
                   .end_ns = start + interval_ns_,
                   .open_x2 = last,
                   .high_x2 = last,
                   .low_x2 = last,
                   .close_x2 = last,
                   .volume = 0};
  }
  return closed;
}

std::vector<Bar> BarBuilder::on_mid(std::uint64_t t_ns, Price mid_x2) {
  std::vector<Bar> closed = advance(t_ns);
  if (!current_) {
    // First mid: start the whole interval that contains it.
    const std::uint64_t start = t_ns / interval_ns_ * interval_ns_;
    current_ = Bar{.start_ns = start,
                   .end_ns = start + interval_ns_,
                   .open_x2 = mid_x2,
                   .high_x2 = mid_x2,
                   .low_x2 = mid_x2,
                   .close_x2 = mid_x2,
                   .volume = 0};
    return closed;
  }
  if (t_ns == current_->start_ns) {
    // A change exactly at the start is the mid in force at the start: it's the open,
    // not a move within the bar (else the old mid would show up as this bar's low/high).
    current_->open_x2 = current_->high_x2 = current_->low_x2 = current_->close_x2 = mid_x2;
    return closed;
  }
  current_->high_x2 = std::max(current_->high_x2, mid_x2);
  current_->low_x2 = std::min(current_->low_x2, mid_x2);
  current_->close_x2 = mid_x2;
  return closed;
}

std::vector<Bar> BarBuilder::on_trade(std::uint64_t t_ns, std::uint64_t shares) {
  std::vector<Bar> closed = advance(t_ns);
  if (current_) {
    current_->volume += shares;
  }
  return closed;
}

}  // namespace ob::sim
