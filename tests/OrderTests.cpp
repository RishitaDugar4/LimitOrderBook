#include <catch2/catch_test_macros.hpp>

#include "Order.h"
#include "OrderModify.h"
#include "Constants.h"

TEST_CASE("A freshly constructed order is unfilled", "[order]") {
    Order order(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);

    REQUIRE(order.GetInitialQuantity() == 10);
    REQUIRE(order.GetRemainingQuantity() == 10);
    REQUIRE(order.GetFilledQuantity() == 0);
    REQUIRE_FALSE(order.IsFilled());
}

TEST_CASE("Fill reduces remaining quantity and tracks filled quantity", "[order]") {
    Order order(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);

    order.Fill(4);

    REQUIRE(order.GetRemainingQuantity() == 6);
    REQUIRE(order.GetFilledQuantity() == 4);
    REQUIRE_FALSE(order.IsFilled());
}

TEST_CASE("Filling an order for its full remaining quantity marks it filled", "[order]") {
    Order order(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);

    order.Fill(10);

    REQUIRE(order.GetRemainingQuantity() == 0);
    REQUIRE(order.IsFilled());
}

TEST_CASE("Filling an order for more than its remaining quantity throws", "[order]") {
    Order order(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);

    REQUIRE_THROWS_AS(order.Fill(11), std::logic_error);
}

TEST_CASE("A Market order is constructed with an invalid price and Market type", "[order]") {
    Order order(1, Side::Buy, 10);

    REQUIRE(order.GetOrderType() == OrderType::Market);
    REQUIRE(order.GetPrice() == Constants::InvalidPrice);
    REQUIRE(order.GetInitialQuantity() == 10);
}

TEST_CASE("ToGoodTillCancel converts a Market order into a priced GoodTillCancel order", "[order]") {
    Order order(1, Side::Buy, 10);

    order.ToGoodTillCancel(150);

    REQUIRE(order.GetOrderType() == OrderType::GoodTillCancel);
    REQUIRE(order.GetPrice() == 150);
}

TEST_CASE("ToGoodTillCancel throws when called on a non-Market order", "[order]") {
    Order order(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);

    REQUIRE_THROWS_AS(order.ToGoodTillCancel(150), std::logic_error);
}

TEST_CASE("OrderModify builds a new order that preserves the requested fields", "[order]") {
    OrderModify modification(42, Side::Sell, 105, 7);
    auto order = modification.ToOrderPointer(OrderType::GoodForDay);

    REQUIRE(order->GetOrderId() == 42);
    REQUIRE(order->GetSide() == Side::Sell);
    REQUIRE(order->GetPrice() == 105);
    REQUIRE(order->GetInitialQuantity() == 7);
    REQUIRE(order->GetOrderType() == OrderType::GoodForDay);
}
