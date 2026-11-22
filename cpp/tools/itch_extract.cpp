// itch_extract: one stock's slice of an ITCH file, same format (D43).
//
//   ./build/release/cpp/itch_extract data/itch/12302019.NASDAQ_ITCH50 AAPL data/itch/12302019.AAPL.ITCH50
//
// Every tool (mm_sim, chart_dump, ...) runs on the output unchanged, in seconds.

#include "orderbook/itch/extract.hpp"
#include "orderbook/itch/mapped_file.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: itch_extract <itch_file> <SYMBOL> <out_file>\n";
    return 2;
  }
  const ob::itch::MappedFile file(argv[1]);
  if (!file.is_open()) {
    std::cerr << "cannot open " << argv[1] << ": " << file.error() << '\n';
    return 1;
  }
  const auto locate = ob::itch::find_locate(file.data(), file.size(), argv[2]);
  if (!locate) {
    std::cerr << argv[2] << ": not in the stock directory\n";
    return 1;
  }
  std::vector<std::uint8_t> out;
  const ob::itch::ExtractStats s = ob::itch::extract_stock(file.data(), file.size(), *locate, out);
  if (s.end_status != ob::itch::ReadStatus::End) {
    std::cerr << "input didn't end cleanly; kept what came before\n";
  }
  std::ofstream f(argv[3], std::ios::binary);
  f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
  if (!f) {
    std::cerr << "cannot write " << argv[3] << '\n';
    return 1;
  }
  std::cerr << argv[2] << " (locate " << *locate << "): kept " << s.kept << " of "
            << s.kept + s.skipped << " messages, " << out.size() / 1'000'000 << " MB\n";
  return 0;
}
