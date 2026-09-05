#pragma once

#include <atomic>
#include <cstdint>
#include "Side.h"

namespace naive {

class Order {
private:
    static inline std::atomic<uint64_t> s_next_id{ 0 }; // atomic for thread safety
public:
    uint64_t id;
    Side side;
    int64_t price;
    uint64_t quantity;
    int64_t remaining_qty;
    uint64_t sequence;

    Order(Side side, int64_t price, uint64_t quantity);
    void LogRemainingQty();
};

}
