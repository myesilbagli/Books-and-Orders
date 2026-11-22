#include "orderbook/itch/extract.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace ob::itch {
namespace {

using File = std::vector<std::uint8_t>;

// Appends one framed message (2-byte length, then the message) for a stock locate, and
// returns the offset of its type byte.
std::size_t frame(File& file, char type, std::size_t len, std::uint16_t locate) {
  file.push_back(static_cast<std::uint8_t>(len >> 8));
  file.push_back(static_cast<std::uint8_t>(len & 0xFF));
  const std::size_t at = file.size();
  file.push_back(static_cast<std::uint8_t>(type));
  file.insert(file.end(), len - 1, 0);
  file[at + 1] = static_cast<std::uint8_t>(locate >> 8);
  file[at + 2] = static_cast<std::uint8_t>(locate & 0xFF);
  return at;
}

// The types of the messages in a file, read back with the normal reader.
std::string types(const File& file) {
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  std::string out;
  while (reader.next(m) == ReadStatus::Ok) {
    out += static_cast<char>(m.data[0]);
  }
  return out;
}

constexpr std::uint16_t kAapl = 13;
constexpr std::uint16_t kMsft = 14;

TEST(ItchExtractTest, KeepsTheStockAndSystemMessages) {
  File in;
  frame(in, 'S', 12, 0);  // system event: start of day
  frame(in, 'A', 36, kAapl);
  frame(in, 'A', 36, kMsft);
  frame(in, 'D', 19, kAapl);
  frame(in, 'E', 31, kMsft);
  File out;
  const ExtractStats s = extract_stock(in.data(), in.size(), kAapl, out);
  EXPECT_EQ(types(out), "SAD");
  EXPECT_EQ(s.kept, 3u);
  EXPECT_EQ(s.skipped, 2u);
  EXPECT_EQ(s.end_status, ReadStatus::End);
}

// Copied byte for byte, length prefixes included: a file of only our stock comes out
// exactly as it went in.
TEST(ItchExtractTest, KeptMessagesAreByteIdentical) {
  File in;
  const std::size_t at = frame(in, 'A', 36, kAapl);
  for (std::size_t i = 3; i < 36; ++i) {
    in[at + i] = static_cast<std::uint8_t>(i * 7);  // arbitrary, non-zero content
  }
  frame(in, 'D', 19, kAapl);
  File out;
  (void)extract_stock(in.data(), in.size(), kAapl, out);
  EXPECT_EQ(out, in);
}

// A cut-off file: keep what came before the cut, and say how it ended.
TEST(ItchExtractTest, ReportsATruncatedInput) {
  File in;
  frame(in, 'D', 19, kAapl);
  frame(in, 'D', 19, kAapl);
  in.resize(in.size() - 5);
  File out;
  const ExtractStats s = extract_stock(in.data(), in.size(), kAapl, out);
  EXPECT_EQ(types(out), "D");
  EXPECT_EQ(s.end_status, ReadStatus::Truncated);
}

TEST(ItchExtractTest, FindsTheLocateByName) {
  File in;
  std::size_t at = frame(in, 'R', 39, kMsft);
  for (std::size_t i = 0; i < 8; ++i) {
    in[at + 11 + i] = static_cast<std::uint8_t>("MSFT    "[i]);
  }
  at = frame(in, 'R', 39, kAapl);
  for (std::size_t i = 0; i < 8; ++i) {
    in[at + 11 + i] = static_cast<std::uint8_t>("AAPL    "[i]);
  }
  EXPECT_EQ(find_locate(in.data(), in.size(), "AAPL"), kAapl);
  EXPECT_EQ(find_locate(in.data(), in.size(), "MSFT"), kMsft);
  EXPECT_FALSE(find_locate(in.data(), in.size(), "TSLA"));
}

}  // namespace
}  // namespace ob::itch
