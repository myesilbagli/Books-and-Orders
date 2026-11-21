# A real library now that the book has a .cpp. Order's one-liners stay in the
# header so the compiler can still inline them.
add_library(orderbook STATIC
  src/book.cpp
  src/itch/messages.cpp
)
target_include_directories(orderbook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
orderbook_warnings(orderbook)

add_executable(orderbook_tests
  tests/order_test.cpp
  tests/book_test.cpp
  tests/match_test.cpp
  tests/itch_bytes_test.cpp
  tests/itch_messages_test.cpp
)
target_link_libraries(orderbook_tests PRIVATE orderbook GTest::gtest_main)
orderbook_warnings(orderbook_tests)

include(GoogleTest)
# Registers each TEST() with CTest separately, so `ctest` shows them one by one.
gtest_discover_tests(orderbook_tests)
