#include "book.hpp"

#include <iterator>
#include <cassert>


namespace book {

    std::unordered_map<OrderId, OrderRef>::iterator Market::find_live_order_(InstrumentId instrument, OrderId order_id){
        
        // Get iterator to the order ref
        auto it = live_order_map_.find(order_id);

        // Handle ref miss
        if (it == live_order_map_.end()) {
            stats_.ref_miss++;
            return live_order_map_.end();
        }

        // Order reference
        OrderRef order_ref = it->second;

        // Validate intrument
        if (order_ref.instrument != instrument) {
            stats_.instrument_mismatch++;
            return live_order_map_.end();
        }

        assert(it->second.pos->id == order_id);
        
        return it;

    }

    void Market::erase_live_order_(std::unordered_map<OrderId, OrderRef>::iterator it) {
        
        // Order reference
        OrderRef order_ref = it->second;

        // Instrument
        InstrumentId instrument = order_ref.instrument;

        // Get the price levels in the order book's bids/asks
        auto& price_levels = (order_ref.side == Side::Buy) ? books_[instrument].bids : books_[instrument].asks;
        auto level = price_levels.find(order_ref.price);

        // Ensure level exists
        assert(level != price_levels.end() && "Price level that does not exist");

        // Get the order list at the price level
        auto& orders_list = level->second;

        // Remove order from the list
        orders_list.erase(order_ref.pos);
        if (orders_list.empty()) {
            price_levels.erase(level);
        }
        
        // Update live order map
        live_order_map_.erase(it);

    }

    Qty Market::quote_qty_sum_(const std::list<Order>& order_list) const {
        Qty sum = 0;
        for (const Order& o : order_list) {
            sum += o.qty;
        }
        return sum;
    }

    void Market::add(InstrumentId instrument, OrderId order_id, Side side, Price price, Qty qty) {  

        // Handle dupe order ref
        if (live_order_map_.find(order_id) != live_order_map_.end()) {
            stats_.dup_add++;
            return;
        }

        // Ensure instrument's orderbook is available
        if (instrument >= books_.size()) {
            books_.resize(instrument + 1);
        }

        // Create order
        Order order = Order {
            .id = order_id,
            .qty = qty
        };
        
        // Create iterator for order ref
        std::list<Order>::iterator pos;

        // Get the orders at a particular price level in bids/asks
        auto& order_list = (side == Side::Buy) ? books_[instrument].bids[price] : books_[instrument].asks[price];
        
        // Add the order to the list
        order_list.push_back(order);
        
        // Get position iterator for inserted order
        pos = std::prev(order_list.end());

        // Update live order map
        OrderRef ref = OrderRef {
            .instrument = instrument,
            .side = side,
            .price = price,
            .pos = pos
        };
        live_order_map_[order_id] = ref;

        // Update max live orders count
        if (live_order_count() > stats_.max_live_orders) {
            stats_.max_live_orders = live_order_count();
        }

    }

    void Market::remove(InstrumentId instrument, OrderId order_id) {

        // Get iterator to live order
        auto it = Market::find_live_order_(instrument, order_id);

        // Ensure the iterator exists
        if (it == live_order_map_.end()) {
            return;
        }

        // Remove the order from the book
        erase_live_order_(it);
        
    }

    void Market::reduce(InstrumentId instrument, OrderId order_id, Qty qty) {

        // Get iterator to live order
        auto it = Market::find_live_order_(instrument, order_id);

        // Ensure the iterator exists
        if (it == live_order_map_.end()) {
            return;
        }

        // Order reference
        OrderRef order_ref = it->second;

        // Ensure there is no underflow
        if (qty > order_ref.pos->qty) {
            stats_.underflow++;
            return;
        }
        
        // Reduce qty
        order_ref.pos->qty -= qty;

        // Update if qty is reduced to 0
        if (order_ref.pos->qty == 0) {
            stats_.reduce_to_zero++;
        }

    }

    void Market::execute(InstrumentId instrument, OrderId order_id, Qty exec_qty) {

        // Get iterator to live order
        auto it = Market::find_live_order_(instrument, order_id);

        // Ensure the iterator exists
        if (it == live_order_map_.end()) {
            return;
        }

        // Order reference
        OrderRef order_ref = it->second;

        // Ensure there is no underflow
        if (exec_qty > order_ref.pos->qty) {
            stats_.underflow++;
            return;
        }
        
        // Reduce qty
        order_ref.pos->qty -= exec_qty;

        // Remove if qty is reduced to 0
        if (order_ref.pos->qty == 0) {
            stats_.full_fills++;
            erase_live_order_(it);
        }

    }

    void Market::replace(InstrumentId instrument, OrderId orig_order_id, OrderId new_order_id, Price price, Qty qty) {

        // Get iterator to live order
        auto it = Market::find_live_order_(instrument, orig_order_id);

        // Ensure the iterator exists
        if (it == live_order_map_.end()) {
            return;
        }

        // Order reference
        OrderRef order_ref = it->second;

        // Remove the order from the book
        erase_live_order_(it);

        // Add new order
        Market::add(instrument, new_order_id, order_ref.side, price, qty);

    }

    std::optional<Quote> Market::best_bid(InstrumentId instrument) const {
        if (instrument >= books_.size()) return std::nullopt;
        auto& price_levels = books_[instrument].bids;
        if (price_levels.empty()) {
            return std::nullopt;
        }
        auto it = price_levels.rbegin();
        Qty qty_sum = quote_qty_sum_(it->second);
        return Quote {
            .price = it->first,
            .qty = qty_sum,
            .orders = static_cast<std::uint32_t>(it->second.size())
        };
    }

    std::optional<Quote> Market::best_ask(InstrumentId instrument) const {
        if (instrument >= books_.size()) return std::nullopt;
        auto& price_levels = books_[instrument].asks;
        if (price_levels.empty()) {
            return std::nullopt;
        }
        auto it = price_levels.begin();
        Qty qty_sum = quote_qty_sum_(it->second);
        return Quote {
            .price = it->first,
            .qty = qty_sum,
            .orders = static_cast<std::uint32_t>(it->second.size())
        };
    }

} // namespace book
