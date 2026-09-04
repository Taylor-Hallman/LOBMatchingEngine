#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

static inline constexpr size_t INVALID_IDX{ std::numeric_limits<size_t>::max() };

enum class Side {
    Buy,
    Sell
};

class Order {
public:
    static std::atomic<uint64_t> s_next_id; // atomic for thread safety
    uint64_t m_id;
    Side m_side;
    int64_t m_price;
    uint64_t m_quantity;
    int64_t m_remaining_qty;
    uint64_t m_sequence;
    size_t m_next{ INVALID_IDX }, m_prev{ INVALID_IDX }; Order(Side side, int64_t price, uint64_t quantity);
    void LogRemainingQty() const;
};
