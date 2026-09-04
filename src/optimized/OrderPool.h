#include "optimized/Order.h"
#include <cassert>
#include <variant>
#include <vector>

class OrderPool {
private:
    std::vector<std::variant<Order, size_t>> m_slots;
    size_t m_head{};
    size_t m_available;

public:
    explicit OrderPool(size_t capacity);
    size_t allocate(const Order& order);
    bool release(size_t handle);
    Order* get(size_t handle);
    int available() const;
    void reset();
};
