#include <print>
#include <string>
#include "Order.h"

void Order::LogRemainingQty() {
    std::string sideStr = side == Side::Buy ? "Bought" : "Sold";
    std::println("Order {0}: {1}/{2} units {3}", id, quantity - remaining_qty, quantity, sideStr);
    if (!remaining_qty)
        std::println("Order {} has been fulfilled and will be closed", id);
}
