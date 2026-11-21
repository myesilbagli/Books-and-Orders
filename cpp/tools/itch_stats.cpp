// itch_stats: map one ITCH 5.0 file, count its messages by type, check that our
// decoders accept every message of their types, and time the pass.
//
//   ./build/release/cpp/itch_stats data/itch/12302019.NASDAQ_ITCH50
//
// Exit code 0 only if the file ended cleanly and nothing failed to decode.

#include "orderbook/itch/mapped_file.hpp"
#include "orderbook/itch/scan.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace {

using ob::itch::ReadStatus;

// Message names from the ITCH 5.0 spec; the ones marked * change the order book.
const char* type_name(char type) {
  switch (type) {
    case 'S': return "system event";
    case 'R': return "stock directory";
    case 'H': return "trading action";
    case 'Y': return "Reg SHO restriction";
    case 'L': return "market participant position";
    case 'V': return "MWCB decline level";
    case 'W': return "MWCB status";
    case 'K': return "IPO quoting period";
    case 'J': return "LULD auction collar";
    case 'h': return "operational halt";
    case 'A': return "* add order";
    case 'F': return "* add order (attributed)";
    case 'E': return "* order executed";
    case 'C': return "* order executed with price";
    case 'X': return "* order cancel";
    case 'D': return "* order delete";
    case 'U': return "* order replace";
    case 'P': return "  trade (non-cross, hidden)";
    case 'Q': return "cross trade";
    case 'B': return "broken trade";
    case 'I': return "NOII (auction imbalance)";
    case 'N': return "RPII (retail interest)";
    case 'O': return "DLCR price discovery";
    default: return "(unknown)";
  }
}

const char* status_name(ReadStatus s) {
  switch (s) {
    case ReadStatus::Ok: return "Ok";
    case ReadStatus::End: return "End (clean)";
    case ReadStatus::Truncated: return "Truncated (file ends inside a message)";
    case ReadStatus::Invalid: return "Invalid (zero-length message)";
  }
  return "?";
}

}  // namespace

// argc = number of words on the command line, argv = the words; argv[0] is the
// program itself, argv[1] the file path.
int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <itch-file>\n";
    return 2;
  }

  // steady_clock never jumps backwards (unlike the wall clock), so it's the one to
  // time with. Mapping is included: on a cold run, page loads are the real cost.
  const auto start = std::chrono::steady_clock::now();

  const ob::itch::MappedFile file(argv[1]);
  if (!file.is_open()) {
    std::cerr << file.error() << "\n";
    return 1;
  }
  const ob::itch::ScanStats s = ob::itch::scan(file.data(), file.size());

  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
  const double seconds = elapsed.count();

  std::cout << "type  count           name\n";
  for (int t = 0; t < 256; ++t) {
    const std::uint64_t n = s.count_by_type[static_cast<std::size_t>(t)];
    if (n == 0) {
      continue;
    }
    const char c = static_cast<char>(t);
    std::cout << "  " << c << "   " << std::left << std::setw(14) << n << "  " << type_name(c)
              << "\n";
  }

  const double gb = static_cast<double>(s.bytes) / 1e9;
  std::cout << std::fixed << std::setprecision(2) << "\nmessages:        " << s.messages
            << "\nbytes:           " << gb << " GB of " << static_cast<double>(file.size()) / 1e9
            << " GB"
            << "\ndecode failures: " << s.decode_failures
            << "\nend:             " << status_name(s.end_status)
            << "\ntime:            " << seconds << " s"
            << "\nspeed:           " << static_cast<double>(s.messages) / seconds / 1e6
            << " M messages/s, " << gb / seconds << " GB/s\n";

  return (s.end_status == ReadStatus::End && s.decode_failures == 0) ? 0 : 1;
}
