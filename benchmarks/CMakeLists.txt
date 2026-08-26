add_executable(orderbook_benchmarks
    OrderBookBenchmark.cpp
)

target_link_libraries(orderbook_benchmarks
    PRIVATE
    orderbook_lib
    benchmark::benchmark
)
