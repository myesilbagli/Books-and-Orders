// chart_dump: one stock's 1-second and 1-minute bars with every indicator, as CSV, to
// compare with a trading chart (D41).
//
//   ./build/release/cpp/chart_dump data/itch/12302019.NASDAQ_ITCH50 AAPL out.csv
//                                  [--from 09:00] [--vwap-from 09:30]
//
// --from: when bars start (pre-market warms the indicators up before the open).
// --vwap-from: when VWAP starts counting trades (09:30 = regular session).
// Bars stop at 16:00 ET.

#include "orderbook/itch/bytes.hpp"
#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/replay.hpp"
#include "orderbook/sim/chart_csv.hpp"
#include "orderbook/sim/chart_feed.hpp"
#include "orderbook/sim/indicators.hpp"
#include "orderbook/sim/trades.hpp"

#include <cstdint>
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

// The two charts, written in time order: a 1 s row and the 1 min row that closed at the
// same moment come out together.
struct Charts {
  ob::sim::ChartFeed one_second{kSecond};
  ob::sim::ChartFeed one_minute{kMinute};
  ob::sim::Vwap vwap;
  std::ofstream& out;

  void write(const std::string& tf, const std::vector<ob::sim::ChartPoint>& points) {
    for (const auto& p : points) {
      ob::sim::write_chart_row(out, tf, p, vwap.value());
    }
  }
  // Close every bar that ended at or before t, before anything at t is added: what happens
  // at t belongs to the next bar, and the VWAP written must not include it yet.
  void advance(std::uint64_t t) {
    write("1s", one_second.advance(t));
    write("1m", one_minute.advance(t));
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: chart_dump <itch_file> <SYMBOL> <out.csv> [--from HH:MM]"
                 " [--vwap-from HH:MM]\n";
    return 2;
  }
  const std::string path = argv[1];
  const std::string symbol = argv[2];
  std::uint64_t from = 9 * kHour;
  std::uint64_t vwap_from = 9 * kHour + 30 * kMinute;
  for (int i = 4; i + 1 < argc; i += 2) {
    const std::string flag = argv[i];
    const auto t = parse_hhmm(argv[i + 1]);
    if (!t || (flag != "--from" && flag != "--vwap-from")) {
      std::cerr << "bad option: " << flag << ' ' << argv[i + 1] << '\n';
      return 2;
    }
    if (flag == "--from") {
      from = *t;
    } else {
      vwap_from = *t;
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

  Charts charts{.vwap = ob::sim::Vwap(vwap_from), .out = out};
  ob::Replayer replay;
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  std::optional<ob::Price> last_mid_x2;
  std::uint64_t trades = 0;
  while (reader.next(m) == ob::itch::ReadStatus::Ok) {
    const bool ours = m.length >= 11 && ob::itch::read_u16(m.data + 1) == *locate;
    const std::uint64_t t = m.length >= 11 ? ob::itch::read_u48(m.data + 5) : 0;
    if (!ours || t < from || t >= end) {
      replay.apply(m.data, m.length);  // every stock's book must stay right
      continue;
    }
    charts.advance(t);
    // Before applying: an E's price is the resting order's, which the book may remove.
    if (const auto trade = ob::sim::trade_of(m.data, m.length, replay)) {
      charts.vwap.on_trade(t, trade->price, trade->shares);
      charts.one_second.on_trade(t, trade->shares);
      charts.one_minute.on_trade(t, trade->shares);
      ++trades;
    }
    replay.apply(m.data, m.length);
    const ob::OrderBook& book = replay.book(*locate);
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    if (bid && ask && *bid < *ask) {
      const ob::Price mid_x2 = *bid + *ask;  // twice the mid, so it stays an integer
      if (!last_mid_x2 || *last_mid_x2 != mid_x2) {
        last_mid_x2 = mid_x2;
        charts.one_second.on_mid(t, mid_x2);
        charts.one_minute.on_mid(t, mid_x2);
      }
    }
  }
  charts.advance(end);  // close the last bars, through 15:59:59 and 15:59

  std::cerr << symbol << ": " << trades << " trades counted, bars written to " << argv[3]
            << '\n';
  return 0;
}
