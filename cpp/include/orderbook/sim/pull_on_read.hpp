#pragma once

#include "orderbook/sim/strategy.hpp"

#include <cstdint>

// A strategy that wraps another one (like PullWhenThin, D33) and takes away the side a
// turn read (D63) says is about to be picked off: while B1 reads down, no bid (whoever
// sells to us is selling ahead of a fall); while it reads up, no ask. Only inside a time
// window, so "B1 at the open" can be tried on its own (round 7d-3, D66).

namespace ob::sim {

enum class TurnRead : std::uint8_t { B1, B2 };

class PullOnRead final : public Strategy {
 public:
  // `inner` is used by reference and must outlive this object. Reads count in
  // [from_ns, to_ns), ns since midnight ET.
  PullOnRead(Strategy& inner, TurnRead read, std::uint64_t from_ns, std::uint64_t to_ns)
      : inner_(inner), read_(read), from_ns_(from_ns), to_ns_(to_ns) {}

  Quotes quote(const MarketView& view) override {
    Quotes q = inner_.quote(view);  // always asked: it may keep state between calls
    if (view.market == nullptr || view.now_ns < from_ns_ || view.now_ns >= to_ns_) {
      return q;
    }
    const int read = read_ == TurnRead::B1 ? view.market->b1(view.now_ns)
                                           : view.market->b2(view.now_ns);
    if (read < 0) {
      q.bid.reset();  // a fall is read: a buy now is the fill that loses
    } else if (read > 0) {
      q.ask.reset();
    }
    return q;
  }

 private:
  Strategy& inner_;
  TurnRead read_;
  std::uint64_t from_ns_;
  std::uint64_t to_ns_;
};

}  // namespace ob::sim
