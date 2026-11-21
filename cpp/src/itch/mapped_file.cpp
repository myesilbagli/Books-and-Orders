#include "orderbook/itch/mapped_file.hpp"

// POSIX (macOS, Linux) system calls; not available on Windows.
#include <fcntl.h>     // open
#include <sys/mman.h>  // mmap, madvise, munmap
#include <sys/stat.h>  // fstat
#include <unistd.h>    // close

#include <cerrno>
#include <cstring>  // strerror: turns an error number into text

namespace ob::itch {

namespace {

// "open data/x.itch: No such file or directory". Must run before close(), which can
// overwrite errno (the OS's "last error" number).
std::string describe(const char* step, const std::string& path) {
  return std::string(step) + " " + path + ": " + std::strerror(errno);
}

}  // namespace

MappedFile::MappedFile(const std::string& path) {
  // 1. Open for reading only; we never write to market data.
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    error_ = describe("open", path);
    return;
  }

  // 2. How big is it, and is it a normal file (not a folder)?
  struct stat st {};
  if (::fstat(fd, &st) != 0) {
    error_ = describe("fstat", path);
    ::close(fd);
    return;
  }
  if (!S_ISREG(st.st_mode)) {
    error_ = path + ": not a regular file";
    ::close(fd);
    return;
  }
  size_ = static_cast<std::size_t>(st.st_size);

  // 3. Map it. mmap refuses 0 bytes, so an empty file is simply open with no data.
  if (size_ > 0) {
    // PROT_READ: read-only pages. MAP_PRIVATE: our view only, never written back.
    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
      error_ = describe("mmap", path);
      size_ = 0;
      ::close(fd);
      return;
    }
    // A hint, not a requirement: "I'll read front to back", so the OS reads ahead
    // and can drop pages behind us. If it's ignored, nothing breaks.
    (void)::madvise(p, size_, MADV_SEQUENTIAL);
    data_ = static_cast<std::uint8_t*>(p);
  }

  // 4. The mapping keeps the file alive on its own; the descriptor isn't needed anymore.
  ::close(fd);
  open_ = true;
}

MappedFile::~MappedFile() {
  if (data_ != nullptr) {
    ::munmap(data_, size_);  // give the pages back: the "book returns itself"
  }
}

}  // namespace ob::itch
