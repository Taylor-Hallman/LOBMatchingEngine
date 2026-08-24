#pragma once

#include "../Order.h"
#include <random>
#include <print>

inline int64_t randomNum(int64_t min, int64_t max) {
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist(min, max);

    return dist(rng);
}

inline Order GenerateOrder(Side side, int64_t priceFloor = 5000, int64_t priceCeiling = 15000, uint64_t quantityFloor = 1, uint64_t quantityCeiling = 100) {
    static uint64_t id{ UINT64_C(0) };
    int64_t price{ randomNum(priceFloor, priceCeiling) };
    uint64_t quantity{ static_cast<uint64_t>(randomNum(quantityFloor, quantityCeiling)) };
    Order order{
        .side = side,
        .price = price,
        .quantity = quantity,
    };
    
    std::string sideTxt = side == Side::Buy ? "Buy" : "Sell";
    std::println(
        "Generated order {0} to {1} {2} options for ${3}.{4}", 
        order.id, 
        sideTxt, 
        quantity,
        price / INT64_C(100),
        price % INT64_C(100)
    );
    
    return order;
}

inline Order GenerateOrder(int64_t priceFloor = 5000, int64_t priceCeiling = 15000, uint64_t quantityFloor = 1, uint64_t quantityCeiling = 100) {
    Side side{ static_cast<Side>(randomNum(0, 1)) };
    return GenerateOrder(side, priceFloor, priceCeiling, quantityFloor, quantityCeiling);
}
