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
    EXPECT_EQ(order.remaining_qty, 0);
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
    EXPECT_EQ(order.remaining_qty, 50);
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

    EXPECT_EQ(bid.remaining_qty, bid.quantity);
    EXPECT_EQ(bid2.remaining_qty, bid2.quantity);
    EXPECT_EQ(bid3.remaining_qty, 0);
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

    book.placeOrder(ask);
    EXPECT_EQ(book.size(), 2uz);

    EXPECT_EQ(ask.remaining_qty, ask.quantity);
    EXPECT_EQ(ask2.remaining_qty, 0);
    EXPECT_EQ(ask3.remaining_qty, ask3.quantity);
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

    EXPECT_EQ(bid.remaining_qty, 0);
    EXPECT_EQ(bid2.remaining_qty, bid2.quantity);
    EXPECT_EQ(bid3.remaining_qty, 0);
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

    book.placeOrder(ask);
    EXPECT_EQ(book.size(), 1uz);

    EXPECT_EQ(ask.remaining_qty, 0);
    EXPECT_EQ(ask2.remaining_qty, 0);
    EXPECT_EQ(ask3.remaining_qty, ask3.quantity);
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
    EXPECT_EQ(bidOlder.remaining_qty, 0);
    EXPECT_EQ(bidNewer.remaining_qty, bidNewer.quantity);

    book.placeOrder(incomingBid);
    EXPECT_EQ(book.size(), 2uz);
    EXPECT_EQ(incomingBid.remaining_qty, 0);
    EXPECT_EQ(askOlder.remaining_qty, 0);
    EXPECT_EQ(askNewer.remaining_qty, askNewer.quantity);
}
