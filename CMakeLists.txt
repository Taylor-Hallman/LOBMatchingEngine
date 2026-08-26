cmake_minimum_required(VERSION 3.25)
project(OrderBook)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(orderbook_lib 
    src/OrderBook.cpp
    src/Order.cpp
)

add_executable(orderbook_app src/main.cpp)

target_link_libraries(orderbook_app PRIVATE orderbook_lib)

target_include_directories(orderbook_lib PUBLIC src)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip 
)
FetchContent_MakeAvailable(googletest)
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

enable_testing()

add_subdirectory(tests)
add_subdirectory(benchmarks)
