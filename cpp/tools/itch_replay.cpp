// itch_replay: replay one ITCH 5.0 file into one order book per stock, then report
// the validation counters, the speed, and the top of book for chosen symbols.
//
//   ./build/release/cpp/itch_replay data/itch/12302019.NASDAQ_ITCH50 AAPL QQQ MSFT
//
// Exit code 0 only if the file ended cleanly and every problem counter is 0.

#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/reader.hpp"
#include "orderbook/replay.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

using ob::Price;

// "289.4300" from 2894300 ticks of $0.0001; "-" when that side of the book is empty.
std::string price_text(std::optional<Price> p) {
  if (!p) {
    return "-";
  }
  std::ostringstream out;
  out << *p / 10'000 << '.' << std::setw(4) << std::setfill('0') << *p % 10'000;
  return out.str();
}

// Nanoseconds since midnight -> "09:44:05.123456789" (ET, the exchange's clock).
std::string clock_text(std::uint64_t ns) {
  const std::uint64_t s = ns / 1'000'000'000;
  std::ostringstream out;
  out << std::setfill('0') << std::setw(2) << s / 3600 << ':' << std::setw(2) << s % 3600 / 60
      << ':' << std::setw(2) << s % 60 << '.' << std::setw(9) << ns % 1'000'000'000;
  return out.str();
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <itch-file> [SYMBOL ...]\n";
    return 2;
  }

  const auto start = std::chrono::steady_clock::now();

  const ob::itch::MappedFile file(argv[1]);
  if (!file.is_open()) {
    std::cerr << file.error() << "\n";
    return 1;
  }

  ob::Replayer replay;
  ob::itch::ItchReader reader(file.data(), file.size());
  ob::itch::RawMessage m{};
  ob::itch::ReadStatus status;
  while ((status = reader.next(m)) == ob::itch::ReadStatus::Ok) {
    replay.apply(m.data, m.length);
  }

  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
  const double seconds = elapsed.count();
  const ob::ReplayStats& s = replay.stats();

  // Orders still resting in all books at the end of the file.
  std::uint64_t resting = 0;
  for (std::uint32_t locate = 0; locate < 65'536; ++locate) {
    resting += replay.book(static_cast<std::uint16_t>(locate)).order_count();
  }

  std::cout << "messages:         " << s.messages << "\n"
            << "end of file:      "
            << (status == ob::itch::ReadStatus::End ? "End (clean)" : "Truncated / Invalid")
            << "\n"
            << "decode failures:  " << s.decode_failures << "\n"
            << "unknown orders:   " << s.unknown_order << "\n"
            << "rejected:         " << s.rejected << "\n"
            << "crossed, trading: " << s.crossed;
  if (s.crossed > 0) {
    std::cout << "   (first: " << replay.symbol_of(s.first_cross_locate) << " at "
              << clock_text(s.first_cross_ns) << ")";
  }
  std::cout << "\ncrossed, halted:  " << s.crossed_not_trading
            << "   (expected: no matching while halted / paused)";
  std::cout << "\nresting orders:   " << resting << " at the end\n"
            << std::fixed << std::setprecision(2) << "time:             " << seconds << " s\n"
            << "speed:            " << static_cast<double>(s.messages) / seconds / 1e6
            << " M messages/s (" << seconds * 1e9 / static_cast<double>(s.messages)
            << " ns per message)\n";

  if (argc > 2) {
    std::cout << "\nsymbol    bid          ask          orders  levels(bid/ask)\n";
  }
  for (int i = 2; i < argc; ++i) {
    const auto locate = replay.locate_of(argv[i]);
    if (!locate) {
      std::cout << std::left << std::setw(10) << argv[i] << "not in the stock directory\n";
      continue;
    }
    const ob::OrderBook& book = replay.book(*locate);
    std::cout << std::left << std::setw(10) << argv[i] << std::setw(13)
              << price_text(book.best_bid()) << std::setw(13) << price_text(book.best_ask())
              << std::setw(8) << book.order_count() << book.level_count(ob::Side::Buy) << "/"
              << book.level_count(ob::Side::Sell) << "\n";
  }

  const bool clean = status == ob::itch::ReadStatus::End && s.decode_failures == 0 &&
                     s.unknown_order == 0 && s.rejected == 0 && s.crossed == 0;
  return clean ? 0 : 1;
}
