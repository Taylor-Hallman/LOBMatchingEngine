#pragma once

#include <atomic>
#include <cstdint>

enum class Side {
    Buy,
    Sell
};

class Order {
private:
    static std::atomic<uint64_t> s_next_id; // atomic for thread safety
public:
    uint64_t id{ s_next_id++ };
    Side side;
    int64_t price;
    uint64_t quantity;
    int64_t remaining_qty{ static_cast<int64_t>(quantity) };
    uint64_t sequence;

    void LogRemainingQty();
};
