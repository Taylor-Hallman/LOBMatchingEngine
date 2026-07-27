#ifndef ORDER_H
#define ORDER_H

#include <cstdint>

enum class Side {
    Buy,
    Sell
};

struct Order {
    uint64_t id;
    Side side;
    int64_t price;
    uint64_t quantity;
    int64_t remaining_qty{ static_cast<int64_t>(quantity) };
    uint64_t sequence;

    void LogRemainingQty();
};
#endif
