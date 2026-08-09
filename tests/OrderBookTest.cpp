#include <gtest/gtest.h>
#include "OrderBook.h"

TEST(OrderBookTest, PlaceTwoBids) {
    OrderBook book;
    Order order{
        .id = 0,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order order2{
        .id = 1,
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
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
        .id = 0,
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order order2{
        .id = 1,
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(book.getBestSellPrice(), 8000);
}

TEST(OrderBookTest, NoMatch) {
    OrderBook book;
    Order order{
        .id = 0,
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order order2{
        .id = 1,
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(order.remaining_qty, order.quantity);
    EXPECT_EQ(order2.remaining_qty, order2.quantity);
}

TEST(OrderBookTest, Match) {
    OrderBook book;
    Order order{
        .id = 0,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order order2{
        .id = 1,
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    book.placeOrder(order);
    EXPECT_EQ(book.size(), 1uz);
    book.placeOrder(order2);
    EXPECT_EQ(book.size(), 0uz);
    EXPECT_EQ(order2.remaining_qty, 0);
}

TEST(OrderBookTest, PartialMatch) {
    OrderBook book;
    Order order{
        .id = 0,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order order2{
        .id = 1,
        .side = Side::Sell,
        .price = 8000,
        .quantity = 50,
        .sequence = 0
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

TEST(OrderBookTest, MatchBestBid) {
    OrderBook book;
    Order bid{
        .id = 0,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order bid2{
        .id = 1,
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    Order bid3{
        .id = 2,
        .side = Side::Buy,
        .price = 11000,
        .quantity = 100,
        .sequence = 0
    };
    Order ask{
        .id = 3,
        .side = Side::Sell,
        .price = 7000,
        .quantity = 100,
        .sequence = 0
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
        .id = 0,
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order ask2{
        .id = 1,
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    Order ask3{
        .id = 2,
        .side = Side::Sell,
        .price = 11000,
        .quantity = 100,
        .sequence = 0
    };
    Order bid{
        .id = 3,
        .side = Side::Buy,
        .price = 12000,
        .quantity = 100,
        .sequence = 0
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
        .id = 0,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order bid2{
        .id = 1,
        .side = Side::Buy,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    Order bid3{
        .id = 2,
        .side = Side::Buy,
        .price = 11000,
        .quantity = 100,
        .sequence = 0
    };
    Order ask{
        .id = 3,
        .side = Side::Sell,
        .price = 7000,
        .quantity = 200,
        .sequence = 0
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

    EXPECT_EQ(bidsLow.front().remaining_qty, bidsLow.front().quantity);
    EXPECT_EQ(ask.remaining_qty, 0);
}

TEST(OrderBookTest, MatchMultipleAsks) {
    OrderBook book;
    Order ask{
        .id = 0,
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order ask2{
        .id = 1,
        .side = Side::Sell,
        .price = 8000,
        .quantity = 100,
        .sequence = 0
    };
    Order ask3{
        .id = 2,
        .side = Side::Sell,
        .price = 11000,
        .quantity = 100,
        .sequence = 0
    };
    Order bid{
        .id = 3,
        .side = Side::Buy,
        .price = 12000,
        .quantity = 200,
        .sequence = 0
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

    EXPECT_EQ(asksHigh.front().remaining_qty, asksHigh.front().quantity);
    EXPECT_EQ(bid.remaining_qty, 0);
}

TEST(OrderBookTest, PrioritizeOldest) {
    OrderBook book;
    Order bidOlder{
        .id = 0,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order bidNewer{
        .id = 1,
        .side = Side::Buy,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order askOlder{
        .id = 2,
        .side = Side::Sell,
        .price = 12000,
        .quantity = 100,
        .sequence = 0
    };
    Order askNewer{
        .id = 3,
        .side = Side::Sell,
        .price = 12000,
        .quantity = 100,
        .sequence = 0
    };
    book.placeOrder(bidOlder);
    book.placeOrder(bidNewer);
    book.placeOrder(askOlder);
    book.placeOrder(askNewer);
    EXPECT_EQ(book.size(), 4uz);
    
    Order incomingAsk{
        .id = 4,
        .side = Side::Sell,
        .price = 10000,
        .quantity = 100,
        .sequence = 0
    };
    Order incomingBid{
        .id = 4,
        .side = Side::Buy,
        .price = 12000,
        .quantity = 100,
        .sequence = 0
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
