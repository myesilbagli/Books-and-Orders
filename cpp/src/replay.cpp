#include "orderbook/replay.hpp"

namespace ob {

namespace {

constexpr std::size_t kLocates = 65'536;  // every value a 2-byte stock locate can take

}  // namespace

Replayer::Replayer() : books_(kLocates), symbols_(kLocates), state_(kLocates, 'T') {}

void Replayer::apply(const std::uint8_t* p, std::size_t len) {
  ++stats_.messages;
  if (len == 0) {
    ++stats_.decode_failures;  // the reader never hands out 0 bytes; just in case
    return;
  }

  // Each case decodes first; a message that doesn't decode is counted and skipped,
  // never half-applied. `if (const auto m = ...)` declares m and tests it in one go:
  // an empty optional is false.
  switch (p[0]) {
    case 'R': {
      if (const auto m = itch::decode_stock_directory(p, len)) {
        symbols_[m->header.stock_locate] = m->stock;
      } else {
        ++stats_.decode_failures;
      }
      break;
    }
    case 'H': {
      if (const auto m = itch::decode_trading_action(p, len)) {
        state_[m->header.stock_locate] = m->state;
      } else {
        ++stats_.decode_failures;
      }
      break;
    }
    case 'A':
    case 'F': {
      const auto m = itch::decode_add_order(p, len);
      if (!m) {
        ++stats_.decode_failures;
        break;
      }
      const std::uint16_t locate = m->header.stock_locate;
      const Order order{
          .id = m->ref, .side = m->side, .price = m->price, .remaining = m->shares, .seq = 0};
      if (books_[locate].add(order)) {
        check_crossed(locate, m->header.timestamp_ns);
      } else {
        ++stats_.rejected;  // duplicate id or 0 shares
      }
      break;
    }
    case 'E': {
      if (const auto m = itch::decode_order_executed(p, len)) {
        reduce(m->header.stock_locate, m->ref, m->executed_shares);
      } else {
        ++stats_.decode_failures;
      }
      break;
    }
    case 'C': {
      if (const auto m = itch::decode_order_executed_with_price(p, len)) {
        reduce(m->header.stock_locate, m->ref, m->executed_shares);
      } else {
        ++stats_.decode_failures;
      }
      break;
    }
    case 'X': {
      if (const auto m = itch::decode_order_cancel(p, len)) {
        reduce(m->header.stock_locate, m->ref, m->cancelled_shares);
      } else {
        ++stats_.decode_failures;
      }
      break;
    }
    case 'D': {
      const auto m = itch::decode_order_delete(p, len);
      if (!m) {
        ++stats_.decode_failures;
        break;
      }
      OrderBook& book = books_[m->header.stock_locate];
      if (!book.contains(m->ref)) {
        ++stats_.unknown_order;
      } else {
        [[maybe_unused]] const bool ok = book.cancel(m->ref);  // can't fail: it's there
      }
      break;
    }
    case 'U': {
      const auto m = itch::decode_order_replace(p, len);
      if (!m) {
        ++stats_.decode_failures;
        break;
      }
      const std::uint16_t locate = m->header.stock_locate;
      OrderBook& book = books_[locate];
      if (!book.contains(m->original_ref)) {
        ++stats_.unknown_order;
      } else if (book.replace(m->original_ref, m->new_ref, m->shares, m->price)) {
        check_crossed(locate, m->header.timestamp_ns);
      } else {
        ++stats_.rejected;  // new ref already used, or 0 shares
      }
      break;
    }
    default:
      break;  // P, Q, system events, halts...: no change to the displayed book
  }
}

void Replayer::reduce(std::uint16_t locate, OrderId id, Quantity qty) {
  OrderBook& book = books_[locate];
  if (!book.contains(id)) {
    ++stats_.unknown_order;
  } else if (!book.reduce(id, qty)) {
    ++stats_.rejected;  // more shares than the order has left: our count has drifted
  }
}

void Replayer::check_crossed(std::uint16_t locate, std::uint64_t timestamp_ns) {
  const OrderBook& book = books_[locate];
  const auto bid = book.best_bid();
  const auto ask = book.best_ask();
  // >=, not >: a locked book (bid == ask) on one venue is just as impossible, since
  // those two orders would have traded with each other.
  if (bid && ask && *bid >= *ask) {
    if (state_[locate] != 'T') {
      ++stats_.crossed_not_trading;  // halted / paused / quoting only: nothing matches
      return;
    }
    if (stats_.crossed == 0) {
      stats_.first_cross_locate = locate;
      stats_.first_cross_ns = timestamp_ns;
    }
    ++stats_.crossed;
  }
}

std::string Replayer::symbol_of(std::uint16_t locate) const {
  const itch::Symbol& s = symbols_[locate];
  // Never-named locates are all zero bytes; real symbols are space-padded on the right.
  std::size_t len = 0;
  while (len < s.size() && s[len] != ' ' && s[len] != '\0') {
    ++len;
  }
  return std::string(s.data(), len);
}

std::optional<std::uint16_t> Replayer::locate_of(const std::string& symbol) const {
  if (symbol.size() > 8) {
    return std::nullopt;
  }
  // ITCH symbols are padded with spaces to 8 bytes: "AAPL" is stored as "AAPL    ".
  itch::Symbol wanted;
  wanted.fill(' ');
  for (std::size_t i = 0; i < symbol.size(); ++i) {
    wanted[i] = symbol[i];
  }
  // A linear scan over 65,536 entries: fine for a lookup done once, not per message.
  for (std::size_t locate = 0; locate < symbols_.size(); ++locate) {
    if (symbols_[locate] == wanted) {
      return static_cast<std::uint16_t>(locate);
    }
  }
  return std::nullopt;
}

}  // namespace ob
