// taker_sim: a taker on the replay (round 7f, D65). When a turn read fires (B1 by default)
// inside the entry window, cross the spread with a market order after a latency, hold,
// cross back. Fills from the real book at the moment each order arrives. One position at a
// time. Writes the trades; fees and statistics are left to research/taker_summary.py.
//
//   ./build/release/cpp/taker_sim data/itch/20260204.AAPL.dbn.ITCH50 AAPL out.csv
//       [--read b1|b2] [--from 09:30] [--to 10:30] [--latency-us 100000] [--hold-s 120]
//       [--size 100]

#include "orderbook/itch/bytes.hpp"
#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/replay.hpp"
#include "orderbook/sim/market_state.hpp"
#include "orderbook/sim/taker.hpp"
#include "orderbook/sim/trades.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;
constexpr std::uint64_t kMinute = 60 * kSecond;
constexpr std::uint64_t kHour = 60 * kMinute;

// Same as in mm_sim: the stock directory comes before any order.
std::optional<std::uint16_t> find_locate(const ob::itch::MappedFile& file,
                                         const std::string& symbol) {
  ob::itch::Symbol wanted;
  wanted.fill(' ');
  for (std::size_t i = 0; i < symbol.size() && i < wanted.size(); ++i) {
    wanted[i] = symbol[i];
  }
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  while (reader.next(m) == ob::itch::ReadStatus::Ok) {
    if (m.data[0] == 'R') {
      const auto dir = ob::itch::decode_stock_directory(m.data, m.length);
      if (dir && dir->stock == wanted) {
        return dir->header.stock_locate;
      }
    } else if (m.data[0] == 'A') {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> parse_hhmm(const std::string& s) {
  if (s.size() != 5 || s[2] != ':') {
    return std::nullopt;
  }
  for (const std::size_t i : {0, 1, 3, 4}) {
    if (s[i] < '0' || s[i] > '9') {
      return std::nullopt;
    }
  }
  return static_cast<std::uint64_t>((s[0] - '0') * 10 + (s[1] - '0')) * kHour +
         static_cast<std::uint64_t>((s[3] - '0') * 10 + (s[4] - '0')) * kMinute;
}

// A whole number >= 0, or nullopt.
std::optional<std::uint64_t> parse_count(const std::string& s) {
  char* end = nullptr;
  const unsigned long long x = std::strtoull(s.c_str(), &end, 10);
  if (s.empty() || end != s.c_str() + s.size()) {
    return std::nullopt;
  }
  return x;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: taker_sim <itch_file> <SYMBOL> <out.csv> [--read b1|b2] [--from HH:MM]"
                 " [--to HH:MM] [--latency-us N] [--hold-s N] [--size N]\n";
    return 2;
  }
  const std::string path = argv[1];
  const std::string symbol = argv[2];
  ob::sim::TakerConfig config;
  bool use_b2 = false;
  for (int i = 4; i + 1 < argc; i += 2) {
    const std::string flag = argv[i];
    const std::string value = argv[i + 1];
    const auto t = parse_hhmm(value);
    const auto n = parse_count(value);
    if (flag == "--read" && (value == "b1" || value == "b2")) {
      use_b2 = value == "b2";
    } else if (flag == "--from" && t) {
      config.from_ns = *t;
    } else if (flag == "--to" && t) {
      config.to_ns = *t;
    } else if (flag == "--latency-us" && n) {
      config.latency_ns = *n * 1'000;
    } else if (flag == "--hold-s" && n) {
      config.hold_ns = *n * kSecond;
    } else if (flag == "--size" && n && *n > 0) {
      config.size = static_cast<ob::Quantity>(*n);
    } else {
      std::cerr << "bad option: " << flag << ' ' << value << '\n';
      return 2;
    }
  }
  const std::uint64_t end = 16 * kHour;

  const ob::itch::MappedFile file(path);
  if (!file.is_open()) {
    std::cerr << "cannot open " << path << ": " << file.error() << '\n';
    return 1;
  }
  const auto locate = find_locate(file, symbol);
  if (!locate) {
    std::cerr << symbol << ": not in the stock directory\n";
    return 1;
  }
  std::ofstream out(argv[3]);
  if (!out) {
    std::cerr << "cannot write " << argv[3] << '\n';
    return 1;
  }

  // The same charts and reads mm_sim and chart_dump use (the founder's settings).
  ob::sim::MarketState market;
  ob::sim::TakerBot bot(config);
  // Hand the bot each read a closed 15 s bar completed, at the bar's end (when known).
  const auto reads = [&](const ob::sim::ClosedBars& closed) {
    for (const auto& p : closed.fifteen_seconds) {
      const auto& read = use_b2 ? p.b2 : p.b1;
      if (read && *read != 0) {
        bot.on_read(p.bar.end_ns, *read);
      }
    }
  };
  ob::Replayer replay;
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  std::optional<ob::Price> last_mid_x2;
  while (reader.next(m) == ob::itch::ReadStatus::Ok) {
    const bool ours = m.length >= 11 && ob::itch::read_u16(m.data + 1) == *locate;
    if (!ours) {
      replay.apply(m.data, m.length);  // every stock's book must stay right
      continue;
    }
    const std::uint64_t t = ob::itch::read_u48(m.data + 5);
    reads(market.advance(t));
    // Before this message changes the book: an order that arrived since the last one sees
    // the book exactly as it is now.
    bot.before_message(t, replay.book(*locate));
    const auto trade = ob::sim::trade_of(m.data, m.length, replay);
    if (trade) {
      market.on_trade(*trade);
    }
    replay.apply(m.data, m.length);
    const ob::OrderBook& book = replay.book(*locate);
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    if (bid && ask && *bid < *ask) {
      const ob::Price mid_x2 = *bid + *ask;
      if (!last_mid_x2 || *last_mid_x2 != mid_x2) {
        last_mid_x2 = mid_x2;
        market.on_mid(t, mid_x2);
      }
    }
  }
  reads(market.advance(end));
  bot.finish(end, replay.book(*locate));
  ob::sim::write_taker_csv(out, bot.trades());
  std::cerr << symbol << ": " << bot.trades().size() << " trades written to " << argv[3] << '\n';
  return 0;
}
