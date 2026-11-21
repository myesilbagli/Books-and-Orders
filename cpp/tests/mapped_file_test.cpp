#include "orderbook/itch/mapped_file.hpp"

#include "orderbook/itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ob::itch {
namespace {

// Writes `bytes` to a fresh file in GoogleTest's temp folder and returns its path.
// fopen/fwrite take raw bytes as-is, so no casting is needed.
std::string write_temp_file(const std::string& name, const std::vector<std::uint8_t>& bytes) {
  const std::string path = ::testing::TempDir() + name;
  std::FILE* f = std::fopen(path.c_str(), "wb");  // "wb": write, binary, replace if it exists
  if (f == nullptr) {
    ADD_FAILURE() << "could not create " << path;
    return path;
  }
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return path;
}

TEST(MappedFileTest, MapsTheFileBytes) {
  const std::vector<std::uint8_t> bytes = {0x00, 0x13, 0x44, 0xFF, 0x07};
  const MappedFile f(write_temp_file("mapped_bytes.bin", bytes));

  ASSERT_TRUE(f.is_open()) << f.error();
  EXPECT_TRUE(f.error().empty());
  ASSERT_EQ(f.size(), bytes.size());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    EXPECT_EQ(f.data()[i], bytes[i]) << "byte " << i;
  }
}

TEST(MappedFileTest, MissingFileIsNotOpen) {
  const MappedFile f(::testing::TempDir() + "does_not_exist.itch");
  EXPECT_FALSE(f.is_open());
  EXPECT_FALSE(f.error().empty());  // says why, e.g. "No such file or directory"
  EXPECT_EQ(f.data(), nullptr);
  EXPECT_EQ(f.size(), 0u);
}

// The OS refuses to mmap 0 bytes, so an empty file is a special case: open, size 0.
TEST(MappedFileTest, EmptyFileOpensWithSizeZero) {
  const MappedFile f(write_temp_file("mapped_empty.bin", {}));
  ASSERT_TRUE(f.is_open()) << f.error();
  EXPECT_EQ(f.size(), 0u);
}

// A folder can be "opened" by the OS but isn't a file we can read as bytes.
TEST(MappedFileTest, DirectoryIsNotOpen) {
  const MappedFile f(::testing::TempDir());
  EXPECT_FALSE(f.is_open());
  EXPECT_FALSE(f.error().empty());
}

// End to end: a tiny ITCH file on disk, mapped, then split by the reader.
TEST(MappedFileTest, ReaderWalksAMappedFile) {
  std::vector<std::uint8_t> bytes = {0x00, 0x13, 'D'};  // length 19, type 'D'
  bytes.insert(bytes.end(), 18, 0);                    // rest of the 19-byte message
  bytes.insert(bytes.end(), {0x00, 0x17, 'X'});        // length 23, type 'X'
  bytes.insert(bytes.end(), 22, 0);

  const MappedFile f(write_temp_file("mapped_itch.bin", bytes));
  ASSERT_TRUE(f.is_open()) << f.error();

  ItchReader reader(f.data(), f.size());
  RawMessage m{};
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(m.data[0], 'D');
  ASSERT_EQ(reader.next(m), ReadStatus::Ok);
  EXPECT_EQ(m.data[0], 'X');
  EXPECT_EQ(reader.next(m), ReadStatus::End);
}

}  // namespace
}  // namespace ob::itch
