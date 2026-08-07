#pragma once

#include "Order.h"
#include <deque>
#include <iostream>
#include <map>

class OrderBook {
private:
    // Naive approach, to be improved later
    std::map<int64_t, std::deque<Order>, std::greater<int64_t>> m_bids; 
    std::map<int64_t, std::deque<Order>> m_asks; 

public:
    void placeOrder(Order& incoming);
    void processMatch(Order& incoming, Order& resting);
    size_t size();
};
