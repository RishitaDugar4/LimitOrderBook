// Python bindings for the C++ Orderbook.
//
// Design notes / threading contract
// ---------------------------------
// The underlying Orderbook is SINGLE-PRODUCER: AddOrder / CancelOrder /
// ModifyOrder (and MakeOrder) must only ever be called from one thread at a
// time. These bindings do NOT add any locking of their own -- that would be
// the wrong layer for it. The Python side is responsible for funneling every
// mutating call through a single dedicated submit thread (see
// backend/engine.py). We deliberately keep the binding a thin, honest mirror
// of the C++ API so that contract stays visible.
//
// We release the GIL around AddOrder/CancelOrder/ModifyOrder because those
// block on the matching-engine thread (they Submit() a command and spin until
// it is applied); holding the GIL there would stall every other Python thread
// for no reason.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "orderbook/Orderbook.h"
#include "orderbook/OrderIdGenerator.h"
#include "orderbook/Order.h"
#include "orderbook/OrderModify.h"
#include "orderbook/OrderType.h"
#include "orderbook/Side.h"
#include "orderbook/Trade.h"
#include "orderbook/TradeInfo.h"
#include "orderbook/LevelInfo.h"
#include "orderbook/Usings.h"

namespace py = pybind11;

namespace {

// Plain-old structs mirrored to Python as attribute-readable objects so the
// FastAPI layer can serialize them straight to JSON without reaching back
// into C++ getters.
py::dict TradeInfoToDict(const TradeInfo& info) {
    py::dict d;
    d["order_id"] = info.orderId_;
    d["price"] = info.price_;
    d["quantity"] = info.quantity_;
    return d;
}

py::list TradesToList(const Trades& trades) {
    py::list out;
    for (const auto& trade : trades) {
        py::dict d;
        d["bid"] = TradeInfoToDict(trade.GetBidTrade());
        d["ask"] = TradeInfoToDict(trade.GetAskTrade());
        // Convenience: a "print" price/qty for a trade tape. Bid and ask
        // quantities are equal for a fill; price is the resting (bid) price.
        d["price"] = trade.GetBidTrade().price_;
        d["quantity"] = trade.GetBidTrade().quantity_;
        out.append(std::move(d));
    }
    return out;
}

py::dict LevelInfosToBook(const OrderbookLevelInfos& infos) {
    auto levelsToList = [](const LevelInfos& levels) {
        py::list out;
        for (const auto& level : levels) {
            py::dict d;
            d["price"] = level.price_;
            d["quantity"] = level.quantity_;
            out.append(std::move(d));
        }
        return out;
    };

    py::dict book;
    book["bids"] = levelsToList(infos.GetBids());
    book["asks"] = levelsToList(infos.GetAsks());
    return book;
}

}  // namespace

PYBIND11_MODULE(orderbook_py, m) {
    m.doc() = "Python bindings for the C++ limit order book matching engine";

    py::enum_<OrderType>(m, "OrderType")
        .value("GoodTillCancel", OrderType::GoodTillCancel)
        .value("FillAndKill", OrderType::FillAndKill)
        .value("FillOrKill", OrderType::FillOrKill)
        .value("GoodForDay", OrderType::GoodForDay)
        .value("Market", OrderType::Market);

    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell);

    // Thread-safe id source; the backend mints ids from a single shared
    // instance before handing commands to the submit thread.
    py::class_<OrderIdGenerator>(m, "OrderIdGenerator")
        .def(py::init<OrderId>(), py::arg("start") = 1)
        .def("next", &OrderIdGenerator::Next);

    py::class_<Orderbook>(m, "Orderbook")
        .def(py::init<std::size_t>(),
             py::arg("order_pool_capacity") = Orderbook::kDefaultOrderPoolCapacity)

        // Add a priced order (GoodTillCancel / FillAndKill / FillOrKill /
        // GoodForDay). Returns the list of trades produced. MUST be called
        // from the single submit thread only.
        .def("add_order",
             [](Orderbook& self, OrderType type, OrderId id, Side side,
                Price price, Quantity quantity) {
                 auto order = self.MakeOrder(type, id, side, price, quantity);
                 Trades trades;
                 {
                     py::gil_scoped_release release;
                     trades = self.AddOrder(std::move(order));
                 }
                 return TradesToList(trades);
             },
             py::arg("order_type"), py::arg("order_id"), py::arg("side"),
             py::arg("price"), py::arg("quantity"))

        // Add a market order (no price; fills against the book or is dropped).
        .def("add_market_order",
             [](Orderbook& self, OrderId id, Side side, Quantity quantity) {
                 auto order = self.MakeOrder(id, side, quantity);
                 Trades trades;
                 {
                     py::gil_scoped_release release;
                     trades = self.AddOrder(std::move(order));
                 }
                 return TradesToList(trades);
             },
             py::arg("order_id"), py::arg("side"), py::arg("quantity"))

        .def("cancel_order",
             [](Orderbook& self, OrderId id) {
                 py::gil_scoped_release release;
                 self.CancelOrder(id);
             },
             py::arg("order_id"))

        .def("modify_order",
             [](Orderbook& self, OrderId id, Side side, Price price,
                Quantity quantity) {
                 Trades trades;
                 {
                     py::gil_scoped_release release;
                     trades = self.ModifyOrder(
                         OrderModify{id, side, price, quantity});
                 }
                 return TradesToList(trades);
             },
             py::arg("order_id"), py::arg("side"), py::arg("price"),
             py::arg("quantity"))

        .def("size", &Orderbook::Size)

        // L2 snapshot: {"bids": [{price, quantity}, ...], "asks": [...]}.
        // bids are highest-first, asks lowest-first (the container ordering).
        .def("get_order_infos",
             [](const Orderbook& self) {
                 return LevelInfosToBook(self.GetOrderInfos());
             });
}