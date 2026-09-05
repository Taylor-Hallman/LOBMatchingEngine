#pragma once

#include "Order.h"
#include <deque>
#include <map>

namespace naive {

class OrderBook {
public:
    using OrderType = Order; // naive::Order
private:
    std::map<int64_t, std::deque<Order>, std::greater<int64_t>> m_bids; 
    std::map<int64_t, std::deque<Order>> m_asks; 

    uint64_t m_next_sequence{ UINT64_C(0) };

public:
    void placeOrder(Order& incoming);
    void processMatch(Order& incoming, Order& resting);
    bool cancelOrder(uint64_t id);
    void clear();
    size_t size();
    int64_t getBestBuyPrice();
    int64_t getBestSellPrice();

    std::deque<Order> getBidsAtPrice(int64_t price);
    std::deque<Order> getAsksAtPrice(int64_t price);
};

}
