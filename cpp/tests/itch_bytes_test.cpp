#include "orderbook/itch/bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace ob::itch {
namespace {

TEST(ItchBytesTest, ReadsU16BigEndian) {
  const std::uint8_t b[] = {0x01, 0x2C};  // 1 * 256 + 44
  EXPECT_EQ(read_u16(b), 300u);
}

// Shares = 100, and the price from the explanation: 150.2778 $ with 4 implied decimals.
TEST(ItchBytesTest, ReadsU32BigEndian) {
  const std::uint8_t shares[] = {0x00, 0x00, 0x00, 0x64};
  const std::uint8_t price[] = {0x00, 0x16, 0xEE, 0x3A};
  EXPECT_EQ(read_u32(shares), 100u);
  EXPECT_EQ(read_u32(price), 1'502'778u);
}

// ITCH timestamps are 6 bytes: nanoseconds since midnight. 09:30:00 = 34'200 s.
TEST(ItchBytesTest, ReadsU48Timestamp) {
  const std::uint8_t b[] = {0x1F, 0x1A, 0xCE, 0xD9, 0xF0, 0x00};
  EXPECT_EQ(read_u48(b), 34'200'000'000'000u);
}

// Distinct bytes catch a reader that gets the order reversed (little-endian).
TEST(ItchBytesTest, ReadsU64InCorrectByteOrder) {
  const std::uint8_t b[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  EXPECT_EQ(read_u64(b), 0x0102030405060708u);
}

// All 0xFF must give the maximum value, not a negative or truncated one.
// This is the classic bug when bytes are read as (signed) char.
TEST(ItchBytesTest, HighBitBytesStayUnsigned) {
  const std::uint8_t b[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  EXPECT_EQ(read_u16(b), 0xFFFFu);
  EXPECT_EQ(read_u32(b), 0xFFFF'FFFFu);
  EXPECT_EQ(read_u48(b), 0xFFFF'FFFF'FFFFu);
  EXPECT_EQ(read_u64(b), 0xFFFF'FFFF'FFFF'FFFFu);
}

// A reader must use exactly its own width and ignore the bytes after it.
TEST(ItchBytesTest, ReadsOnlyItsOwnWidth) {
  const std::uint8_t b[] = {0x01, 0x2C, 0xAB, 0xCD};
  EXPECT_EQ(read_u16(b), 300u);
}

}  // namespace
}  // namespace ob::itch
