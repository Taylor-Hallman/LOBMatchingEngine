#include <deque>
#include <unordered_map>
#include <vector>
#include "optimized/Order.h"
#include "optimized/OrderPool.h"

namespace optimized {

inline constexpr size_t POOL_SIZE{ 1'000'000 };
inline constexpr size_t MAX_PRICE{ 10'000'00 };
inline constexpr size_t WORD_SIZE{ 64 };
inline constexpr size_t OCCUPANCY_SIZE{ MAX_PRICE / WORD_SIZE };

class OrderBook {
public:
    using OrderType = Order; // optimized::Order
private:
    OrderPool m_orderPool;
    
    std::unordered_map<uint64_t, size_t> m_orderLocations;

    // We will allocate the entire price range up front.
    // This sacrifices memory for cache locality
    std::vector<size_t> m_bids, m_asks;

    // Bitmap to help search through orders at price points faster
    std::vector<uint64_t> m_bidOccupancy, m_askOccupancy;

    // Cache the highest bid and the lowest ask
    size_t m_bestBid{ INVALID_IDX }, m_bestAsk{ INVALID_IDX };

    uint64_t m_next_sequence{ UINT64_C(0) };

    bool releaseOrder(size_t orderIdx);
public:
    OrderBook();
    void placeOrder(Order& incoming);
    bool cancelOrder(uint64_t id);
    void clear();
    size_t size();
    int64_t getBestBuyPrice();
    int64_t getBestSellPrice();

    // For consistency with naive version
    std::deque<Order> getBidsAtPrice(int64_t price);
    std::deque<Order> getAsksAtPrice(int64_t price);
};

}
