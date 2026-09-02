#include <list>
#include <vector>
#include "optimized/Order.h"
#include "optimized/OrderPool.h"

static inline constexpr size_t POOL_SIZE{ 1'000'000 };
static inline constexpr size_t MAX_PRICE{ 10'000'00 };

class OrderBook {
private:
    OrderPool m_orderPool;
    std::vector<size_t> m_bids, m_asks;
public:
    OrderBook();
    void placeOrder(Order& incoming);
    void processMatch(Order& incoming, Order& resting);
    bool cancelOrder(uint64_t id);
    void clear();
    size_t size();
    int64_t getBestBuyPrice();
    int64_t getBestSellPrice();

};
