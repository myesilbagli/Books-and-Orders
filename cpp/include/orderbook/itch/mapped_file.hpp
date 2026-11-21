#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// A read-only file shown as one array of bytes (mmap). The OS loads pages only when
// they're touched, so a 10 GB ITCH day never has to fit in RAM at once.
//
// RAII: the constructor maps the file, the destructor unmaps it, automatically, on
// every path out of the scope. Like borrowing a book that returns itself.

namespace ob::itch {

class MappedFile {
 public:
  // explicit: a path string can't silently turn into a MappedFile somewhere by accident.
  explicit MappedFile(const std::string& path);
  ~MappedFile();  // destructor: runs automatically when the object goes away

  // No copies: two objects owning the same mapping would both unmap it (double free).
  // = delete makes a copy a compile error instead of a crash at run time.
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  // Like std::ifstream: check is_open() first; error() says why it failed.
  [[nodiscard]] bool is_open() const { return open_; }
  [[nodiscard]] const std::string& error() const { return error_; }

  [[nodiscard]] const std::uint8_t* data() const { return data_; }
  [[nodiscard]] std::size_t size() const { return size_; }

 private:
  std::uint8_t* data_ = nullptr;  // nullptr when not open, or when the file is empty
  std::size_t size_ = 0;
  bool open_ = false;
  std::string error_;
};

}  // namespace ob::itch
