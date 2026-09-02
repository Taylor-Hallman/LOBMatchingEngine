#include <array>
#include <gtest/gtest.h>
#include "naive/OrderBook.h"
#include "util/OrderGenerator.h"

TEST(OrderBookTest, EmptyBookQuery) {
    OrderBook book;
    EXPECT_EQ(book.size(), 0uz);
    EXPECT_EQ(book.getBestBuyPrice(), -1);
    EXPECT_EQ(book.getBestSellPrice(), -1);
}

TEST(OrderBookTest, PlaceTwoBids) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(book.getBestBuyPrice(), 10000);
}

TEST(OrderBookTest, PlaceTwoAsks) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(book.getBestSellPrice(), 8000);
}

TEST(OrderBookTest, NearMatchRestingBid) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 9999,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(order.remaining_qty, order.quantity);
    EXPECT_EQ(order2.remaining_qty, order2.quantity);
}

TEST(OrderBookTest, NearMatchRestingAsk) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Buy,
        .price = 9999,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(order.remaining_qty, order.quantity);
    EXPECT_EQ(order2.remaining_qty, order2.quantity);
}

TEST(OrderBookTest, MatchRestingBid) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, MatchRestingAsk) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, ExactMatchRestingBid) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, ExactMatchRestingAsk) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, PartialMatchRestingBid) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 50,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 1uz);

    auto bids{ book.getBidsAtPrice(order.price) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 1uz);
    EXPECT_EQ(bids.front().remaining_qty, 50);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, PartialMatchRestingAsk) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 50,
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 1uz);

    auto asks{ book.getAsksAtPrice(order.price) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 1uz);
    EXPECT_EQ(asks.front().remaining_qty, 50);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, MatchBestBid) {
    OrderBook book;
    Order bid{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order bid2{
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
    };
    Order bid3{
        .side = Side::Buy,
        .price = 11000,
        .quantity = 100,
    };
    Order ask{
        .side = Side::Sell,
        .price = 7000,
        .quantity = 100,
    };
    book.placeOrder(bid);
    book.placeOrder(bid2);
    book.placeOrder(bid3);
    EXPECT_EQ(book.size(), 3uz);
    EXPECT_EQ(book.getBestBuyPrice(), 11000);

    book.placeOrder(ask);
    EXPECT_EQ(book.size(), 2uz);

    auto bidsLow{ book.getBidsAtPrice(8000) }, 
         bidsMid{ book.getBidsAtPrice(10000) },
         bidsHigh{ book.getBidsAtPrice(11000) };

    ASSERT_FALSE(bidsLow.empty() || bidsMid.empty());
    EXPECT_EQ(bidsLow.size(), 1uz);
    EXPECT_EQ(bidsMid.size(), 1uz);
    EXPECT_TRUE(bidsHigh.empty());

    EXPECT_EQ(bidsLow.front().remaining_qty, bidsLow.front().quantity);
    EXPECT_EQ(bidsMid.front().remaining_qty, bidsMid.front().quantity);
    EXPECT_EQ(ask.remaining_qty, 0);
}

TEST(OrderBookTest, MatchBestAsk) {
    OrderBook book;
    Order ask{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    Order ask2{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
    };
    Order ask3{
        .side = Side::Sell,
        .price = 11000,
        .quantity = 100,
    };
    Order bid{
        .side = Side::Buy,
        .price = 12000,
        .quantity = 100,
    };
    book.placeOrder(ask);
    book.placeOrder(ask2);
    book.placeOrder(ask3);
    EXPECT_EQ(book.size(), 3uz);
    EXPECT_EQ(book.getBestSellPrice(), 8000);

    book.placeOrder(bid);
    EXPECT_EQ(book.size(), 2uz);

    auto asksLow{ book.getAsksAtPrice(8000) }, 
         asksMid{ book.getAsksAtPrice(10000) },
         asksHigh{ book.getAsksAtPrice(11000) };

    ASSERT_FALSE(asksMid.empty() || asksHigh.empty());
    EXPECT_TRUE(asksLow.empty());
    EXPECT_EQ(asksMid.size(), 1uz);
    EXPECT_EQ(asksHigh.size(), 1uz);

    EXPECT_EQ(asksMid.front().remaining_qty, asksMid.front().quantity);
    EXPECT_EQ(asksHigh.front().remaining_qty, asksHigh.front().quantity);
    EXPECT_EQ(bid.remaining_qty, 0);
}

TEST(OrderBookTest, MatchMultipleBids) {
    OrderBook book;
    Order bid{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order bid2{
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
    };
    Order bid3{
        .side = Side::Buy,
        .price = 11000,
        .quantity = 100,
    };
    Order ask{
        .side = Side::Sell,
        .price = 7000,
        .quantity = 200,
    };
    book.placeOrder(bid);
    book.placeOrder(bid2);
    book.placeOrder(bid3);
    EXPECT_EQ(book.size(), 3uz);
    EXPECT_EQ(book.getBestBuyPrice(), 11000);

    book.placeOrder(ask);
    EXPECT_EQ(book.size(), 1uz);

    auto bidsLow{ book.getBidsAtPrice(8000) },
         bidsMid{ book.getBidsAtPrice(10000) },
         bidsHigh{ book.getBidsAtPrice(11000) };

    ASSERT_FALSE(bidsLow.empty());
    EXPECT_TRUE(bidsMid.empty() && bidsHigh.empty());
    EXPECT_EQ(bidsLow.size(), 1uz);
    EXPECT_EQ(book.getBestBuyPrice(), 8000);

    EXPECT_EQ(bidsLow.front().remaining_qty, bidsLow.front().quantity);
    EXPECT_EQ(ask.remaining_qty, 0);
}

TEST(OrderBookTest, MatchMultipleAsks) {
    OrderBook book;
    Order ask{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    Order ask2{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
    };
    Order ask3{
        .side = Side::Sell,
        .price = 11000,
        .quantity = 100,
    };
    Order bid{
        .side = Side::Buy,
        .price = 12000,
        .quantity = 200,
    };
    book.placeOrder(ask);
    book.placeOrder(ask2);
    book.placeOrder(ask3);
    EXPECT_EQ(book.size(), 3uz);
    EXPECT_EQ(book.getBestSellPrice(), 8000);

    book.placeOrder(bid);
    EXPECT_EQ(book.size(), 1uz);

    auto asksLow{ book.getAsksAtPrice(8000) },
         asksMid{ book.getAsksAtPrice(10000) },
         asksHigh{ book.getAsksAtPrice(11000) };

    ASSERT_FALSE(asksHigh.empty());
    EXPECT_TRUE(asksLow.empty() && asksMid.empty());
    EXPECT_EQ(asksHigh.size(), 1uz);
    EXPECT_EQ(book.getBestSellPrice(), 11000);

    EXPECT_EQ(asksHigh.front().remaining_qty, asksHigh.front().quantity);
    EXPECT_EQ(bid.remaining_qty, 0);
}

TEST(OrderBookTest, NewerHasHigherSequence) {
    OrderBook book;
    Order bid{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    
    book.placeOrder(bid);
    book.placeOrder(bid);

    auto bids{ book.getBidsAtPrice(10000) }; 
    ASSERT_EQ(bids.size(), 2uz);

    EXPECT_GT(bids[1].sequence, bids[0].sequence);
}

TEST(OrderBookTest, PrioritizeOldest) {
    OrderBook book;
    Order bidOlder{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order bidNewer{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
    };
    Order askOlder{
        .side = Side::Sell,
        .price = 12000,
        .quantity = 100,
    };
    Order askNewer{
        .side = Side::Sell,
        .price = 12000,
        .quantity = 100,
    };
    book.placeOrder(bidOlder);
    book.placeOrder(bidNewer);
    book.placeOrder(askOlder);
    book.placeOrder(askNewer);
    EXPECT_EQ(book.size(), 4uz);
    
    Order incomingAsk{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
    };
    Order incomingBid{
        .side = Side::Buy,
        .price = 12000,
        .quantity = 100,
    };

    book.placeOrder(incomingAsk);
    EXPECT_EQ(book.size(), 3uz);
    EXPECT_EQ(incomingAsk.remaining_qty, 0);
    auto bids{ book.getBidsAtPrice(10000) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 1uz);
    EXPECT_EQ(bids.front().id, bidNewer.id);

    book.placeOrder(incomingBid);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(incomingBid.remaining_qty, 0);
    auto asks{ book.getAsksAtPrice(12000) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 1uz);
    EXPECT_EQ(asks.front().id, askNewer.id);
}

TEST(OrderBookTest, ExhaustAllAsks) {
    OrderBook book;

    Order ask1{
        .side = Side::Sell,
        .price = 12000,
        .quantity = 100
    };
    Order ask2{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100
    };
    Order ask3{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100
    };

    Order bigBid{
        .side = Side::Buy,
        .price = 12000,
        .quantity = 500
    };

    book.placeOrder(ask1);
    book.placeOrder(ask2);
    book.placeOrder(ask3);
    EXPECT_EQ(book.size(), 3uz);

    book.placeOrder(bigBid);
    EXPECT_EQ(book.size(), 1uz);

    auto bids{ book.getBidsAtPrice(12000) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 1uz);
    EXPECT_EQ(bids.front().id, bigBid.id);
    EXPECT_EQ(bids.front().remaining_qty, 200);
}

TEST(OrderBookTest, ExhaustAllBids) {
    OrderBook book;

    Order bid1{
        .side = Side::Buy,
        .price = 12000,
        .quantity = 100
    };
    Order bid2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order bid3{
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100
    };

    Order bigAsk{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 500
    };

    book.placeOrder(bid1);
    book.placeOrder(bid2);
    book.placeOrder(bid3);
    EXPECT_EQ(book.size(), 3uz);

    book.placeOrder(bigAsk);
    EXPECT_EQ(book.size(), 1uz);

    auto asks{ book.getAsksAtPrice(8000) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 1uz);
    EXPECT_EQ(asks.front().id, bigAsk.id);
    EXPECT_EQ(asks.front().remaining_qty, 200);
}

TEST(OrderBookTest, PlaceAndCancel) {
    OrderBook book;

    Order order{ GenerateOrder() };
    auto id{ order.id };
    
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);

    bool cancelled{ book.cancelOrder(id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 0uz);
}

TEST(OrderBookTest, PlaceManyCancelOne) {
    OrderBook book;
    uint64_t id{};
    for (int i{}; i < 10; ++i) {
        Order order{ GenerateOrder(Side::Buy) };
        book.placeOrder(order);
        if (i == 5)
            id = order.id;
    }
    EXPECT_EQ(book.size(), 10uz);

    bool cancelled{ book.cancelOrder(id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 9uz);
}

TEST(OrderBookTest, CancelNonexistent) {
    OrderBook book;
    std::array<uint64_t, 10> ids;
    for (int i{}; i < 10; ++i) {
        Order order{ GenerateOrder(Side::Buy) };
        book.placeOrder(order);
        ids[i] = order.id;
    }
    EXPECT_EQ(book.size(), 10uz);

    auto cancelId{ (*std::max_element(ids.begin(), ids.end())) + 1};
    bool cancelled{ book.cancelOrder(cancelId) };
    EXPECT_FALSE(cancelled);
    EXPECT_EQ(book.size(), 10uz);
}

TEST(OrderBookTest, DoubleCancel) {
    OrderBook book;
    uint64_t id{};
    for (int i{}; i < 10; ++i) {
        Order order{ GenerateOrder(Side::Buy) };
        book.placeOrder(order);
        if (i == 5)
            id = order.id;
    }
    EXPECT_EQ(book.size(), 10uz);

    bool cancelled{ book.cancelOrder(id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 9uz);

    bool cancelledAgain{ book.cancelOrder(id) };
    EXPECT_FALSE(cancelledAgain);
    EXPECT_EQ(book.size(), 9uz);
}

TEST(OrderBookTest, CancelLastBidAtPrice) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    book.placeOrder(order);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);

    auto best{ book.getBestBuyPrice() };
    EXPECT_EQ(best, 10000);

    bool cancelled{ book.cancelOrder(order2.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 1uz);

    auto newBest{ book.getBestBuyPrice() };
    EXPECT_EQ(newBest, 8000);
}

TEST(OrderBookTest, CancelLastAskAtPrice) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100
    };
    Order order2{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100
    };
    book.placeOrder(order);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);

    auto best{ book.getBestSellPrice() };
    EXPECT_EQ(best, 8000);

    bool cancelled{ book.cancelOrder(order.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 1uz);

    auto newBest{ book.getBestSellPrice() };
    EXPECT_EQ(newBest, 10000);
}

TEST(OrderBookTest, CancelOneBidAtPrice) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order order3{
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100
    };
    book.placeOrder(order);
    book.placeOrder(order2);
    book.placeOrder(order3);
    EXPECT_EQ(book.size(), 3uz);

    auto best{ book.getBestBuyPrice() };
    EXPECT_EQ(best, 10000);

    bool cancelled{ book.cancelOrder(order.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 2uz);

    auto newBest{ book.getBestBuyPrice() };
    EXPECT_EQ(newBest, 10000);
}

TEST(OrderBookTest, CancelOneAskAtPrice) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100
    };
    Order order2{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100
    };
    Order order3{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100
    };
    book.placeOrder(order);
    book.placeOrder(order2);
    book.placeOrder(order3);
    EXPECT_EQ(book.size(), 3uz);

    auto best{ book.getBestSellPrice() };
    EXPECT_EQ(best, 8000);

    bool cancelled{ book.cancelOrder(order.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 2uz);

    auto newBest{ book.getBestSellPrice() };
    EXPECT_EQ(newBest, 8000);
}

TEST(OrderBookTest, CancelMiddleBidAtPrice) {
    OrderBook book;
    Order order{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order order2{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order order3{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    book.placeOrder(order);
    book.placeOrder(order2);
    book.placeOrder(order3);
    EXPECT_EQ(book.size(), 3uz);

    bool cancelled{ book.cancelOrder(order2.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 2uz);

    auto bids{ book.getBidsAtPrice(10000) };
    ASSERT_FALSE(bids.empty());
    EXPECT_EQ(bids.size(), 2uz);
    EXPECT_EQ(bids.front().id, order.id);
}

TEST(OrderBookTest, CancelMiddleAskAtPrice) {
    OrderBook book;
    Order order{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100
    };
    Order order2{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100
    };
    Order order3{
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100
    };
    book.placeOrder(order);
    book.placeOrder(order2);
    book.placeOrder(order3);
    EXPECT_EQ(book.size(), 3uz);

    bool cancelled{ book.cancelOrder(order2.id) };
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(book.size(), 2uz);

    auto asks{ book.getAsksAtPrice(10000) };
    ASSERT_FALSE(asks.empty());
    EXPECT_EQ(asks.size(), 2uz);
    EXPECT_EQ(asks.front().id, order.id);
}

TEST(OrderBookTest, AttemptMatchRestingBidAfterCancel) {
    OrderBook book;
    Order bid{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order ask{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100
    };
    book.placeOrder(bid);
    book.cancelOrder(bid.id);
    book.placeOrder(ask);
    EXPECT_EQ(book.size(), 1uz);
    EXPECT_EQ(book.getBestSellPrice(), 8000);
}

TEST(OrderBookTest, AttemptMatchRestingAskAfterCancel) {
    OrderBook book;
    Order bid{
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100
    };
    Order ask{
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100
    };
    book.placeOrder(ask);
    book.cancelOrder(ask.id);
    book.placeOrder(bid);
    EXPECT_EQ(book.size(), 1uz);
    EXPECT_EQ(book.getBestBuyPrice(), 10000);
}
