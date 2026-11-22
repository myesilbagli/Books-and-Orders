// imbalance_study: does the queue imbalance at the touch predict the next mid move? (D32)
//
//   ./build/release/cpp/imbalance_study data/itch/12302019.NASDAQ_ITCH50 AAPL [grid_ms]
//
// Replays the whole file, samples the stock's touch every grid_ms (default 100) from
// 09:31 to 16:00 ET, and prints, per imbalance decile, the average mid change 1 s and
// 10 s later next to what the weighted mid predicted.

#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/replay.hpp"
#include "orderbook/sim/imbalance.hpp"

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMillisecond = 1'000'000;
constexpr std::uint64_t kSecond = 1000 * kMillisecond;
constexpr std::uint64_t kHour = 3600 * kSecond;

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

ob::sim::BookSample snapshot(const ob::OrderBook& book, std::uint64_t t) {
  const auto bid = book.best_bid();
  const auto ask = book.best_ask();
  return ob::sim::BookSample{.timestamp_ns = t,
                             .best_bid = bid.value_or(0),
                             .best_ask = ask.value_or(0),
                             .bid_size = bid ? book.volume_at(ob::Side::Buy, *bid) : 0,
                             .ask_size = ask ? book.volume_at(ob::Side::Sell, *ask) : 0};
}

void print_table(const std::vector<ob::sim::BookSample>& samples, std::size_t horizon,
                 const char* name) {
  const auto table = ob::sim::imbalance_table(samples, horizon, 10);
  std::cout << "\nmid change " << name << " later, by imbalance I = bid size / (bid + ask)\n"
            << "  I            pairs   predicted  realized   up     down   (cents)\n";
  for (const auto& b : table) {
    std::cout << std::fixed << std::setprecision(1) << "  " << b.low << "-" << b.high
              << std::setw(11) << b.count << std::setprecision(3) << std::setw(11)
              << b.mean_predicted_ticks / 100.0 << std::setw(10) << b.mean_change_ticks / 100.0
              << std::setprecision(0) << std::setw(6) << b.up_share * 100 << "%" << std::setw(6)
              << b.down_share * 100 << "%\n";
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <itch-file> <SYMBOL> [grid_ms]\n";
    return 2;
  }
  const std::string symbol = argv[2];
  const std::uint64_t grid_ns =
      (argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 100) * kMillisecond;
  if (grid_ns == 0) {
    std::cerr << "grid_ms must be > 0\n";
    return 2;
  }

  const ob::itch::MappedFile file(argv[1]);
  if (!file.is_open()) {
    std::cerr << file.error() << "\n";
    return 1;
  }
  const auto locate = find_locate(file, symbol);
  if (!locate) {
    std::cerr << symbol << ": not in the stock directory\n";
    return 1;
  }

  const std::uint64_t start = 9 * kHour + 31 * 60 * kSecond;
  const std::uint64_t end = 16 * kHour;
  std::vector<ob::sim::BookSample> samples;
  samples.reserve((end - start) / grid_ns + 1);

  ob::Replayer replay;
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  std::uint64_t next_grid = start;
  while (reader.next(m) == ob::itch::ReadStatus::Ok) {
    if (m.length >= 11) {
      const std::uint64_t t = ob::itch::read_u48(m.data + 5);
      // Before applying a message from after a grid point, the book is exactly as it was
      // at that point: every message stamped at or before it is already in.
      while (next_grid <= end && t > next_grid) {
        samples.push_back(snapshot(replay.book(*locate), next_grid));
        next_grid += grid_ns;
      }
    }
    replay.apply(m.data, m.length);
  }

  // How wide the touch is matters: the weighted mid can only move within the spread.
  std::size_t usable = 0;
  std::size_t by_cents[5] = {};  // 1, 2, 3, 4, 5+ cents (anything not on the cent grid: 5+)
  for (const auto& s : samples) {
    if (ob::sim::usable(s)) {
      ++usable;
      const auto spread = s.best_ask - s.best_bid;
      const auto cents = spread % 100 == 0 ? spread / 100 : 5;
      ++by_cents[cents >= 1 && cents <= 4 ? cents - 1 : 4];
    }
  }
  std::cout << symbol << ": " << samples.size() << " samples every " << grid_ns / kMillisecond
            << " ms, 09:31-16:00 ET; " << usable << " usable\nspread:";
  const char* labels[] = {"1c", "2c", "3c", "4c", "5c+"};
  for (int i = 0; i < 5; ++i) {
    const double share =
        usable ? static_cast<double>(by_cents[i]) / static_cast<double>(usable) : 0.0;
    std::cout << "  " << labels[i] << " " << std::fixed << std::setprecision(0) << share * 100
              << "%";
  }
  std::cout << "\n";

  print_table(samples, kSecond / grid_ns, "1 s");
  print_table(samples, 10 * kSecond / grid_ns, "10 s");
  return 0;
}
