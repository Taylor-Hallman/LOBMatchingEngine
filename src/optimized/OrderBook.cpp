#include "optimized/OrderBook.h"
#include <algorithm>
#include <bit>
#include <cassert>

OrderBook::OrderBook() : 
    m_orderPool(POOL_SIZE), 
    m_bids(MAX_PRICE, INVALID_IDX), 
    m_asks(MAX_PRICE, INVALID_IDX), 
    m_bidOccupancy(OCCUPANCY_SIZE, 0), 
    m_askOccupancy(OCCUPANCY_SIZE, 0) 
{}

bool OrderBook::releaseOrder(size_t orderIdx) {
    Order order{ *m_orderPool.get(orderIdx) };
    if (!m_orderPool.release(orderIdx))
        return false;

    auto& side = order.m_side == Side::Buy ? m_bids : m_asks;
    auto& occupancy = order.m_side == Side::Buy ? m_bidOccupancy : m_askOccupancy;
    size_t& bestIdx = order.m_side == Side::Buy ? m_bestBid : m_bestAsk;

    m_orderLocations.erase(order.m_id);

    if (order.m_next == INVALID_IDX) {
        // Reset this price point to invalid in the vector
        side[order.m_price] = INVALID_IDX;

        // zero out bit flag
        size_t occupIdx{ order.m_price / WORD_SIZE };
        auto& word{ occupancy[occupIdx] };
        auto offset{ order.m_price & WORD_SIZE };
        word &= ~(UINT64_C(0x01) << offset);

        // if this was the best order for this side, search for a new best
        if (orderIdx == bestIdx) {
            auto check = order.m_side == Side::Buy
                ? [](size_t& i) { return i-- > 0; }
                : [](size_t& i) { return ++i < OCCUPANCY_SIZE; };

            for (size_t i{occupIdx}; check(i);) {
                if (auto zeros = std::countl_zero(occupancy[i]) < WORD_SIZE) {
                    auto newPrice{ i * WORD_SIZE + (zeros + 1) };
                    bestIdx = side[newPrice];
                    assert(bestIdx != INVALID_IDX);
                    return true;
                }
            }

            bestIdx = INVALID_IDX;
        }
    }
    // Not the last order at this price point, so just update the head
    // to point to the next order for this price point
    else {
        side[order.m_price] = order.m_next;
        if (orderIdx == bestIdx)
            bestIdx = order.m_next;
    }
    return true;
}

void OrderBook::placeOrder(Order& incoming) {
    auto& incomingSide = incoming.m_side == Side::Buy ? m_bids : m_asks;
    auto& restingSide = incoming.m_side == Side::Buy ? m_asks : m_bids;
    auto& incOccupancy = incoming.m_side == Side::Buy ? m_bidOccupancy : m_askOccupancy;
    auto& restOccupancy = incoming.m_side == Side::Buy ? m_askOccupancy : m_bidOccupancy;

    auto crosses = incoming.m_side == Side::Buy
        ? [](int64_t incomingPrice, int64_t restingPrice) { return incomingPrice >= restingPrice; }
        : [](int64_t incomingPrice, int64_t restingPrice) { return incomingPrice <= restingPrice; };

    size_t& bestIdx = incoming.m_side == Side::Buy ? m_bestAsk : m_bestBid;

    Order* resting{ m_orderPool.get(bestIdx) };
    if (resting && crosses(incoming.m_price, resting->m_price)) {
        // Process match
        auto amtTraded{ std::min(incoming.m_remaining_qty, resting->m_remaining_qty) };
        incoming.m_remaining_qty -= amtTraded;
        resting->m_remaining_qty -= amtTraded;

        auto priceTraded{resting->m_price * amtTraded};

        if (!resting->m_remaining_qty)
            releaseOrder(bestIdx);
        if (!incoming.m_remaining_qty)
            return; // if this order was immediately exhausted, return early & don't allocate it in the pool
    }

    // Allocate order in pool
    size_t idx{ m_orderPool.allocate(incoming) };
    m_orderLocations[incoming.m_id] = idx;

    // If we already have an order with this price, append to the end of the list for that price
    if (incomingSide[incoming.m_price] != INVALID_IDX) {
        Order* other{m_orderPool.get(incomingSide[incoming.m_price])};
        assert(other != nullptr);
        while (other->m_next != INVALID_IDX)
            other = m_orderPool.get(other->m_next);
        other->m_next = idx;
        incoming.m_prev = m_orderPool.get(other->m_prev)->m_next;
    }
    else {
        // Insert order at this price point
        incomingSide[incoming.m_price] = idx;

        // Update best bid/ask if incoming order is better
        Order best{ *m_orderPool.get(bestIdx) };
        if (incoming.m_side == Side::Buy && incoming.m_price > best.m_price)
            m_bestBid = idx;
        else if (incoming.m_side == Side::Sell && incoming.m_price < best.m_price)
            m_bestAsk = idx;
        
        // Mark price point as occupied in bitmap
        auto& word{ incOccupancy[incoming.m_price / WORD_SIZE] };
        auto offset{ incoming.m_price % WORD_SIZE };
        word |= (UINT64_C(0x01) << offset);
    }
}

bool OrderBook::cancelOrder(uint64_t id) {
    return releaseOrder(m_orderLocations[id]);
}
