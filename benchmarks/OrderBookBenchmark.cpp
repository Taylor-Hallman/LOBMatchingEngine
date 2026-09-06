#include <benchmark/benchmark.h>
#include <random>
#include "naive/OrderBook.h"
#include "optimized/OrderBook.h"
#include "util/OrderGenerator.h"

static const std::vector<int64_t> kDepths = {100, 1'000, 10'000, 100'000, 1'000'000};
static const std::vector<int64_t> kMatchPcts = {0, 30, 70, 100};

// Price range presets
constexpr int64_t kNarrowMin{ 5000 }, kNarrowMax{ 15000 };
constexpr int64_t kWideMin{ 0 }, kWideMax{ optimized::MAX_PRICE - 1 };

// Fixed quantity used when FixedQty == true, guarantees clean full fills
constexpr int64_t kFixedQty{ 100 };

template <typename BookType, bool WideRange>
static void BM_PlaceOrder_NoMatch_Bids(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t min{ WideRange ? kWideMin : kNarrowMin };
    constexpr int64_t max{ WideRange ? kWideMax : kNarrowMax };
    state.SetLabel(WideRange ? "wide_range" : "narrow_range");

    BookType book;
    auto depth{ state.range(0) };
    std::vector<OrderType> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Buy, min, max) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Buy, min, max));
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
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Bids, naive::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Bids, naive::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Bids, optimized::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Bids, optimized::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});

template <typename BookType, bool WideRange>
static void BM_PlaceOrder_NoMatch_Asks(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t min{ WideRange ? kWideMin : kNarrowMin };
    constexpr int64_t max{ WideRange ? kWideMax : kNarrowMax };
    state.SetLabel(WideRange ? "wide_range" : "narrow_range");

    BookType book;
    auto depth{ state.range(0) };
    std::vector<OrderType> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Sell, min, max) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Sell, min, max));
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
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Asks, naive::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Asks, naive::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Asks, optimized::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_NoMatch_Asks, optimized::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});

// AlwaysMatch: no WideRange variant (resting side is pinned to a fixed band
// by design, so price range doesn't change what's being measured here).
template <typename BookType, bool FixedQty>
static void BM_PlaceOrder_AlwaysMatch_BidsResting(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t restMin{ 10000 }, restMax{ 15000 };
    constexpr int64_t incMin{ 5000 }, incMax{ 10000 };
    constexpr int64_t qtyMin{ FixedQty ? kFixedQty : 1 };
    constexpr int64_t qtyMax{ FixedQty ? kFixedQty : 100 };
    state.SetLabel(FixedQty ? "fixed_qty" : "varied_qty");

    BookType book;
    auto depth{ state.range(0) };
    std::vector<OrderType> asks;
    asks.reserve(depth);
    auto reset = [&] {
        book.clear();
        asks.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Buy, restMin, restMax, qtyMin, qtyMax) };
            book.placeOrder(order);
            asks.push_back(GenerateOrder<OrderType>(Side::Sell, incMin, incMax, qtyMin, qtyMax));
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
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_BidsResting, naive::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_BidsResting, naive::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_BidsResting, optimized::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_BidsResting, optimized::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});

template <typename BookType, bool FixedQty>
static void BM_PlaceOrder_AlwaysMatch_AsksResting(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t restMin{ 5000 }, restMax{ 10000 };
    constexpr int64_t incMin{ 10000 }, incMax{ 15000 };
    constexpr int64_t qtyMin{ FixedQty ? kFixedQty : 1 };
    constexpr int64_t qtyMax{ FixedQty ? kFixedQty : 100 };
    state.SetLabel(FixedQty ? "fixed_qty" : "varied_qty");

    BookType book;
    auto depth{ state.range(0) };
    std::vector<OrderType> bids;
    bids.reserve(depth);
    auto reset = [&] {
        book.clear();
        bids.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Sell, restMin, restMax, qtyMin, qtyMax) };
            book.placeOrder(order);
            bids.push_back(GenerateOrder<OrderType>(Side::Buy, incMin, incMax, qtyMin, qtyMax));
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
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_AsksResting, naive::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_AsksResting, naive::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_AsksResting, optimized::OrderBook, false)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_AlwaysMatch_AsksResting, optimized::OrderBook, true)
    ->Args({100})->Args({1'000})->Args({10'000})->Args({100'000})->Args({1'000'000})
    ->ArgNames({"depth"});

template <typename BookType, bool WideRange, bool FixedQty>
static void BM_PlaceOrder_MixedTraffic_BidsResting(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t min{ WideRange ? kWideMin : 1000 };
    constexpr int64_t max{ WideRange ? kWideMax : 100000 };
    constexpr int64_t qtyMin{ FixedQty ? kFixedQty : 1 };
    constexpr int64_t qtyMax{ FixedQty ? kFixedQty : 100 };
    state.SetLabel(std::string(WideRange ? "wide_range" : "narrow_range") + " " +
                   (FixedQty ? "fixed_qty" : "varied_qty"));

    auto depth{ state.range(0) };

    BookType book;
    double match_probability{ state.range(1) / 100.0 };
    int64_t crossingPrice{ min + static_cast<int64_t>(match_probability * (max - min)) };

    std::vector<OrderType> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Buy, crossingPrice, crossingPrice, qtyMin, qtyMax) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Sell, min, max, qtyMin, qtyMax));
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
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, naive::OrderBook, false, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, naive::OrderBook, false, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, naive::OrderBook, true, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, naive::OrderBook, true, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, optimized::OrderBook, false, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, optimized::OrderBook, false, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, optimized::OrderBook, true, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_BidsResting, optimized::OrderBook, true, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});

template <typename BookType, bool WideRange, bool FixedQty>
static void BM_PlaceOrder_MixedTraffic_AsksResting(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t min{ WideRange ? kWideMin : 1000 };
    constexpr int64_t max{ WideRange ? kWideMax : 100000 };
    constexpr int64_t qtyMin{ FixedQty ? kFixedQty : 1 };
    constexpr int64_t qtyMax{ FixedQty ? kFixedQty : 100 };
    state.SetLabel(std::string(WideRange ? "wide_range" : "narrow_range") + " " +
                   (FixedQty ? "fixed_qty" : "varied_qty"));

    auto depth{ state.range(0) };

    BookType book;
    double match_probability{ state.range(1) / 100.0 };
    int64_t crossingPrice{ max - static_cast<int64_t>(match_probability * (max - min)) };

    std::vector<OrderType> orders;
    orders.reserve(depth);
    auto reset = [&] {
        book.clear();
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Sell, crossingPrice, crossingPrice, qtyMin, qtyMax) };
            book.placeOrder(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Buy, min, max, qtyMin, qtyMax));
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
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, naive::OrderBook, false, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, naive::OrderBook, false, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, naive::OrderBook, true, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, naive::OrderBook, true, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, optimized::OrderBook, false, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, optimized::OrderBook, false, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, optimized::OrderBook, true, false)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});
BENCHMARK_TEMPLATE(BM_PlaceOrder_MixedTraffic_AsksResting, optimized::OrderBook, true, true)
    ->ArgsProduct({kDepths, kMatchPcts})->ArgNames({"depth", "match_pct"});

// Cancel benchmarks: no price/quantity variants, just named depth arg.
template <typename BookType>
static void BM_CancelBids(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    BookType book;

    auto depth{ state.range(0) };

    std::random_device rd;
    std::mt19937 g(rd());

    std::deque<uint64_t> ids;
    for (auto i{0uz}; i < depth * 2; ++i) {
        OrderType bid{ GenerateOrder<OrderType>(Side::Buy) };
        book.placeOrder(bid);
        ids.push_back(bid.id);
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
                OrderType bid{ GenerateOrder<OrderType>(Side::Buy) };
                book.placeOrder(bid);
                ids.push_back(bid.id);
            }
            std::shuffle(ids.begin(), ids.end(), g);
            state.ResumeTiming();
        }
    }
}
BENCHMARK_TEMPLATE(BM_CancelBids, naive::OrderBook)
    ->RangeMultiplier(10)->Range(100, 1'000'000)->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_CancelBids, optimized::OrderBook)
    ->RangeMultiplier(10)->Range(100, 1'000'000)->ArgNames({"depth"});

template <typename BookType>
static void BM_CancelAsks(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    BookType book;

    auto depth{ state.range(0) };

    std::random_device rd;
    std::mt19937 g(rd());

    std::deque<uint64_t> ids;
    for (auto i{0uz}; i < depth * 2; ++i) {
        OrderType ask{ GenerateOrder<OrderType>(Side::Sell) };
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
                OrderType ask{ GenerateOrder<OrderType>(Side::Sell) };
                book.placeOrder(ask);
                ids.push_back(ask.id);
            }
            std::shuffle(ids.begin(), ids.end(), g);
            state.ResumeTiming();
        }
    }
}
BENCHMARK_TEMPLATE(BM_CancelAsks, naive::OrderBook)
    ->RangeMultiplier(10)->Range(100, 1'000'000)->ArgNames({"depth"});
BENCHMARK_TEMPLATE(BM_CancelAsks, optimized::OrderBook)
    ->RangeMultiplier(10)->Range(100, 1'000'000)->ArgNames({"depth"});

BENCHMARK_MAIN();
