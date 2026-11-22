// mm_sim: run the naive market-making simulation for one stock over an ITCH file and
// report fills, inventory, P&L and markouts.
//
//   ./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50 [fills.csv]
//
// The third argument is the latency in microseconds. Quoting runs 09:31-16:00 ET: the
// first minute is skipped because the opening auction (cross) works by different rules
// than the continuous book the simulator models.

#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/sim/simulator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;
constexpr std::uint64_t kHour = 3600 * kSecond;

// The stock directory ('R') comes at the very start of the file, before any orders, so
// finding a symbol's locate only reads a few thousand messages.
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
      return std::nullopt;  // orders started: the directory is over
    }
  }
  return std::nullopt;
}

std::string clock_text(std::uint64_t ns) {
  const std::uint64_t s = ns / kSecond;
  std::ostringstream out;
  out << std::setfill('0') << std::setw(2) << s / 3600 << ':' << std::setw(2) << s % 3600 / 60
      << ':' << std::setw(2) << s % 60;
  return out.str();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cerr << "usage: " << argv[0] << " <itch-file> <SYMBOL> <latency_us> [fills.csv]\n";
    return 2;
  }
  const std::string symbol = argv[2];
  const std::uint64_t latency_ns = std::strtoull(argv[3], nullptr, 10) * 1000;

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

  const ob::sim::SimConfig config{.locate = *locate,
                                  .quote_size = 100,
                                  .latency_ns = latency_ns,
                                  .max_inventory = 500,
                                  .start_ns = 9 * kHour + 31 * 60 * kSecond,
                                  .end_ns = 16 * kHour};
  ob::sim::Simulator sim(config);

  const auto start = std::chrono::steady_clock::now();
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  std::uint64_t last_ts = 0;
  while (reader.next(m) == ob::itch::ReadStatus::Ok) {
    sim.on_message(m.data, m.length);
    if (m.length >= 11) {
      last_ts = ob::itch::read_u48(m.data + 5);
    }
  }
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

  // Walk the fills once: counts, shares, and the largest position we held.
  const auto& fills = sim.fills();
  std::uint64_t buys = 0, sells = 0, bought = 0, sold = 0;
  std::int64_t position = 0, max_position = 0;
  for (const auto& f : fills) {
    if (f.side == ob::Side::Buy) {
      ++buys;
      bought += f.quantity;
      position += f.quantity;
    } else {
      ++sells;
      sold += f.quantity;
      position -= f.quantity;
    }
    max_position = std::max(max_position, position < 0 ? -position : position);
  }

  std::cout << symbol << " (locate " << *locate << "), latency " << latency_ns / 1000
            << " us, quoting 09:31 ET until " << clock_text(std::min(last_ts, config.end_ns))
            << " ET\n"
            << "fills:            " << fills.size() << "  (" << buys << " buys / " << bought
            << " sh, " << sells << " sells / " << sold << " sh)\n"
            << "inventory at end: " << sim.inventory() << " sh  (max |position| " << max_position
            << " sh)\n"
            << std::fixed << std::setprecision(2) << "P&L:              $" << sim.pnl_dollars()
            << "  (inventory marked at the 16:00 mid, no fees or rebates)\n";

  // Share-weighted average markout per horizon, in cents per share (1 cent = 100 ticks).
  const std::uint64_t horizons[] = {0, kSecond / 10, kSecond, 10 * kSecond, 60 * kSecond};
  const char* names[] = {"0", "100ms", "1s", "10s", "60s"};
  std::cout << "\nmarkout per share (cents; + good for us, - adverse selection)\n";
  for (std::size_t h = 0; h < 5; ++h) {
    double total = 0.0;
    std::uint64_t shares = 0;
    for (const auto& f : fills) {
      if (const auto mk = ob::sim::markout_ticks(f, sim.mids(), horizons[h])) {
        total += *mk * f.quantity;
        shares += f.quantity;
      }
    }
    std::cout << "  " << std::left << std::setw(6) << names[h] << std::right << std::setw(8)
              << std::setprecision(3) << (shares ? total / static_cast<double>(shares) / 100.0 : 0.0)
              << "\n";
  }
  std::cout << std::setprecision(1) << "\nrun time: " << elapsed.count() << " s\n";

  if (argc > 4) {
    std::ofstream csv(argv[4]);
    csv << "timestamp_ns,side,price,quantity,markout_1s_ticks,markout_10s_ticks\n";
    for (const auto& f : fills) {
      csv << f.timestamp_ns << ',' << (f.side == ob::Side::Buy ? 'B' : 'S') << ',' << f.price
          << ',' << f.quantity << ','
          << ob::sim::markout_ticks(f, sim.mids(), kSecond).value_or(0.0) << ','
          << ob::sim::markout_ticks(f, sim.mids(), 10 * kSecond).value_or(0.0) << '\n';
    }
  }
  return 0;
}
