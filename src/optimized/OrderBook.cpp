#include "optimized/OrderBook.h"
#include <algorithm>
#include <bit>
#include <cassert>

namespace optimized {

OrderBook::OrderBook() : 
    m_orderPool(POOL_SIZE), 
    m_bids(MAX_PRICE, OrderList{ INVALID_IDX, INVALID_IDX }), 
    m_asks(MAX_PRICE, OrderList{ INVALID_IDX, INVALID_IDX }), 
    m_bidOccupancy(OCCUPANCY_SIZE, 0), 
    m_askOccupancy(OCCUPANCY_SIZE, 0) 
{}

bool OrderBook::releaseOrder(size_t orderIdx) {
    Order* ptr{ m_orderPool.get(orderIdx) };
    if (!ptr)
        return false;
    Order order{ *ptr };
    if (!m_orderPool.release(orderIdx))
        return false;

    auto& side = order.side == Side::Buy ? m_bids : m_asks;
    auto& occupancy = order.side == Side::Buy ? m_bidOccupancy : m_askOccupancy;
    size_t& bestIdx = order.side == Side::Buy ? m_bestBid : m_bestAsk;

    m_orderLocations.erase(order.id);

    if (order.next == INVALID_IDX && order.prev == INVALID_IDX) {
        // Reset this price point to invalid in the vector
        side[order.price] = { INVALID_IDX, INVALID_IDX };

        // zero out bit flag
        size_t occupIdx{ order.price / WORD_SIZE };
        auto& word{ occupancy[occupIdx] };
        auto offset{ order.price % WORD_SIZE };
        word &= ~(UINT64_C(0x01) << offset);

        // if this was the best order for this side, search for a new best
        if (orderIdx == bestIdx) {
            if (orderIdx == bestIdx) {
                bool found{ false };
                if (order.side == Side::Buy) {
                    for (size_t i{occupIdx + 1}; i-- > 0; ) {
                        auto zeros{ std::countr_zero(occupancy[i]) };
                        if (zeros < WORD_SIZE) {
                            auto newPrice{ i * WORD_SIZE + zeros };
                            bestIdx = side[newPrice].head;
                            found = true;
                            break;
                        }
                    }
                } else {
                    for (size_t i{occupIdx}; i < OCCUPANCY_SIZE; ++i) {
                        auto zeros{ std::countr_zero(occupancy[i]) };
                        if (zeros < WORD_SIZE) {
                            auto newPrice{ i * WORD_SIZE + zeros };
                            bestIdx = side[newPrice].head;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found)
                    bestIdx = INVALID_IDX;
            }
        }
    }
    // Not the last order at this price point, so just update the linked list
    else {
        if (order.prev != INVALID_IDX) {
            Order* prev{ m_orderPool.get(order.prev) };
            prev->next = order.next;
        }
        else {
            side[order.price].head = order.next;
        }
        if (order.next != INVALID_IDX) {
            Order* next{ m_orderPool.get(order.next) };
            next->prev = order.prev;
        }
        else {
            side[order.price].tail = order.prev;
        }
        if (orderIdx == bestIdx)
            bestIdx = order.prev == INVALID_IDX ? order.next : order.prev;
    }
    return true;
}

size_t OrderBook::placeOrder(Order& incoming) {
    incoming.sequence = m_next_sequence++;

    auto& incomingSide = incoming.side == Side::Buy ? m_bids : m_asks;
    auto& restingSide = incoming.side == Side::Buy ? m_asks : m_bids;
    auto& incOccupancy = incoming.side == Side::Buy ? m_bidOccupancy : m_askOccupancy;

    auto crosses = incoming.side == Side::Buy
        ? [](int64_t incomingPrice, int64_t restingPrice) { return incomingPrice >= restingPrice; }
        : [](int64_t incomingPrice, int64_t restingPrice) { return incomingPrice <= restingPrice; };

    size_t& bestIdxRestingSide = incoming.side == Side::Buy ? m_bestAsk : m_bestBid;
    size_t& bestIdxIncomingSide = incoming.side == Side::Buy ? m_bestBid : m_bestAsk;

    Order* resting{ m_orderPool.get(bestIdxRestingSide) };
    while (resting && crosses(incoming.price, resting->price)) {
        // Process match
        auto amtTraded{ std::min(incoming.remaining_qty, resting->remaining_qty) };
        incoming.remaining_qty -= amtTraded;
        resting->remaining_qty -= amtTraded;

        auto priceTraded{resting->price * amtTraded};

        if (!resting->remaining_qty)
            releaseOrder(bestIdxRestingSide);
        if (!incoming.remaining_qty)
            return INVALID_IDX; // if this order was immediately exhausted, return early & don't allocate it in the pool

        resting = m_orderPool.get(bestIdxRestingSide);
    }

    // Allocate order in pool
    size_t idx{ m_orderPool.allocate(incoming) };

    m_orderLocations[incoming.id] = idx;

    // If we already have an order with this price, append to the end of the list for that price
    if (incomingSide[incoming.price].head != INVALID_IDX || incomingSide[incoming.price].tail != INVALID_IDX) {
        size_t otherIdx{ incomingSide[incoming.price].tail };
        Order* other{m_orderPool.get(otherIdx)};
        assert(other != nullptr);
        other->next = idx;

        Order* incomingPtr{ m_orderPool.get(idx) };
        incomingPtr->prev = otherIdx;
        incomingSide[incoming.price].tail = idx;
    }
    else {
        // Insert order at this price point
        incomingSide[incoming.price] = { idx, idx };

        // Update best bid/ask if incoming order is better
        Order* best{ m_orderPool.get(bestIdxIncomingSide) };
        if (incoming.side == Side::Buy && (!best || incoming.price > best->price))
            m_bestBid = idx;
        else if (incoming.side == Side::Sell && (!best || incoming.price < best->price))
            m_bestAsk = idx;
        
        // Mark price point as occupied in bitmap
        auto& word{ incOccupancy[incoming.price / WORD_SIZE] };
        auto offset{ incoming.price % WORD_SIZE };
        word |= (UINT64_C(0x01) << offset);
    }

    return idx;
}

bool OrderBook::cancelOrder(uint64_t id) {
    if (!m_orderLocations.contains(id))
        return false;
    return releaseOrder(m_orderLocations.at(id));
}

/* clear() is quite slow: O(POOL_CAPACITY) regardless of how many orders
 * are currently being tracked. This could be improved by keeping track of used slots
 * and only releasing those, but because clear() is not on the hot path and would very
 * rarely be called in a real production environment, there is little to be gained by
 * optimizing it.
 */
void OrderBook::clear(bool resetOrderPool) {
    m_orderLocations.clear();
    if (resetOrderPool)
        m_orderPool.reset();
    for (auto i{0uz}; i < OCCUPANCY_SIZE; ++i) {
        auto bidZeros{ std::countr_zero(m_bidOccupancy[i]) };
        auto askZeros{ std::countr_zero(m_askOccupancy[i]) };
        while (bidZeros < WORD_SIZE) {
            int64_t price{ static_cast<int64_t>(i * WORD_SIZE + bidZeros) };
            m_bids[price] = OrderList{ INVALID_IDX, INVALID_IDX };
            auto& word{ m_bidOccupancy[i] };
            auto offset{ price % WORD_SIZE };
            word &= ~(INT64_C(0x01) << offset);
            bidZeros = std::countr_zero(m_bidOccupancy[i]);
        }
        while (askZeros < WORD_SIZE) {
            int64_t price{ static_cast<int64_t>(i * WORD_SIZE + askZeros) };
            m_asks[price] = OrderList{ INVALID_IDX, INVALID_IDX };
            auto& word{ m_askOccupancy[i] };
            auto offset{ price % WORD_SIZE };
            word &= ~(INT64_C(0x01) << offset);
            askZeros = std::countr_zero(m_askOccupancy[i]);
        }
    }
    m_bestBid = INVALID_IDX;
    m_bestAsk = INVALID_IDX;
}

size_t OrderBook::size() {
    return m_orderLocations.size();
}

int64_t OrderBook::getBestBuyPrice() {
    if (m_bestBid == INVALID_IDX)
        return -1;
    Order* best{ m_orderPool.get(m_bestBid) };
    return best->price;
}

int64_t OrderBook::getBestSellPrice() {
    if (m_bestAsk == INVALID_IDX)
        return -1;
    Order* best{ m_orderPool.get(m_bestAsk) };
    return best->price;
}

std::deque<Order> OrderBook::getBidsAtPrice(int64_t price) {
    std::deque<Order> bids;
    for (Order* bid = m_orderPool.get(m_bids[price].head); bid != nullptr; bid = m_orderPool.get(bid->next))
        bids.push_back(*bid);
    return bids;
}

std::deque<Order> OrderBook::getAsksAtPrice(int64_t price) {
    std::deque<Order> asks;
    for (Order* ask = m_orderPool.get(m_asks[price].head); ask != nullptr; ask = m_orderPool.get(ask->next))
        asks.push_back(*ask);
    return asks;
}

}
