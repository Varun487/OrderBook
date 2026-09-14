#pragma once

#include <map>
#include <unordered_map>
#include <list>
#include <cstdint>
#include <vector>
#include <optional>
#include <cstddef>

namespace book {

    // Aliases
    using OrderId = std::uint64_t;
    using Price = std::uint32_t;
    using Qty = std::uint32_t;
    using InstrumentId = std::uint16_t;

    // Side - Buy / Sell
    enum class Side : std::uint8_t {
        Buy,
        Sell
    };

    // Order - Resting in the book
    struct Order {
        OrderId id;
        Qty qty;
    };

    // OrderRef - Maps an order to its position in the book
    struct OrderRef {
        InstrumentId instrument;
        Side side;
        Price price;
        std::list<Order>::iterator pos;
    };

    // Order Book
    struct OrderBook {
        std::map<Price, std::list<Order>> bids;
        std::map<Price, std::list<Order>> asks;
    };

    // Quote
    struct Quote {
        Price price;
        Qty qty;
        std::uint32_t orders;
    };

    // Stats
    struct BookStats {
        std::uint64_t ref_miss = 0;
        std::uint64_t underflow = 0;
        std::uint64_t full_fills = 0;
        std::uint64_t max_live_orders = 0;
        std::uint64_t dup_add = 0;
        std::uint64_t instrument_mismatch = 0;
        std::uint64_t reduce_to_zero = 0;
    };

    // An order book for each instrument
    class Market {
    public:
        void add(InstrumentId instrument, OrderId order_id, Side side, Price price, Qty qty);
        void remove(InstrumentId instrument, OrderId order_id);
        void reduce(InstrumentId instrument, OrderId order_id, Qty qty);
        void execute(InstrumentId instrument, OrderId order_id, Qty exec_qty);
        void replace(InstrumentId instrument, OrderId orig_order_id, OrderId new_order_id, Price price, Qty qty);
        std::optional<Quote> best_bid(InstrumentId instrument) const;
        std::optional<Quote> best_ask(InstrumentId instrument) const;

        // noexcept - defined inline in hpp
        // compiler can skip emitting exception-unwinding scaffolding around calls to it
        // If an exception does escape, std::terminate is called on the spot
        const BookStats& stats() const noexcept { return stats_; };
        std::size_t live_order_count() const noexcept { return live_order_map_.size(); }
    private:
        std::vector<OrderBook> books_;
        std::unordered_map<OrderId, OrderRef> live_order_map_;
        BookStats stats_;

        std::unordered_map<OrderId, OrderRef>::iterator find_live_order_(InstrumentId instrument, OrderId order_id);
        void erase_live_order_(std::unordered_map<OrderId, OrderRef>::iterator it);
        Qty quote_qty_sum_(const std::list<Order>& order_list) const;
    };

} // namespace book
