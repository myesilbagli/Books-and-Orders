#include "orderbook/sim/trades.hpp"

#include "orderbook/itch/messages.hpp"

namespace ob::sim {

std::optional<Trade> trade_of(const std::uint8_t* p, std::size_t len, const Replayer& replay) {
  if (len < 1) {
    return std::nullopt;
  }
  // Check the type byte first: most messages are adds and cancels, and this skips them
  // without decoding anything.
  if (p[0] == 'E') {
    const auto m = itch::decode_order_executed(p, len);
    if (!m) {
      return std::nullopt;
    }
    const Order* resting = replay.book(m->header.stock_locate).find(m->ref);
    if (resting == nullptr) {
      return std::nullopt;  // the replay counts it as an unknown order (D21)
    }
    return Trade{.timestamp_ns = m->header.timestamp_ns,
                 .locate = m->header.stock_locate,
                 .price = resting->price,
                 .shares = m->executed_shares};
  }
  if (p[0] == 'C') {
    const auto m = itch::decode_order_executed_with_price(p, len);
    if (!m || !m->printable) {
      return std::nullopt;
    }
    return Trade{.timestamp_ns = m->header.timestamp_ns,
                 .locate = m->header.stock_locate,
                 .price = m->execution_price,
                 .shares = m->executed_shares};
  }
  if (p[0] == 'P') {
    const auto m = itch::decode_non_cross_trade(p, len);
    if (!m) {
      return std::nullopt;
    }
    return Trade{.timestamp_ns = m->header.timestamp_ns,
                 .locate = m->header.stock_locate,
                 .price = m->price,
                 .shares = m->shares};
  }
  return std::nullopt;
}

}  // namespace ob::sim
