#include "optimized/Order.h"
#include <print>
#include <string>

Order::Order(Side side, int64_t price, uint64_t quantity) : 
    m_id{ s_next_id++ },
    m_side{ side }, 
    m_price{ price }, 
    m_quantity{ quantity },
    m_remaining_qty{ static_cast<int64_t>(quantity) }
{}

void Order::LogRemainingQty() const {
    std::string sideStr = m_side == Side::Buy ? "Bought" : "Sold";
    std::println("Order {0}: {1}/{2} units {3}", m_id, m_quantity - m_remaining_qty, m_quantity, sideStr);
    if (!m_remaining_qty)
        std::println("Order {} has been fulfilled and will be closed", m_id);
}
