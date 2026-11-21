cmake_minimum_required(VERSION 3.24)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Stay on standard C++20; no GNU extensions, so the code builds the same on any compiler.
set(CMAKE_CXX_EXTENSIONS OFF)

# Sanitizers apply to everything in Debug, GoogleTest included:
# ASan reports false positives when only half of a program is instrumented.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# Strict warnings go on our own targets only (see cpp/), not on third-party code
# we don't control. This helper is called once per target.
function(orderbook_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

include(FetchContent)
# Pinned release + checksum: every clone builds against the exact same GoogleTest,
# and a tampered download fails the build instead of running.
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(cpp)
