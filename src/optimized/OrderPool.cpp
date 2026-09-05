#include "optimized/OrderPool.h"
#include <numeric>
#include <utility>

/* Order Pool
 * This is a pool allocator for orders; another instance of trading memory for speed.
 * We allocate a big block of memory up front and carve it into identical chunks that store Order objects.
 * All operations are O(1) and we never have to search for a free slot. The free list is threaded through
 * the free slots themselves, saving space. In the case that we exhaust the pool we initially allocated,
 * the order pool will allocate more space by performing a resize with a growth factor of 2.
 */

namespace optimized {

OrderPool::OrderPool(size_t capacity) : m_available{ capacity }, m_slots(capacity, std::variant<Order, size_t>{std::in_place_index<1>, 0}) {
    std::iota(m_slots.begin(), m_slots.end() - 1, 1uz);
    m_slots[capacity - 1] = INVALID_IDX;
}

size_t OrderPool::allocate(const Order& order) {
    if (m_head == INVALID_IDX)
        ReAlloc(m_slots.size() * GROWTH_FACTOR);

    --m_available;
    auto& head{ m_slots[m_head] };
    size_t nextFree{ std::get<size_t>(head) };
    assert(nextFree == INVALID_IDX || m_slots[nextFree].index() == 1);

    size_t insertedIdx{ m_head };
    m_head = nextFree;
    head = order;
    return insertedIdx;
}

bool OrderPool::release(size_t handle) {
    if (handle >= m_slots.size() || m_slots[handle].index() == 1)
        return false;

    m_slots[handle] = m_head;
    m_head = handle;
    ++m_available;
    return true;
}

Order* OrderPool::get(size_t handle) {
    if (handle >= m_slots.size())
        return nullptr;
    return std::get_if<Order>(&m_slots[handle]);
}

int OrderPool::available() const {
    return m_available;
}

void OrderPool::reset() {
    std::iota(m_slots.begin(), m_slots.end() - 1, 1uz);
    m_slots[m_slots.size() - 1] = INVALID_IDX;
    m_available = m_slots.size();
    m_head = 0;
}

void OrderPool::ReAlloc(size_t new_capacity) {
    size_t old_capacity{ m_slots.size() };
    m_slots.resize(new_capacity, std::variant<Order, size_t>{std::in_place_index<1>, 0 });
    std::iota(m_slots.begin() + old_capacity, m_slots.end() - 1, old_capacity + 1);
    m_slots[new_capacity - 1] = INVALID_IDX;
    m_head = old_capacity;
}

}
