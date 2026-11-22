# A real library now that the book has a .cpp. Order's one-liners stay in the
# header so the compiler can still inline them.
add_library(orderbook STATIC
  src/book.cpp
  src/replay.cpp
  src/sim/queue.cpp
  src/sim/simulator.cpp
  src/sim/markout.cpp
  src/sim/volatility.cpp
  src/sim/avellaneda_stoikov.cpp
  src/sim/imbalance.cpp
  src/sim/pnl.cpp
  src/itch/messages.cpp
  src/itch/mapped_file.cpp
  src/itch/scan.cpp
)
target_include_directories(orderbook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
orderbook_warnings(orderbook)

add_executable(orderbook_tests
  tests/order_test.cpp
  tests/book_test.cpp
  tests/match_test.cpp
  tests/reduce_replace_test.cpp
  tests/itch_bytes_test.cpp
  tests/itch_messages_test.cpp
  tests/itch_reader_test.cpp
  tests/mapped_file_test.cpp
  tests/itch_scan_test.cpp
  tests/replay_test.cpp
  tests/sim_queue_test.cpp
  tests/simulator_test.cpp
  tests/markout_test.cpp
  tests/volatility_test.cpp
  tests/avellaneda_stoikov_test.cpp
  tests/imbalance_test.cpp
  tests/pull_when_thin_test.cpp
  tests/pnl_test.cpp
)
target_link_libraries(orderbook_tests PRIVATE orderbook GTest::gtest_main)
orderbook_warnings(orderbook_tests)

include(GoogleTest)
# Registers each TEST() with CTest separately, so `ctest` shows them one by one.
gtest_discover_tests(orderbook_tests)

# Command-line tools that run on real ITCH files (not part of the tests).
add_executable(itch_stats tools/itch_stats.cpp)
target_link_libraries(itch_stats PRIVATE orderbook)
orderbook_warnings(itch_stats)

add_executable(itch_replay tools/itch_replay.cpp)
target_link_libraries(itch_replay PRIVATE orderbook)
orderbook_warnings(itch_replay)

add_executable(mm_sim tools/mm_sim.cpp)
target_link_libraries(mm_sim PRIVATE orderbook)
orderbook_warnings(mm_sim)

add_executable(imbalance_study tools/imbalance_study.cpp)
target_link_libraries(imbalance_study PRIVATE orderbook)
orderbook_warnings(imbalance_study)
