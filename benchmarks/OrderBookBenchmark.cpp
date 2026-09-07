#include <benchmark/benchmark.h>
#include <random>
#include "naive/OrderBook.h"
#include "optimized/OrderBook.h"
#include "util/OrderGenerator.h"

static const std::vector<int64_t> kDepths = {100, 1'000, 10'000, 100'000, 1'000'000};
static const std::vector<int64_t> kMatchPcts = {0, 30, 70, 100};

constexpr int64_t kNarrowMin{ 5000 }, kNarrowMax{ 15000 };
constexpr int64_t kWideMin{ 0 }, kWideMax{ optimized::MAX_PRICE - 1 };
constexpr int64_t kFixedQty{ 100 };

template <typename BookType, bool WideRange>
static void BM_PlaceOrder_NoMatch_Bids(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t min{ WideRange ? kWideMin : kNarrowMin };
    constexpr int64_t max{ WideRange ? kWideMax : kNarrowMax };
    state.SetLabel(WideRange ? "wide_range" : "narrow_range");

    BookType book;
    constexpr bool isOpt{ std::is_same_v<BookType, optimized::OrderBook> };
    auto depth{ state.range(0) };
    std::vector<OrderType> orders;
    std::vector<size_t> indexes;
    orders.reserve(depth);
    auto place = [&](OrderType& order) {
        if constexpr (isOpt) {
            auto idx{ book.placeOrder(order) };
            if (idx != optimized::INVALID_IDX)
                indexes.push_back(idx);
        } else {
            book.placeOrder(order);
        }
    };
    auto reset = [&] {
        if constexpr (isOpt) {
            for (size_t idx : indexes)
                book.releaseOrder(idx);
            indexes.clear();
            book.clear(false);
        } else {
            book.clear();
        }
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Buy, min, max) };
            place(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Buy, min, max));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        place(orders[i++]);
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
    constexpr bool isOpt{ std::is_same_v<BookType, optimized::OrderBook> };
    auto depth{ state.range(0) };
    std::vector<OrderType> orders;
    std::vector<size_t> indexes;
    orders.reserve(depth);
    auto place = [&](OrderType& order) {
        if constexpr (isOpt) {
            auto idx{ book.placeOrder(order) };
            if (idx != optimized::INVALID_IDX)
                indexes.push_back(idx);
        } else {
            book.placeOrder(order);
        }
    };
    auto reset = [&] {
        if constexpr (isOpt) {
            for (size_t idx : indexes)
                book.releaseOrder(idx);
            indexes.clear();
            book.clear(false);
        } else {
            book.clear();
        }
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Sell, min, max) };
            place(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Sell, min, max));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        place(orders[i++]);
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

template <typename BookType, bool FixedQty>
static void BM_PlaceOrder_AlwaysMatch_BidsResting(benchmark::State& state) {
    using OrderType = typename BookType::OrderType;
    constexpr int64_t restMin{ 10000 }, restMax{ 15000 };
    constexpr int64_t incMin{ 5000 }, incMax{ 10000 };
    constexpr int64_t qtyMin{ FixedQty ? kFixedQty : 1 };
    constexpr int64_t qtyMax{ FixedQty ? kFixedQty : 100 };
    state.SetLabel(FixedQty ? "fixed_qty" : "varied_qty");

    BookType book;
    constexpr bool isOpt{ std::is_same_v<BookType, optimized::OrderBook> };
    auto depth{ state.range(0) };
    std::vector<OrderType> asks;
    std::vector<size_t> indexes;
    asks.reserve(depth);
    auto place = [&](OrderType& order) {
        if constexpr (isOpt) {
            auto idx{ book.placeOrder(order) };
            if (idx != optimized::INVALID_IDX)
                indexes.push_back(idx);
        } else {
            book.placeOrder(order);
        }
    };
    auto reset = [&] {
        if constexpr (isOpt) {
            for (size_t idx : indexes)
                book.releaseOrder(idx);
            indexes.clear();
            book.clear(false);
        } else {
            book.clear();
        }
        asks.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Buy, restMin, restMax, qtyMin, qtyMax) };
            place(order);
            asks.push_back(GenerateOrder<OrderType>(Side::Sell, incMin, incMax, qtyMin, qtyMax));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        place(asks[i++]);
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
    constexpr bool isOpt{ std::is_same_v<BookType, optimized::OrderBook> };
    auto depth{ state.range(0) };
    std::vector<OrderType> bids;
    std::vector<size_t> indexes;
    bids.reserve(depth);
    auto place = [&](OrderType& order) {
        if constexpr (isOpt) {
            auto idx{ book.placeOrder(order) };
            if (idx != optimized::INVALID_IDX)
                indexes.push_back(idx);
        } else {
            book.placeOrder(order);
        }
    };
    auto reset = [&] {
        if constexpr (isOpt) {
            for (size_t idx : indexes)
                book.releaseOrder(idx);
            indexes.clear();
            book.clear(false);
        } else {
            book.clear();
        }
        bids.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Sell, restMin, restMax, qtyMin, qtyMax) };
            place(order);
            bids.push_back(GenerateOrder<OrderType>(Side::Buy, incMin, incMax, qtyMin, qtyMax));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        place(bids[i++]);
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
    constexpr bool isOpt{ std::is_same_v<BookType, optimized::OrderBook> };
    double match_probability{ state.range(1) / 100.0 };
    int64_t crossingPrice{ min + static_cast<int64_t>(match_probability * (max - min)) };

    std::vector<OrderType> orders;
    std::vector<size_t> indexes;
    orders.reserve(depth);
    auto place = [&](OrderType& order) {
        if constexpr (isOpt) {
            auto idx{ book.placeOrder(order) };
            if (idx != optimized::INVALID_IDX)
                indexes.push_back(idx);
        } else {
            book.placeOrder(order);
        }
    };
    auto reset = [&] {
        if constexpr (isOpt) {
            for (size_t idx : indexes)
                book.releaseOrder(idx);
            indexes.clear();
            book.clear(false);
        } else {
            book.clear();
        }
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Buy, crossingPrice, crossingPrice, qtyMin, qtyMax) };
            place(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Sell, min, max, qtyMin, qtyMax));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        place(orders[i++]);
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
    constexpr bool isOpt{ std::is_same_v<BookType, optimized::OrderBook> };
    double match_probability{ state.range(1) / 100.0 };
    int64_t crossingPrice{ max - static_cast<int64_t>(match_probability * (max - min)) };

    std::vector<OrderType> orders;
    std::vector<size_t> indexes;
    orders.reserve(depth);
    auto place = [&](OrderType& order) {
        if constexpr (isOpt) {
            auto idx{ book.placeOrder(order) };
            if (idx != optimized::INVALID_IDX)
                indexes.push_back(idx);
        } else {
            book.placeOrder(order);
        }
    };
    auto reset = [&] {
        if constexpr (isOpt) {
            for (size_t idx : indexes)
                book.releaseOrder(idx);
            indexes.clear();
            book.clear(false);
        } else {
            book.clear();
        }
        orders.clear();
        for (auto i{0uz}; i < depth; ++i) {
            OrderType order{ GenerateOrder<OrderType>(Side::Sell, crossingPrice, crossingPrice, qtyMin, qtyMax) };
            place(order);
            orders.push_back(GenerateOrder<OrderType>(Side::Buy, min, max, qtyMin, qtyMax));
        }
    };

    reset();
    auto i{0uz};
    for (auto _ : state) {
        place(orders[i++]);
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

// Cancel benchmarks unchanged — they never call clear(), they already
// maintain bounded book size via shuffle-and-replenish.
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
