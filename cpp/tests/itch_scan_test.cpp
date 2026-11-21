#include "orderbook/itch/scan.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace ob::itch {
namespace {

// Appends one framed message: 2-byte length, type byte, then zeros.
// Returns the offset of the type byte, so a test can patch fields after it.
std::size_t frame(std::vector<std::uint8_t>& file, char type, std::size_t len) {
  file.push_back(static_cast<std::uint8_t>(len >> 8));
  file.push_back(static_cast<std::uint8_t>(len & 0xFF));
  const std::size_t at = file.size();
  file.push_back(static_cast<std::uint8_t>(type));
  file.insert(file.end(), len - 1, 0);
  return at;
}

std::uint64_t count(const ScanStats& s, char type) {
  return s.count_by_type[static_cast<std::uint8_t>(type)];
}

TEST(ItchScanTest, EmptyFile) {
  const ScanStats s = scan(nullptr, 0);
  EXPECT_EQ(s.messages, 0u);
  EXPECT_EQ(s.bytes, 0u);
  EXPECT_EQ(s.end_status, ReadStatus::End);
}

// 'S' (system event, 12 bytes) is real ITCH that we count but don't decode.
TEST(ItchScanTest, CountsEachTypeAndDecodesOurs) {
  std::vector<std::uint8_t> file;
  frame(file, 'S', 12);
  const std::size_t add = frame(file, 'A', 36);
  file[add + 19] = 'B';  // a valid side, so the decoder accepts it
  frame(file, 'D', 19);
  frame(file, 'D', 19);

  const ScanStats s = scan(file.data(), file.size());
  EXPECT_EQ(s.messages, 4u);
  EXPECT_EQ(count(s, 'S'), 1u);
  EXPECT_EQ(count(s, 'A'), 1u);
  EXPECT_EQ(count(s, 'D'), 2u);
  EXPECT_EQ(s.decode_failures, 0u);
  EXPECT_EQ(s.bytes, file.size());
  EXPECT_EQ(s.end_status, ReadStatus::End);
}

// An 'A' with side 'X' is framed fine but the decoder must reject it, and the
// scan must say so: on real data this number should be 0.
TEST(ItchScanTest, CountsDecodeFailures) {
  std::vector<std::uint8_t> file;
  const std::size_t add = frame(file, 'A', 36);
  file[add + 19] = 'X';

  const ScanStats s = scan(file.data(), file.size());
  EXPECT_EQ(count(s, 'A'), 1u);
  EXPECT_EQ(s.decode_failures, 1u);
}

// A message of one of our types at the wrong length is also a decode failure.
TEST(ItchScanTest, WrongLengthForKnownTypeIsAFailure) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 20);  // 'D' must be 19

  const ScanStats s = scan(file.data(), file.size());
  EXPECT_EQ(s.decode_failures, 1u);
}

// A half-downloaded file: what came before is counted, and the scan reports where
// and why it stopped instead of pretending the day is complete.
TEST(ItchScanTest, StopsAtATruncatedMessage) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 19);
  frame(file, 'D', 19);
  const std::size_t whole = file.size();
  frame(file, 'A', 36);
  file.resize(whole + 10);

  const ScanStats s = scan(file.data(), file.size());
  EXPECT_EQ(s.messages, 2u);
  EXPECT_EQ(s.bytes, whole);
  EXPECT_EQ(s.end_status, ReadStatus::Truncated);
}

}  // namespace
}  // namespace ob::itch
