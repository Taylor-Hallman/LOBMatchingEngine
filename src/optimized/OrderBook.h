#include <list>
#include <unordered_map>
#include <vector>
#include "optimized/Order.h"

static constexpr size_t POOL_SIZE{ 1'000'000 };
static constexpr size_t MAX_PRICE{ 10'000'00 };

struct NodeHandle {
    Side side;
    std::vector<std::list<size_t>>::iterator arrLoc;
    std::list<size_t>::iterator listLoc;
};

class OrderBook {
private:
    std::vector<Order> m_orderPool;
    std::vector<std::list<size_t>> m_bids, m_asks;
    std::unordered_map<uint64_t, NodeHandle> m_orderLocations;
public:
    OrderBook() {
        m_orderPool.reserve(POOL_SIZE);
        m_bids.reserve(MAX_PRICE);
        m_asks.reserve(MAX_PRICE);
    }

    void placeOrder(Order& incoming);
    void processMatch(Order& incoming, Order& resting);
    bool cancelOrder(uint64_t id);
    void clear();
    size_t size();
    int64_t getBestBuyPrice();
    int64_t getBestSellPrice();

    std::list<Order> getBidsAtPrice(int64_t price);
    std::list<Order> getAsksAtPrice(int64_t price);
};
