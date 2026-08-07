add_executable(orderbook_tests
    OrderBookTest.cpp
)

target_link_libraries(orderbook_tests
    PRIVATE
    orderbook_lib
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(orderbook_tests)
