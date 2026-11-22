// mm_sim: run the naive market-making simulation for one stock over an ITCH file and
// report fills, inventory, P&L and markouts.
//
//   ./build/release/cpp/mm_sim data/itch/12302019.NASDAQ_ITCH50 AAPL 50 [fills.csv]
//                             [--as 0.2] [--pull 0.2] [--rebate 0.2]
//
// The third argument is the latency in microseconds. Without --as the naive strategy
// runs (join the touch); --as <cents> runs Avellaneda-Stoikov with gamma set so that,
// at AAPL's average volatility on 30 Dec 2019, one lot (one quote) leans the quotes by
// that many cents (D31). --pull <share> wraps either one: no quote on a side whose share
// of the shares at the touch is below <share> (D34). --rebate <cents> adds the maker
// rebate per share to the P&L breakdown (D36). Quoting runs 09:31-16:00 ET: the
// first minute is skipped because the opening auction (cross) works by different rules
// than the continuous book the simulator models.

#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/messages.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/sim/avellaneda_stoikov.hpp"
#include "orderbook/sim/pnl.hpp"
#include "orderbook/sim/pull_when_thin.hpp"
#include "orderbook/sim/simulator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace {

constexpr std::uint64_t kSecond = 1'000'000'000;
constexpr std::uint64_t kHour = 3600 * kSecond;

// AAPL's mid variance over 30 Dec 2019 on a 1 s grid (D29), in ticks^2 per second. Only
// used to turn "cents of lean per lot" into a gamma, so runs are easy to compare.
constexpr double kReferenceVariance = 40'000.0;

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

const std::uint64_t kHorizons[] = {0, kSecond / 10, kSecond, 10 * kSecond, 60 * kSecond};
const char* const kHorizonNames[] = {"0", "100ms", "1s", "10s", "60s"};

// One row per group of fills: how many, their share of all shares traded, and the
// share-weighted markout at each horizon in cents (1 cent = 100 ticks).
void print_group(const char* label, const std::vector<ob::sim::Fill>& group,
                 std::uint64_t all_shares, const std::vector<ob::sim::MidPoint>& mids) {
  std::uint64_t shares = 0;
  for (const auto& f : group) {
    shares += f.quantity;
  }
  std::cout << "  " << std::left << std::setw(12) << label << std::right << std::setw(7)
            << group.size() << std::fixed << std::setprecision(0) << std::setw(6)
            << (all_shares ? 100.0 * static_cast<double>(shares) / static_cast<double>(all_shares)
                           : 0.0)
            << "%" << std::setprecision(3);
  for (const auto h : kHorizons) {
    const auto mk = ob::sim::average_markout_ticks(group, mids, h);
    std::cout << std::setw(9) << (mk ? *mk / 100.0 : 0.0);
  }
  std::cout << "\n";
}

void print_header(const char* title) {
  std::cout << "\n" << title << "\n  " << std::left << std::setw(12) << "" << std::right
            << std::setw(7) << "fills" << std::setw(7) << "shares";
  for (const char* name : kHorizonNames) {
    std::cout << std::setw(9) << name;
  }
  std::cout << "\n";
}

// One line of the P&L breakdown: a part at each of the three horizons, in dollars and in
// cents per share. Values come in ticks x shares (1 tick = $0.0001; 1 cent = 100 ticks).
void print_parts(const char* name, const std::array<double, 3>& ticks, std::uint64_t shares) {
  std::cout << "  " << std::left << std::setw(19) << name << std::right;
  for (const double t : ticks) {
    const double cents_per_share = shares ? t / static_cast<double>(shares) / 100.0 : 0.0;
    std::cout << std::fixed << std::setprecision(0) << std::setw(9) << t / 10'000.0
              << std::setprecision(2) << std::setw(7) << cents_per_share;
  }
  std::cout << "\n";
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
    std::cerr << "usage: " << argv[0]
              << " <itch-file> <SYMBOL> <latency_us> [fills.csv] [--as <cents per lot>]"
              << " [--pull <touch share>] [--rebate <cents per share>]\n";
    return 2;
  }
  const std::string symbol = argv[2];
  const std::uint64_t latency_ns = std::strtoull(argv[3], nullptr, 10) * 1000;
  std::optional<std::string> csv_path;
  std::optional<double> as_lean_cents;
  std::optional<double> pull_share;
  double rebate_cents = 0.0;  // what the exchange pays per share of liquidity we add
  for (int i = 4; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--as" && i + 1 < argc) {
      as_lean_cents = std::strtod(argv[++i], nullptr);
    } else if (arg == "--pull" && i + 1 < argc) {
      pull_share = std::strtod(argv[++i], nullptr);
    } else if (arg == "--rebate" && i + 1 < argc) {
      rebate_cents = std::strtod(argv[++i], nullptr);
    } else {
      csv_path = arg;
    }
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

  const ob::sim::SimConfig config{.locate = *locate,
                                  .quote_size = 100,
                                  .latency_ns = latency_ns,
                                  .max_inventory = 500,
                                  .start_ns = 9 * kHour + 31 * 60 * kSecond,
                                  .end_ns = 16 * kHour};
  // The strategy has to outlive the simulator (it's used by reference), so it's made
  // first. unique_ptr<Strategy> holds either kind; it deletes the object when main ends.
  std::unique_ptr<ob::sim::Strategy> strategy;
  std::string strategy_name = "naive (join the touch)";
  if (as_lean_cents) {
    ob::sim::AsConfig as{.gamma = 0.0};
    // lean per lot = gamma x variance x H, in ticks; 1 cent = 100 ticks.
    as.gamma = *as_lean_cents * 100.0 / (kReferenceVariance * as.holding_s);
    as.lot = static_cast<std::int64_t>(config.quote_size);
    strategy = std::make_unique<ob::sim::AvellanedaStoikov>(as);
    std::ostringstream name;
    name << "Avellaneda-Stoikov, " << *as_lean_cents << " cents lean per lot at average vol"
         << " (gamma " << as.gamma << ", H " << as.holding_s << " s)";
    strategy_name = name.str();
  } else {
    strategy = std::make_unique<ob::sim::NaiveStrategy>();
  }
  // The pull rule wraps whichever strategy was picked. Declared after `strategy`, so it's
  // destroyed first: it never outlives the strategy it points to.
  std::unique_ptr<ob::sim::PullWhenThin> pull;
  ob::sim::Strategy* driver = strategy.get();
  if (pull_share) {
    pull = std::make_unique<ob::sim::PullWhenThin>(*strategy, *pull_share);
    driver = pull.get();
    std::ostringstream name;
    name << strategy_name << ", pulled below " << *pull_share * 100 << "% of the touch";
    strategy_name = name.str();
  }
  ob::sim::Simulator sim(config, *driver);

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
            << "strategy:         " << strategy_name << "\n"
            << "fills:            " << fills.size() << "  (" << buys << " buys / " << bought
            << " sh, " << sells << " sells / " << sold << " sh)\n"
            << "inventory at end: " << sim.inventory() << " sh  (max |position| " << max_position
            << " sh)\n"
            << "quote messages:   " << sim.quote_messages() << "  ("
            << std::fixed << std::setprecision(1)
            << (fills.empty() ? 0.0
                              : static_cast<double>(sim.quote_messages()) /
                                    static_cast<double>(fills.size()))
            << " per fill)\n"
            << std::setprecision(2) << "P&L:              $" << sim.pnl_dollars()
            << "  (inventory marked at the 16:00 mid, no fees or rebates)\n";

  // Where the P&L came from (D35): the spread we earned, the move against us within the
  // horizon (adverse selection), and holding the position after it (inventory). Shown for
  // three horizons: a longer one moves loss from "inventory" to "adverse selection".
  {
    const std::uint64_t hs[] = {kSecond, 10 * kSecond, 60 * kSecond};
    ob::sim::PnlBreakdown parts[3];
    for (int i = 0; i < 3; ++i) {
      // 1 cent = 100 ticks; the rebate is the same at every horizon
      parts[i] = ob::sim::pnl_breakdown(fills, sim.mids(), hs[i], config.end_ns,
                                        rebate_cents * 100.0);
    }
    const std::uint64_t shares = parts[0].shares;
    std::cout << "\nP&L breakdown ($ and cents per share), adverse selection measured over\n  "
              << std::setw(19) << "" << std::setw(16) << "1 s" << std::setw(16) << "10 s"
              << std::setw(16) << "60 s" << "\n";
    print_parts("spread captured",
                {parts[0].spread, parts[1].spread, parts[2].spread}, shares);
    print_parts("adverse selection",
                {parts[0].adverse_selection, parts[1].adverse_selection,
                 parts[2].adverse_selection},
                shares);
    print_parts("inventory", {parts[0].inventory, parts[1].inventory, parts[2].inventory},
                shares);
    print_parts("total", {parts[0].total, parts[1].total, parts[2].total}, shares);
    if (rebate_cents != 0.0) {
      print_parts("maker rebate", {parts[0].rebate, parts[1].rebate, parts[2].rebate}, shares);
      print_parts("total + rebate",
                  {parts[0].total + parts[0].rebate, parts[1].total + parts[1].rebate,
                   parts[2].total + parts[2].rebate},
                  shares);
    }
    // The breakdown recomputes the P&L from the fills alone; it must match the simulator.
    const double gap = parts[0].total / 10'000.0 - sim.pnl_dollars();
    std::cout << "  check: total vs simulator P&L " << std::setprecision(2)
              << (gap < 0.01 && gap > -0.01 ? "agree" : "DISAGREE") << "\n";
  }

  // Markouts for all fills, then split by what the book looked like when we were filled
  // (D33): is a fill from a thin touch, or with nobody left behind us, worse?
  const std::uint64_t all_shares = bought + sold;
  print_header("markout per share (cents; + good for us, - adverse selection)");
  print_group("all", fills, all_shares, sim.mids());

  print_header("by our side's share of the touch (thin -> thick)");
  for (int b = 0; b < 5; ++b) {
    std::vector<ob::sim::Fill> group;
    for (const auto& f : fills) {
      const int bucket = std::min(static_cast<int>(f.touch_share * 5), 4);
      if (bucket == b) {
        group.push_back(f);
      }
    }
    const std::string label = std::to_string(b * 20) + "-" + std::to_string(b * 20 + 20) + "%";
    print_group(label.c_str(), group, all_shares, sim.mids());
  }

  print_header("by real shares left at our price after the fill (none -> many)");
  const std::uint64_t edges[] = {0, 1, 500, 2000, UINT64_MAX};
  const char* labels[] = {"0", "1-499", "500-1999", "2000+"};
  for (int b = 0; b < 4; ++b) {
    std::vector<ob::sim::Fill> group;
    for (const auto& f : fills) {
      if (f.shares_left >= edges[b] && f.shares_left < edges[b + 1]) {
        group.push_back(f);
      }
    }
    print_group(labels[b], group, all_shares, sim.mids());
  }
  std::cout << std::setprecision(1) << "\nrun time: " << elapsed.count() << " s\n";

  if (csv_path) {
    std::ofstream csv(*csv_path);
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
