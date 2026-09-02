#include <benchmark/benchmark.h>
#include <random>
#include "naive/OrderBook.h"
#include "util/OrderGenerator.h"

static void BM_PlaceOrder_NoMatch_Bids(benchmark::State& state) {
    OrderBook book;
    auto depth{ state.range(0) };
    std::vector<Order> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            Order order{ GenerateOrder(Side::Buy) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder(Side::Buy));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        book.placeOrder(orders[i++]);
        if (i >= depth) {
            state.PauseTiming();
            reset();
            i = 0;
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_PlaceOrder_NoMatch_Bids)->RangeMultiplier(10)->Range(100, 1'000'000);

static void BM_PlaceOrder_NoMatch_Asks(benchmark::State& state) {
    OrderBook book;
    auto depth{ state.range(0) };
    std::vector<Order> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            Order order{ GenerateOrder(Side::Sell) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder(Side::Sell));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        book.placeOrder(orders[i++]);
        if (i >= depth) {
            state.PauseTiming();
            reset();
            i = 0;
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_PlaceOrder_NoMatch_Asks)->RangeMultiplier(10)->Range(100, 1'000'000);

static void BM_PlaceOrder_AlwaysMatch_BidsResting(benchmark::State& state) {
    OrderBook book;
    auto depth{ state.range(0) };
    std::vector<Order> asks;
    asks.reserve(depth);
    auto reset = [&] {
        book.clear();
        asks.clear();
        for (auto i{0uz}; i < depth; ++i) {
            Order order{ GenerateOrder(Side::Buy, 10000, 15000) };
            book.placeOrder(order);
            asks.push_back(GenerateOrder(Side::Sell, 5000, 10000));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        book.placeOrder(asks[i++]);
        if (i >= depth) {
            state.PauseTiming();
            reset();
            i = 0;
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_PlaceOrder_AlwaysMatch_BidsResting)->RangeMultiplier(10)->Range(100, 1'000'000);

static void BM_PlaceOrder_AlwaysMatch_AsksResting(benchmark::State& state) {
    OrderBook book;
    auto depth{ state.range(0) };
    std::vector<Order> bids;
    bids.reserve(depth);
    auto reset = [&] {
        book.clear();
        bids.clear();
        for (auto i{0uz}; i < depth; ++i) {
            Order order{ GenerateOrder(Side::Sell, 5000, 10000) };
            book.placeOrder(order);
            bids.push_back(GenerateOrder(Side::Buy, 10000, 15000));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        book.placeOrder(bids[i++]);
        if (i >= depth) {
            state.PauseTiming();
            reset();
            i = 0;
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_PlaceOrder_AlwaysMatch_AsksResting)->RangeMultiplier(10)->Range(100, 1'000'000);

static void BM_PlaceOrder_MixedTraffic_BidsResting(benchmark::State& state) {
    constexpr int min{ 1000 };
    constexpr int max{ 100000 };

    auto depth{ state.range(0) };

    OrderBook book;
    double match_probability{ state.range(1) / 100.0 };
    int64_t crossingPrice{ min + static_cast<int64_t>(match_probability * (max - min)) };

    std::vector<Order> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            Order order{ GenerateOrder(Side::Buy, crossingPrice, crossingPrice) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder(Side::Sell, min, max));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        book.placeOrder(orders[i++]);
        if (i >= depth) {
            state.PauseTiming();
            reset();
            i = 0;
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_PlaceOrder_MixedTraffic_BidsResting)
    ->ArgsProduct({
        {100, 1'000, 10'000, 100'000, 1'000'000},
        {0, 30, 70, 100}
    });

static void BM_PlaceOrder_MixedTraffic_AsksResting(benchmark::State& state) {
    constexpr int min{ 1000 };
    constexpr int max{ 100000 };

    auto depth{ state.range(0) };

    OrderBook book;
    double match_probability{ state.range(1) / 100.0 };
    int64_t crossingPrice{ max - static_cast<int64_t>(match_probability * (max - min)) };

    std::vector<Order> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            Order order{ GenerateOrder(Side::Sell, crossingPrice, crossingPrice) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder(Side::Buy, min, max));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        book.placeOrder(orders[i++]);
        if (i >= depth) {
            state.PauseTiming();
            reset();
            i = 0;
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_PlaceOrder_MixedTraffic_AsksResting)
    ->ArgsProduct({
        {100, 1'000, 10'000, 100'000, 1'000'000},
        {0, 30, 70, 100}
    });

static void BM_CancelBids(benchmark::State& state) {
    OrderBook book;

    auto depth{ state.range(0) };

    std::random_device rd;
    std::mt19937 g(rd());
    
    std::deque<uint64_t> ids;
    for (auto i{0uz}; i < depth * 2; ++i) {
        Order bid{ GenerateOrder(Side::Buy) };
        book.placeOrder(bid);
        ids.push_back(bid.id);
    }

    // Shuffle the ids so we're not always cancelling the oldest
    std::shuffle(ids.begin(), ids.end(), g);

    auto i{0uz};
    for (auto _ : state) {
        auto idToCancel{ ids.front() };
        ids.pop_front();
        book.cancelOrder(idToCancel);
        if (++i % depth == 0) {
            state.PauseTiming();
            for (auto i{0uz}; i < depth; ++i) {
                Order bid{ GenerateOrder(Side::Buy) };
                book.placeOrder(bid);
                ids.push_back(bid.id);
            }
            std::shuffle(ids.begin(), ids.end(), g);
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_CancelBids)->RangeMultiplier(10)->Range(100, 1'000'000);

static void BM_CancelAsks(benchmark::State& state) {
    OrderBook book;

    auto depth{ state.range(0) };

    std::random_device rd;
    std::mt19937 g(rd());
    
    std::deque<uint64_t> ids;
    for (auto i{0uz}; i < depth * 2; ++i) {
        Order ask{ GenerateOrder(Side::Sell) };
        book.placeOrder(ask);
        ids.push_back(ask.id);
    }

    std::shuffle(ids.begin(), ids.end(), g);

    auto i{0uz};
    for (auto _ : state) {
        auto idToCancel{ ids.front() };
        ids.pop_front();
        book.cancelOrder(idToCancel);
        if (++i % depth == 0) {
            state.PauseTiming();
            for (auto i{0uz}; i < depth; ++i) {
                Order ask{ GenerateOrder(Side::Sell) };
                book.placeOrder(ask);
                ids.push_back(ask.id);
            }
            std::shuffle(ids.begin(), ids.end(), g);
            state.ResumeTiming();
        }
    }
}
BENCHMARK(BM_CancelAsks)->RangeMultiplier(10)->Range(100, 1'000'000);
