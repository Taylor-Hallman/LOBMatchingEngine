#include <array>
#include <gtest/gtest.h>
#include "naive/OrderBook.h"
#include "optimized/OrderBook.h"
#include "util/OrderGenerator.h"

template<typename BookType>
class OrderBookTest : public ::testing::Test {
protected:
    using OrderType = typename BookType::OrderType;
    BookType book;
};

using BookTypes = ::testing::Types<naive::OrderBook, optimized::OrderBook>;
TYPED_TEST_SUITE(OrderBookTest, BookTypes);

TYPED_TEST(OrderBookTest, EmptyBookQuery) {
    EXPECT_EQ(this->book.size(), 0uz);
    EXPECT_EQ(this->book.getBestBuyPrice(), -1);
    EXPECT_EQ(this->book.getBestSellPrice(), -1);
}

TYPED_TEST(OrderBookTest, PlaceTwoBids) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 10000, 100 };
    OrderType order2{ Side::Buy, 8000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 2uz);
    EXPECT_EQ(this->book.getBestBuyPrice(), 10000);
}

TYPED_TEST(OrderBookTest, PlaceTwoAsks) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 10000, 100 };
    OrderType order2{ Side::Sell, 8000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 2uz);
    EXPECT_EQ(this->book.getBestSellPrice(), 8000);
}

TYPED_TEST(OrderBookTest, NearMatchRestingBid) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 9999, 100 };
    OrderType order2{ Side::Buy, 10000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 2uz);
    EXPECT_EQ(order.remaining_qty, order.quantity);
    EXPECT_EQ(order2.remaining_qty, order2.quantity);
}

TYPED_TEST(OrderBookTest, NearMatchRestingAsk) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 10000, 100 };
    OrderType order2{ Side::Buy, 9999, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 2uz);
    EXPECT_EQ(order.remaining_qty, order.quantity);
    EXPECT_EQ(order2.remaining_qty, order2.quantity);
}

TYPED_TEST(OrderBookTest, MatchRestingBid) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 10000, 100 };
    OrderType order2{ Side::Sell, 8000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, MatchRestingAsk) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 8000, 100 };
    OrderType order2{ Side::Buy, 10000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, ExactMatchRestingBid) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 10000, 100 };
    OrderType order2{ Side::Sell, 10000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, ExactMatchRestingAsk) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 10000, 100 };
    OrderType order2{ Side::Buy, 10000, 100 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, PartialMatchRestingBid) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 10000, 100 };
    OrderType order2{ Side::Sell, 8000, 50 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 1uz);

    auto bids{ this->book.getBidsAtPrice(order.price) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 1uz);
    EXPECT_EQ(bids.front().remaining_qty, 50);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, PartialMatchRestingAsk) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 8000, 100 };
    OrderType order2{ Side::Buy, 10000, 50 };
    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 1uz);

    auto asks{ this->book.getAsksAtPrice(order.price) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 1uz);
    EXPECT_EQ(asks.front().remaining_qty, 50);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, MatchBestBid) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bid{ Side::Buy, 10000, 100 };
    OrderType bid2{ Side::Buy, 8000, 100 };
    OrderType bid3{ Side::Buy, 11000, 100 };
    OrderType ask{ Side::Sell, 7000, 100 };
    this->book.placeOrder(bid);
    this->book.placeOrder(bid2);
    this->book.placeOrder(bid3);
    EXPECT_EQ(this->book.size(), 3uz);
    EXPECT_EQ(this->book.getBestBuyPrice(), 11000);

    this->book.placeOrder(ask);
    EXPECT_EQ(this->book.size(), 2uz);

    auto bidsLow{ this->book.getBidsAtPrice(8000) },
         bidsMid{ this->book.getBidsAtPrice(10000) },
         bidsHigh{ this->book.getBidsAtPrice(11000) };

    ASSERT_FALSE(bidsLow.empty() || bidsMid.empty());
    EXPECT_EQ(bidsLow.size(), 1uz);
    EXPECT_EQ(bidsMid.size(), 1uz);
    EXPECT_TRUE(bidsHigh.empty());

    EXPECT_EQ(bidsLow.front().remaining_qty, bidsLow.front().quantity);
    EXPECT_EQ(bidsMid.front().remaining_qty, bidsMid.front().quantity);
    EXPECT_EQ(ask.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, MatchBestAsk) {
    using OrderType = typename TestFixture::OrderType;
    OrderType ask{ Side::Sell, 10000, 100 };
    OrderType ask2{ Side::Sell, 8000, 100 };
    OrderType ask3{ Side::Sell, 11000, 100 };
    OrderType bid{ Side::Buy, 12000, 100 };
    this->book.placeOrder(ask);
    this->book.placeOrder(ask2);
    this->book.placeOrder(ask3);
    EXPECT_EQ(this->book.size(), 3uz);
    EXPECT_EQ(this->book.getBestSellPrice(), 8000);

    this->book.placeOrder(bid);
    EXPECT_EQ(this->book.size(), 2uz);

    auto asksLow{ this->book.getAsksAtPrice(8000) },
         asksMid{ this->book.getAsksAtPrice(10000) },
         asksHigh{ this->book.getAsksAtPrice(11000) };

    ASSERT_FALSE(asksMid.empty() || asksHigh.empty());
    EXPECT_TRUE(asksLow.empty());
    EXPECT_EQ(asksMid.size(), 1uz);
    EXPECT_EQ(asksHigh.size(), 1uz);

    EXPECT_EQ(asksMid.front().remaining_qty, asksMid.front().quantity);
    EXPECT_EQ(asksHigh.front().remaining_qty, asksHigh.front().quantity);
    EXPECT_EQ(bid.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, MatchMultipleBids) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bid{ Side::Buy, 10000, 100 };
    OrderType bid2{ Side::Buy, 8000, 100 };
    OrderType bid3{ Side::Buy, 11000, 100 };
    OrderType ask{ Side::Sell, 7000, 200 };
    this->book.placeOrder(bid);
    this->book.placeOrder(bid2);
    this->book.placeOrder(bid3);
    EXPECT_EQ(this->book.size(), 3uz);
    EXPECT_EQ(this->book.getBestBuyPrice(), 11000);

    this->book.placeOrder(ask);
    EXPECT_EQ(this->book.size(), 1uz);

    auto bidsLow{ this->book.getBidsAtPrice(8000) },
         bidsMid{ this->book.getBidsAtPrice(10000) },
         bidsHigh{ this->book.getBidsAtPrice(11000) };

    ASSERT_FALSE(bidsLow.empty());
    EXPECT_TRUE(bidsMid.empty() && bidsHigh.empty());
    EXPECT_EQ(bidsLow.size(), 1uz);
    EXPECT_EQ(this->book.getBestBuyPrice(), 8000);

    EXPECT_EQ(bidsLow.front().remaining_qty, bidsLow.front().quantity);
    EXPECT_EQ(ask.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, MatchMultipleAsks) {
    using OrderType = typename TestFixture::OrderType;
    OrderType ask{ Side::Sell, 10000, 100 };
    OrderType ask2{ Side::Sell, 8000, 100 };
    OrderType ask3{ Side::Sell, 11000, 100 };
    OrderType bid{ Side::Buy, 12000, 200 };
    this->book.placeOrder(ask);
    this->book.placeOrder(ask2);
    this->book.placeOrder(ask3);
    EXPECT_EQ(this->book.size(), 3uz);
    EXPECT_EQ(this->book.getBestSellPrice(), 8000);

    this->book.placeOrder(bid);
    EXPECT_EQ(this->book.size(), 1uz);

    auto asksLow{ this->book.getAsksAtPrice(8000) },
         asksMid{ this->book.getAsksAtPrice(10000) },
         asksHigh{ this->book.getAsksAtPrice(11000) };

    ASSERT_FALSE(asksHigh.empty());
    EXPECT_TRUE(asksLow.empty() && asksMid.empty());
    EXPECT_EQ(asksHigh.size(), 1uz);
    EXPECT_EQ(this->book.getBestSellPrice(), 11000);

    EXPECT_EQ(asksHigh.front().remaining_qty, asksHigh.front().quantity);
    EXPECT_EQ(bid.remaining_qty, 0);
}

TYPED_TEST(OrderBookTest, NewerHasHigherSequence) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bid{ Side::Buy, 10000, 100 };

    this->book.placeOrder(bid);
    this->book.placeOrder(bid);

    auto bids{ this->book.getBidsAtPrice(10000) };
    ASSERT_EQ(bids.size(), 2uz);

    EXPECT_GT(bids[1].sequence, bids[0].sequence);
}

TYPED_TEST(OrderBookTest, PrioritizeOldest) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bidOlder{ Side::Buy, 10000, 100 };
    OrderType bidNewer{ Side::Buy, 10000, 100 };
    OrderType askOlder{ Side::Sell, 12000, 100 };
    OrderType askNewer{ Side::Sell, 12000, 100 };
    this->book.placeOrder(bidOlder);
    this->book.placeOrder(bidNewer);
    this->book.placeOrder(askOlder);
    this->book.placeOrder(askNewer);
    EXPECT_EQ(this->book.size(), 4uz);

    OrderType incomingAsk{ Side::Sell, 10000, 100 };
    OrderType incomingBid{ Side::Buy, 12000, 100 };

    this->book.placeOrder(incomingAsk);
    EXPECT_EQ(this->book.size(), 3uz);
    EXPECT_EQ(incomingAsk.remaining_qty, 0);
    auto bids{ this->book.getBidsAtPrice(10000) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 1uz);
    EXPECT_EQ(bids.front().id, bidNewer.id);

    this->book.placeOrder(incomingBid);
    EXPECT_EQ(this->book.size(), 2uz);
    EXPECT_EQ(incomingBid.remaining_qty, 0);
    auto asks{ this->book.getAsksAtPrice(12000) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 1uz);
    EXPECT_EQ(asks.front().id, askNewer.id);
}

TYPED_TEST(OrderBookTest, ExhaustAllAsks) {
    using OrderType = typename TestFixture::OrderType;
    OrderType ask1{ Side::Sell, 12000, 100 };
    OrderType ask2{ Side::Sell, 10000, 100 };
    OrderType ask3{ Side::Sell, 8000, 100 };
    OrderType bigBid{ Side::Buy, 12000, 500 };

    this->book.placeOrder(ask1);
    this->book.placeOrder(ask2);
    this->book.placeOrder(ask3);
    EXPECT_EQ(this->book.size(), 3uz);

    this->book.placeOrder(bigBid);
    EXPECT_EQ(this->book.size(), 1uz);

    auto bids{ this->book.getBidsAtPrice(12000) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 1uz);
    EXPECT_EQ(bids.front().id, bigBid.id);
    EXPECT_EQ(bids.front().remaining_qty, 200);
}

TYPED_TEST(OrderBookTest, ExhaustAllBids) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bid1{ Side::Buy, 12000, 100 };
    OrderType bid2{ Side::Buy, 10000, 100 };
    OrderType bid3{ Side::Buy, 8000, 100 };
    OrderType bigAsk{ Side::Sell, 8000, 500 };

    this->book.placeOrder(bid1);
    this->book.placeOrder(bid2);
    this->book.placeOrder(bid3);
    EXPECT_EQ(this->book.size(), 3uz);

    this->book.placeOrder(bigAsk);
    EXPECT_EQ(this->book.size(), 1uz);

    auto asks{ this->book.getAsksAtPrice(8000) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 1uz);
    EXPECT_EQ(asks.front().id, bigAsk.id);
    EXPECT_EQ(asks.front().remaining_qty, 200);
}

TYPED_TEST(OrderBookTest, PlaceAndCancel) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ GenerateOrder<OrderType>() };
    auto id{ order.id };

    this->book.placeOrder(order);
    EXPECT_EQ(this->book.size(), 1uz);

    bool cancelled{ this->book.cancelOrder(id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 0uz);
}

TYPED_TEST(OrderBookTest, PlaceManyCancelOne) {
    using OrderType = typename TestFixture::OrderType;
    uint64_t id{};
    for (int i{}; i < 10; ++i) {
        OrderType order{ GenerateOrder<OrderType>(Side::Buy) };
        this->book.placeOrder(order);
        if (i == 5)
            id = order.id;
    }
    EXPECT_EQ(this->book.size(), 10uz);

    bool cancelled{ this->book.cancelOrder(id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 9uz);
}

TYPED_TEST(OrderBookTest, CancelNonexistent) {
    using OrderType = typename TestFixture::OrderType;
    std::array<uint64_t, 10> ids;
    for (int i{}; i < 10; ++i) {
        OrderType order{ GenerateOrder<OrderType>(Side::Buy) };
        this->book.placeOrder(order);
        ids[i] = order.id;
    }
    EXPECT_EQ(this->book.size(), 10uz);

    auto cancelId{ (*std::max_element(ids.begin(), ids.end())) + 1 };
    bool cancelled{ this->book.cancelOrder(cancelId) };
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(this->book.size(), 10uz);
}

TYPED_TEST(OrderBookTest, DoubleCancel) {
    using OrderType = typename TestFixture::OrderType;
    uint64_t id{};
    for (int i{}; i < 10; ++i) {
        OrderType order{ GenerateOrder<OrderType>(Side::Buy) };
        this->book.placeOrder(order);
        if (i == 5)
            id = order.id;
    }
    EXPECT_EQ(this->book.size(), 10uz);

    bool cancelled{ this->book.cancelOrder(id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 9uz);

    bool cancelledAgain{ this->book.cancelOrder(id) };
    EXPECT_FALSE(cancelledAgain);
    EXPECT_EQ(this->book.size(), 9uz);
}

TYPED_TEST(OrderBookTest, CancelLastBidAtPrice) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 8000, 100 };
    OrderType order2{ Side::Buy, 10000, 100 };
    this->book.placeOrder(order);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 2uz);

    auto best{ this->book.getBestBuyPrice() };
    EXPECT_EQ(best, 10000);

    bool cancelled{ this->book.cancelOrder(order2.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 1uz);

    auto newBest{ this->book.getBestBuyPrice() };
    EXPECT_EQ(newBest, 8000);
}

TYPED_TEST(OrderBookTest, CancelLastAskAtPrice) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 8000, 100 };
    OrderType order2{ Side::Sell, 10000, 100 };
    this->book.placeOrder(order);
    this->book.placeOrder(order2);
    EXPECT_EQ(this->book.size(), 2uz);

    auto best{ this->book.getBestSellPrice() };
    EXPECT_EQ(best, 8000);

    bool cancelled{ this->book.cancelOrder(order.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 1uz);

    auto newBest{ this->book.getBestSellPrice() };
    EXPECT_EQ(newBest, 10000);
}

TYPED_TEST(OrderBookTest, CancelOneBidAtPrice) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 10000, 100 };
    OrderType order2{ Side::Buy, 10000, 100 };
    OrderType order3{ Side::Buy, 8000, 100 };
    this->book.placeOrder(order);
    this->book.placeOrder(order2);
    this->book.placeOrder(order3);
    EXPECT_EQ(this->book.size(), 3uz);

    auto best{ this->book.getBestBuyPrice() };
    EXPECT_EQ(best, 10000);

    bool cancelled{ this->book.cancelOrder(order.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 2uz);

    auto newBest{ this->book.getBestBuyPrice() };
    EXPECT_EQ(newBest, 10000);
}

TYPED_TEST(OrderBookTest, CancelOneAskAtPrice) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 8000, 100 };
    OrderType order2{ Side::Sell, 10000, 100 };
    OrderType order3{ Side::Sell, 8000, 100 };
    this->book.placeOrder(order);
    this->book.placeOrder(order2);
    this->book.placeOrder(order3);
    EXPECT_EQ(this->book.size(), 3uz);

    auto best{ this->book.getBestSellPrice() };
    EXPECT_EQ(best, 8000);

    bool cancelled{ this->book.cancelOrder(order.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 2uz);

    auto newBest{ this->book.getBestSellPrice() };
    EXPECT_EQ(newBest, 8000);
}

TYPED_TEST(OrderBookTest, CancelMiddleBidAtPrice) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Buy, 10000, 100 };
    OrderType order2{ Side::Buy, 10000, 100 };
    OrderType order3{ Side::Buy, 10000, 100 };
    this->book.placeOrder(order);
    this->book.placeOrder(order2);
    this->book.placeOrder(order3);
    EXPECT_EQ(this->book.size(), 3uz);

    bool cancelled{ this->book.cancelOrder(order2.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 2uz);

    auto bids{ this->book.getBidsAtPrice(10000) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 2uz);
    EXPECT_EQ(bids.front().id, order.id);
}

TYPED_TEST(OrderBookTest, CancelMiddleAskAtPrice) {
    using OrderType = typename TestFixture::OrderType;
    OrderType order{ Side::Sell, 10000, 100 };
    OrderType order2{ Side::Sell, 10000, 100 };
    OrderType order3{ Side::Sell, 10000, 100 };
    this->book.placeOrder(order);
    this->book.placeOrder(order2);
    this->book.placeOrder(order3);
    EXPECT_EQ(this->book.size(), 3uz);

    bool cancelled{ this->book.cancelOrder(order2.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(this->book.size(), 2uz);

    auto asks{ this->book.getAsksAtPrice(10000) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 2uz);
    EXPECT_EQ(asks.front().id, order.id);
}

TYPED_TEST(OrderBookTest, AttemptMatchRestingBidAfterCancel) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bid{ Side::Buy, 10000, 100 };
    OrderType ask{ Side::Sell, 8000, 100 };
    this->book.placeOrder(bid);
    this->book.cancelOrder(bid.id);
    this->book.placeOrder(ask);
    EXPECT_EQ(this->book.size(), 1uz);
    EXPECT_EQ(this->book.getBestSellPrice(), 8000);
}

TYPED_TEST(OrderBookTest, AttemptMatchRestingAskAfterCancel) {
    using OrderType = typename TestFixture::OrderType;
    OrderType bid{ Side::Buy, 10000, 100 };
    OrderType ask{ Side::Sell, 8000, 100 };
    this->book.placeOrder(ask);
    this->book.cancelOrder(ask.id);
    this->book.placeOrder(bid);
    EXPECT_EQ(this->book.size(), 1uz);
    EXPECT_EQ(this->book.getBestBuyPrice(), 10000);
}
