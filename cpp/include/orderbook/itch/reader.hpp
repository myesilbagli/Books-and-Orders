#pragma once

#include <cstddef>
#include <cstdint>

#include "orderbook/itch/bytes.hpp"

// Splits a whole ITCH 5.0 file, already in memory (mmap), into messages.
// The file is a plain sequence of [2-byte big-endian length][message], with nothing
// in between. The reader walks it front to back and hands out pointers into the
// file's own memory (zero-copy); it never allocates and never decodes.

namespace ob::itch {

enum class ReadStatus : std::uint8_t {
  Ok,         // `out` holds the next message
  End,        // the file ended cleanly, between two messages
  Truncated,  // the file ended in the middle of a message or its length prefix
  Invalid,    // a length prefix of 0
};

struct RawMessage {
  const std::uint8_t* data;  // points at the type byte, inside the file's memory
  std::size_t length;
};

class ItchReader {
 public:
  ItchReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  // Hands out the next message, or says why there isn't one. On an error the position
  // doesn't move, so every later call reports the same error instead of reading garbage.
  // Defined here in the header: it runs once per message (~hundreds of millions a day),
  // so the compiler should be able to inline it (D6).
  [[nodiscard]] ReadStatus next(RawMessage& out) {
    const std::size_t left = size_ - pos_;  // never negative: pos_ only moves within size_
    if (left == 0) {
      return ReadStatus::End;  // checked first, so an empty file never touches data_
    }
    if (left < 2) {
      return ReadStatus::Truncated;  // not even a whole length prefix
    }
    const std::size_t len = read_u16(data_ + pos_);
    if (len == 0) {
      return ReadStatus::Invalid;
    }
    // Compare with what's left instead of computing pos_ + 2 + len, which could overflow.
    if (len > left - 2) {
      return ReadStatus::Truncated;
    }
    out = RawMessage{.data = data_ + pos_ + 2, .length = len};  // a pointer, no copy
    pos_ += 2 + len;
    return ReadStatus::Ok;
  }

  [[nodiscard]] std::size_t offset() const { return pos_; }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

}  // namespace ob::itch
