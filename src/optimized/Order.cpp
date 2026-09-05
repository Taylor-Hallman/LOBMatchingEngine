#include "optimized/Order.h"
#include <print>
#include <string>

namespace optimized {

Order::Order(Side side, int64_t price, uint64_t quantity) : 
    id{ s_next_id++ },
    side{ side }, 
    price{ price }, 
    quantity{ quantity },
    remaining_qty{ static_cast<int64_t>(quantity) }
{}

void Order::LogRemainingQty() const {
    std::string sideStr = side == Side::Buy ? "Bought" : "Sold";
    std::println("Order {0}: {1}/{2} units {3}", id, quantity - remaining_qty, quantity, sideStr);
    if (!remaining_qty)
        std::println("Order {} has been fulfilled and will be closed", id);
}

}
