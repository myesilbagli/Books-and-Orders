#include "orderbook/itch/extract.hpp"

#include "orderbook/itch/bytes.hpp"
#include "orderbook/itch/messages.hpp"

namespace ob::itch {

ExtractStats extract_stock(const std::uint8_t* data, std::size_t size, std::uint16_t locate,
                           std::vector<std::uint8_t>& out) {
  ExtractStats s;
  ItchReader reader(data, size);
  RawMessage m{};
  while ((s.end_status = reader.next(m)) == ReadStatus::Ok) {
    // Too short to have a header: no locate to judge by, so it can't be ours.
    const std::uint16_t loc = m.length >= 11 ? read_u16(m.data + 1) : 0xFFFF;
    if (loc != locate && loc != 0) {
      ++s.skipped;
      continue;
    }
    // The 2-byte length prefix sits right before the message: copy both as they are.
    out.insert(out.end(), m.data - 2, m.data + m.length);
    ++s.kept;
  }
  return s;
}

std::optional<std::uint16_t> find_locate(const std::uint8_t* data, std::size_t size,
                                         const std::string& symbol) {
  Symbol wanted;
  wanted.fill(' ');  // ITCH pads symbols with spaces to 8 characters
  for (std::size_t i = 0; i < symbol.size() && i < wanted.size(); ++i) {
    wanted[i] = symbol[i];
  }
  ItchReader reader(data, size);
  RawMessage m{};
  while (reader.next(m) == ReadStatus::Ok) {
    if (m.data[0] == 'R') {
      const auto dir = decode_stock_directory(m.data, m.length);
      if (dir && dir->stock == wanted) {
        return dir->header.stock_locate;
      }
    } else if (m.data[0] == 'A') {
      return std::nullopt;  // the directory comes before any order: we're past it
    }
  }
  return std::nullopt;
}

}  // namespace ob::itch
