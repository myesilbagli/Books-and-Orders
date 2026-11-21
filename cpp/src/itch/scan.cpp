#include "orderbook/itch/scan.hpp"

#include "orderbook/itch/messages.hpp"

namespace ob::itch {

namespace {

// Runs the decoder for the types we decode (the 8 book types plus R).
// true = decoded, or not one of ours (nothing to check).
bool decodes(const RawMessage& m) {
  switch (m.data[0]) {
    case 'A':
    case 'F':  // both go through decode_add_order
      return decode_add_order(m.data, m.length).has_value();
    case 'E':
      return decode_order_executed(m.data, m.length).has_value();
    case 'C':
      return decode_order_executed_with_price(m.data, m.length).has_value();
    case 'X':
      return decode_order_cancel(m.data, m.length).has_value();
    case 'D':
      return decode_order_delete(m.data, m.length).has_value();
    case 'U':
      return decode_order_replace(m.data, m.length).has_value();
    case 'P':
      return decode_non_cross_trade(m.data, m.length).has_value();
    case 'R':
      return decode_stock_directory(m.data, m.length).has_value();
    default:
      return true;  // system events, halts, ...: counted, not decoded
  }
}

}  // namespace

ScanStats scan(const std::uint8_t* data, std::size_t size) {
  ScanStats s;
  ItchReader reader(data, size);
  RawMessage m{};
  ReadStatus status;
  while ((status = reader.next(m)) == ReadStatus::Ok) {
    ++s.messages;
    ++s.count_by_type[m.data[0]];  // the type byte is the index (0..255)
    if (!decodes(m)) {
      ++s.decode_failures;
    }
  }
  s.bytes = reader.offset();
  s.end_status = status;  // End, or why the reader stopped early
  return s;
}

}  // namespace ob::itch
