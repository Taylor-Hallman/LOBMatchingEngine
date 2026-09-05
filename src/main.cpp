#include "naive/OrderBook.h"
#include "naive/Order.h"
#include "util/OrderGenerator.h"

#include <print>
#include <thread>
#include <chrono>

int main() {
    using namespace naive;
    using clock = std::chrono::steady_clock;
    std::chrono::nanoseconds total_work_time{ 0 };

    OrderBook orderBook;
    for (size_t i{}; i < 100uz; ++i) {
        Order bid = GenerateOrder<Order>(Side::Buy);
        Order ask = GenerateOrder<Order>(Side::Sell);

        auto start = clock::now();

        orderBook.placeOrder(bid);
        orderBook.placeOrder(ask);

        auto end = clock::now();
        total_work_time += (end - start);

        //std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::println("Work took {} nanoseconds", total_work_time.count());
}
