#include "OrderBook.h"
#include "Order.h"
#include <print>

void OrderBook::placeOrder(Order& incoming) {
    switch (incoming.side) {
    case Side::Buy:
        while (m_asks.size() && m_asks.begin()->first <= incoming.price) {
            Order& match = m_asks.begin()->second.front();
            processMatch(incoming, match);
            if (!match.remaining_qty) {
                m_asks.begin()->second.pop_front();
                if (!m_asks.begin()->second.size())
                    m_asks.erase(m_asks.begin());
            }
            if (!incoming.remaining_qty)
                return; // order was fulfilled immediately so will not be placed in the queue
        }
        m_bids[incoming.price].push_back(incoming);
        break;
    case Side::Sell:
        while (m_bids.size() && m_bids.begin()->first >= incoming.price) {
            Order& match = m_bids.begin()->second.front();
            processMatch(incoming, match);
            if (!match.remaining_qty) {
                m_bids.begin()->second.pop_front();
                if (!m_bids.begin()->second.size())
                    m_bids.erase(m_bids.begin());
            }
            if (!incoming.remaining_qty)
                return;
        }
        m_asks[incoming.price].push_back(incoming);
    }
}

void OrderBook::processMatch(Order& incoming, Order& resting) {
    auto amt{ std::min(incoming.remaining_qty, resting.remaining_qty) };
    incoming.remaining_qty = std::max(INT64_C(0), incoming.remaining_qty - resting.remaining_qty);
    resting.remaining_qty = std::max(INT64_C(0), resting.remaining_qty - incoming.remaining_qty);
    std::println("Matched order {0} with order {1}. Traded {2} options for ${3:.2f}", 
            incoming.id,
            resting.id,
            amt,
            amt * resting.price * 0.01f
        );
    incoming.LogRemainingQty();
    resting.LogRemainingQty();
}

size_t OrderBook::size() {
    return m_bids.size() + m_asks.size();
}

int64_t OrderBook::getBestBuyPrice() {
    if (m_bids.size())
        return m_bids.begin()->first;
    return -1;
}

int64_t OrderBook::getBestSellPrice() {
    if (m_asks.size())
        return m_asks.begin()->first;
    return -1;
}
