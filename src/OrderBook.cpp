#include "OrderBook.h"
#include "Order.h"
#include <print>

std::atomic<uint64_t> Order::s_next_id{ 0 };

void OrderBook::placeOrder(Order& incoming) {
    incoming.sequence = m_next_sequence++;
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
        m_bids[incoming.price].push_back(std::move(incoming));
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
        m_asks[incoming.price].push_back(std::move(incoming));
    }
}

void OrderBook::processMatch(Order& incoming, Order& resting) {
    auto amt{ std::min(incoming.remaining_qty, resting.remaining_qty) };
    auto temp{ incoming.remaining_qty };
    incoming.remaining_qty = std::max(INT64_C(0), incoming.remaining_qty - resting.remaining_qty);
    resting.remaining_qty = std::max(INT64_C(0), resting.remaining_qty - temp);
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
    auto bidsSize{ 0uz }, asksSize{ 0uz };
    for (auto& [price, bidsDeque] : m_bids)
        bidsSize += bidsDeque.size();
    for (auto& [price, asksDeque] : m_asks)
        asksSize += asksDeque.size();
    return bidsSize + asksSize;
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

std::deque<Order> OrderBook::getBidsAtPrice(int64_t price) {
    if (!m_bids.contains(price))
        return {};
    return m_bids.at(price);
}

std::deque<Order> OrderBook::getAsksAtPrice(int64_t price) {
    if (!m_asks.contains(price))
        return {};
    return m_asks.at(price);
}
