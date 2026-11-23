// chart_dump: one stock's 1 s, 15 s, 1 min and 5 min bars with every indicator and the
// market structure, as CSV, to compare with a trading chart and draw on the hub (D41, D49).
//
//   ./build/release/cpp/chart_dump data/itch/12302019.NASDAQ_ITCH50 AAPL out.csv
//                                  [--from 09:00] [--vwap-from 09:30] [--c 2]
//                                  [--swings swings.csv]
//
// --from: when bars start (pre-market warms the indicators up before the open).
// --vwap-from: when VWAP starts counting trades (09:30 = regular session).
// --c: a swing is a pullback of c x ATR, on every timeframe (D48); without it, each
//      timeframe's own (1.5 for 1 min, 2 for the others; D51).
// --swings: also write every confirmed swing, per timeframe.
// Bars stop at 16:00 ET.

#include "orderbook/itch/bytes.hpp"
#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/replay.hpp"
#include "orderbook/sim/chart_csv.hpp"
#include "orderbook/sim/market_state.hpp"
#include "orderbook/sim/trades.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

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

// "09:30" -> nanoseconds since midnight; nullopt if it isn't HH:MM.
std::optional<std::uint64_t> parse_hhmm(const std::string& s) {
  if (s.size() != 5 || s[2] != ':') {
    return std::nullopt;
  }
  for (const std::size_t i : {0, 1, 3, 4}) {
    if (s[i] < '0' || s[i] > '9') {
      return std::nullopt;
    }
  }
  const std::uint64_t hours = static_cast<std::uint64_t>((s[0] - '0') * 10 + (s[1] - '0'));
  const std::uint64_t minutes = static_cast<std::uint64_t>((s[3] - '0') * 10 + (s[4] - '0'));
  return hours * kHour + minutes * kMinute;
}

// "2.5" -> 2.5; nullopt unless the whole text is a number above 0.
std::optional<double> parse_positive(const std::string& s) {
  char* end = nullptr;
  const double x = std::strtod(s.c_str(), &end);  // end: where the number stopped
  if (s.empty() || end != s.c_str() + s.size() || !(x > 0.0)) {
    return std::nullopt;
  }
  return x;
}

void write_rows(std::ofstream& out, const char* timeframe,
                const std::vector<ob::sim::ChartPoint>& points, std::optional<double> vwap) {
  for (const auto& p : points) {
    ob::sim::write_chart_row(out, timeframe, p, vwap);
  }
}

// Rows for the bars one advance() closed, shortest timeframe first; the VWAP written is
// the one when they closed, before anything at a later time is counted.
void write_closed(std::ofstream& out, const ob::sim::ClosedBars& closed,
                  std::optional<double> vwap) {
  write_rows(out, "1s", closed.one_second, vwap);
  write_rows(out, "15s", closed.fifteen_seconds, vwap);
  write_rows(out, "1m", closed.one_minute, vwap);
  write_rows(out, "5m", closed.five_minutes, vwap);
}

void write_swings(std::ofstream& out, const char* timeframe, const ob::sim::ChartFeed& chart) {
  for (const auto& s : chart.structure().swings()) {
    ob::sim::write_swing_row(out, timeframe, s);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: chart_dump <itch_file> <SYMBOL> <out.csv> [--from HH:MM]"
                 " [--vwap-from HH:MM] [--c X] [--swings out.csv]\n";
    return 2;
  }
  const std::string path = argv[1];
  const std::string symbol = argv[2];
  std::uint64_t from = 9 * kHour;
  std::uint64_t vwap_from = 9 * kHour + 30 * kMinute;
  ob::sim::SwingSizes swing_c;  // the founder's picks unless --c sets one for all (D51)
  std::string swings_path;
  for (int i = 4; i < argc; i += 2) {
    const std::string flag = argv[i];
    const std::string value = i + 1 < argc ? argv[i + 1] : "";
    const auto t = parse_hhmm(value);
    const auto c = parse_positive(value);
    if (flag == "--from" && t) {
      from = *t;
    } else if (flag == "--vwap-from" && t) {
      vwap_from = *t;
    } else if (flag == "--c" && c) {
      swing_c = ob::sim::SwingSizes{.one_second = *c, .fifteen_seconds = *c,
                                    .one_minute = *c, .five_minutes = *c};
    } else if (flag == "--swings" && !value.empty()) {
      swings_path = value;
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
  ob::sim::write_chart_header(out);

  // The same class the simulator shows its strategies (D44), so the CSV is what they see.
  ob::sim::MarketState market(ob::sim::MarketStateConfig{
      .from_ns = from, .to_ns = end, .vwap_from_ns = vwap_from, .swing_c = swing_c});
  ob::Replayer replay;
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  std::optional<ob::Price> last_mid_x2;
  std::uint64_t trades = 0;
  while (reader.next(m) == ob::itch::ReadStatus::Ok) {
    const bool ours = m.length >= 11 && ob::itch::read_u16(m.data + 1) == *locate;
    if (!ours) {
      replay.apply(m.data, m.length);  // every stock's book must stay right
      continue;
    }
    const std::uint64_t t = ob::itch::read_u48(m.data + 5);
    write_closed(out, market.advance(t), market.vwap().value());
    // Before applying: an E's price is the resting order's, which the book may remove.
    const auto trade = ob::sim::trade_of(m.data, m.length, replay);
    if (trade) {
      market.on_trade(*trade);
      if (t >= from && t < end) {
        ++trades;
      }
    }
    replay.apply(m.data, m.length);
    const ob::OrderBook& book = replay.book(*locate);
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    if (bid && ask && *bid < *ask) {
      const ob::Price mid_x2 = *bid + *ask;  // twice the mid, so it stays an integer
      if (!last_mid_x2 || *last_mid_x2 != mid_x2) {
        last_mid_x2 = mid_x2;
        market.on_mid(t, mid_x2);
      }
    }
  }
  // Close the last bars, through 15:59:59 and 15:59.
  write_closed(out, market.advance(end), market.vwap().value());

  if (!swings_path.empty()) {
    std::ofstream swings(swings_path);
    if (!swings) {
      std::cerr << "cannot write " << swings_path << '\n';
      return 1;
    }
    ob::sim::write_swings_header(swings);
    write_swings(swings, "1s", market.one_second());
    write_swings(swings, "15s", market.fifteen_seconds());
    write_swings(swings, "1m", market.one_minute());
    write_swings(swings, "5m", market.five_minutes());
  }

  std::cerr << symbol << ": " << trades << " trades counted, bars written to " << argv[3]
            << '\n';
  return 0;
}
