#include "orderbook/itch/reader.hpp"

#include "orderbook/itch/messages.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace ob::itch {
namespace {

// Appends one message the way the ITCH file stores it: a 2-byte big-endian length,
// then the message bytes. The first byte of `body` is the type, the rest is zeros.
void frame(std::vector<std::uint8_t>& file, char type, std::size_t len) {
  file.push_back(static_cast<std::uint8_t>(len >> 8));
  file.push_back(static_cast<std::uint8_t>(len & 0xFF));
  file.push_back(static_cast<std::uint8_t>(type));
  file.insert(file.end(), len - 1, 0);  // len - 1 zero bytes after the type
}

TEST(ItchReaderTest, EmptyFileIsEnd) {
  const std::vector<std::uint8_t> file;
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  EXPECT_EQ(reader.next(m), ReadStatus::End);
}

// [00 13][19 bytes 'D'][00 24][36 bytes 'A'] -> 'D', then 'A', then End.
TEST(ItchReaderTest, SplitsMessagesByTheirLengthPrefix) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 19);
  frame(file, 'A', 36);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};

  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(m.length, 19u);
  EXPECT_EQ(m.data[0], 'D');

  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(m.length, 36u);
  EXPECT_EQ(m.data[0], 'A');

  EXPECT_EQ(reader.next(m), ReadStatus::End);
}

// Zero-copy: the message points into the file's own bytes, right after the prefix.
TEST(ItchReaderTest, MessagePointsIntoTheFileWithoutCopying) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 19);
  frame(file, 'X', 23);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};

  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(m.data, file.data() + 2);  // skip the 2-byte prefix
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(m.data, file.data() + 2 + 19 + 2);
}

TEST(ItchReaderTest, OffsetCountsBytesConsumed) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 19);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  EXPECT_EQ(reader.offset(), 0u);
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(reader.offset(), 21u);  // 2 prefix + 19 message
}

// Only one byte left: not even a full length prefix. A half-downloaded file.
TEST(ItchReaderTest, HalfALengthPrefixIsTruncated) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 19);
  file.push_back(0x00);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(reader.next(m), ReadStatus::Truncated);
}

// The prefix says 36 bytes but the file ends after 20.
TEST(ItchReaderTest, MessageCutShortIsTruncated) {
  std::vector<std::uint8_t> file;
  frame(file, 'A', 36);
  file.resize(2 + 20);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  EXPECT_EQ(reader.next(m), ReadStatus::Truncated);
}

// A zero length would hand out a message with no type byte; ITCH never sends one.
TEST(ItchReaderTest, ZeroLengthIsInvalid) {
  const std::vector<std::uint8_t> file = {0x00, 0x00, 0x44};
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  EXPECT_EQ(reader.next(m), ReadStatus::Invalid);
}

// After an error the reader is stuck where it was: once we've lost track of where
// messages start, everything after is garbage, so it must not keep going.
TEST(ItchReaderTest, StaysStoppedAfterAnError) {
  std::vector<std::uint8_t> file;
  frame(file, 'A', 36);
  file.resize(2 + 20);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  ASSERT_EQ(reader.next(m), ReadStatus::Truncated);
  EXPECT_EQ(reader.next(m), ReadStatus::Truncated);
  EXPECT_EQ(reader.offset(), 0u);
}

TEST(ItchReaderTest, EndRepeats) {
  std::vector<std::uint8_t> file;
  frame(file, 'D', 19);
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(reader.next(m), ReadStatus::End);
  EXPECT_EQ(reader.next(m), ReadStatus::End);
}

// Reader + decoder together: what the replay loop will do for every message.
TEST(ItchReaderTest, ReaderOutputFeedsTheDecoder) {
  const std::vector<std::uint8_t> file = {
      0x00, 0x13,                                      // length 19
      0x44,                                            // 'D'
      0x00, 0x0D, 0x00, 0x00,                          // stock 13, tracking
      0x1F, 0x1A, 0xCE, 0xD9, 0xF0, 0x00,              // 09:30:00
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,  // order #7
  };
  ItchReader reader(file.data(), file.size());
  RawMessage m{};
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);

  const auto del = decode_order_delete(m.data, m.length);
  ASSERT_TRUE(del.has_value());
  EXPECT_EQ(del->ref, 7u);
}

}  // namespace
}  // namespace ob::itch
