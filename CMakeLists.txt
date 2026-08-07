cmake_minimum_required(VERSION 3.25)
project(OrderBook)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(orderbook_lib 
    src/OrderBook.cpp
    src/Order.cpp
)
target_include_directories(orderbook_lib PUBLIC src)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip 
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
