#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

#include "book.hpp"

using book::InstrumentId;
using book::Market;
using book::OrderId;
using book::Price;
using book::Qty;
using book::Quote;
using book::Side;

namespace {

constexpr InstrumentId kInst = 7;

// Every rejection counter must be zero after a clean sequence. Asserting this
// at the end of each happy-path case is what makes a counter that fires when
// it shouldn't visible.
void require_no_rejections(const Market& m) {
    const auto& s = m.stats();
    REQUIRE(s.ref_miss == 0);
    REQUIRE(s.underflow == 0);
    REQUIRE(s.dup_add == 0);
    REQUIRE(s.instrument_mismatch == 0);
}

void require_quote(const std::optional<Quote>& q, Price price, Qty qty, std::uint32_t orders) {
    REQUIRE(q.has_value());
    REQUIRE(q->price == price);
    REQUIRE(q->qty == qty);
    REQUIRE(q->orders == orders);
}

} // namespace

// ---------------------------------------------------------------------------
// Empty state
// ---------------------------------------------------------------------------

TEST_CASE("empty market has no quotes and no live orders") {
    Market m;
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
    REQUIRE_FALSE(m.best_ask(kInst).has_value());
    REQUIRE(m.live_order_count() == 0);
    REQUIRE(m.stats().max_live_orders == 0);
    require_no_rejections(m);
}

TEST_CASE("best_bid on an instrument never seen returns nullopt, not garbage") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    // Above books_.size(): must be bounds-checked, not indexed.
    REQUIRE_FALSE(m.best_bid(kInst + 100).has_value());
    REQUIRE_FALSE(m.best_ask(kInst + 100).has_value());
    // Below books_.size() but never written: resize value-initialised it.
    REQUIRE_FALSE(m.best_bid(kInst - 1).has_value());
}

// ---------------------------------------------------------------------------
// add
// ---------------------------------------------------------------------------

TEST_CASE("add on the buy side sets best_bid and leaves best_ask empty") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 300);
    require_quote(m.best_bid(kInst), 100'0000, 300, 1);
    REQUIRE_FALSE(m.best_ask(kInst).has_value());
    REQUIRE(m.live_order_count() == 1);
    REQUIRE(m.stats().max_live_orders == 1);
    require_no_rejections(m);
}

TEST_CASE("add on the sell side sets best_ask and leaves best_bid empty") {
    Market m;
    m.add(kInst, 1, Side::Sell, 101'0000, 200);
    require_quote(m.best_ask(kInst), 101'0000, 200, 1);
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
}

TEST_CASE("two orders at one price aggregate qty and count both") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.add(kInst, 2, Side::Buy, 100'0000, 250);
    require_quote(m.best_bid(kInst), 100'0000, 350, 2);
    require_no_rejections(m);
}

TEST_CASE("best bid is the highest price, best ask the lowest") {
    Market m;
    m.add(kInst, 1, Side::Buy, 99'0000, 100);
    m.add(kInst, 2, Side::Buy, 100'0000, 100);   // better bid, added second
    m.add(kInst, 3, Side::Buy, 98'0000, 100);
    m.add(kInst, 4, Side::Sell, 102'0000, 100);
    m.add(kInst, 5, Side::Sell, 101'0000, 100);  // better ask, added second
    m.add(kInst, 6, Side::Sell, 103'0000, 100);
    require_quote(m.best_bid(kInst), 100'0000, 100, 1);
    require_quote(m.best_ask(kInst), 101'0000, 100, 1);
}

TEST_CASE("duplicate order id is counted and does not change the book") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.add(kInst, 1, Side::Buy, 100'0000, 999);
    require_quote(m.best_bid(kInst), 100'0000, 100, 1);
    REQUIRE(m.live_order_count() == 1);
    REQUIRE(m.stats().dup_add == 1);
}

TEST_CASE("instruments are independent") {
    Market m;
    m.add(1, 1, Side::Buy, 100'0000, 100);
    m.add(5, 2, Side::Buy, 200'0000, 100);
    require_quote(m.best_bid(1), 100'0000, 100, 1);
    require_quote(m.best_bid(5), 200'0000, 100, 1);
    REQUIRE_FALSE(m.best_bid(3).has_value());
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

TEST_CASE("remove of the only order empties the side") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.remove(kInst, 1);
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
    REQUIRE(m.live_order_count() == 0);
    require_no_rejections(m);
}

TEST_CASE("remove of one order at a level leaves the rest") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.add(kInst, 2, Side::Buy, 100'0000, 200);
    m.add(kInst, 3, Side::Buy, 100'0000, 300);
    m.remove(kInst, 2);   // middle of the queue
    require_quote(m.best_bid(kInst), 100'0000, 400, 2);
    REQUIRE(m.live_order_count() == 2);
    require_no_rejections(m);
}

TEST_CASE("remove of the best level exposes the next one") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.add(kInst, 2, Side::Buy, 99'0000, 500);
    m.remove(kInst, 1);
    require_quote(m.best_bid(kInst), 99'0000, 500, 1);
}

TEST_CASE("remove of an unknown id is a ref miss") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.remove(kInst, 1);
    m.remove(kInst, 1);   // second time: gone
    m.remove(kInst, 42);  // never existed
    REQUIRE(m.stats().ref_miss == 2);
    REQUIRE(m.live_order_count() == 0);
}

TEST_CASE("remove with the wrong instrument is counted and not applied") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.remove(kInst + 1, 1);
    REQUIRE(m.stats().instrument_mismatch == 1);
    REQUIRE(m.stats().ref_miss == 0);
    require_quote(m.best_bid(kInst), 100'0000, 100, 1);
    REQUIRE(m.live_order_count() == 1);
}

TEST_CASE("max_live_orders records the peak, not the current count") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.add(kInst, 2, Side::Buy, 100'0000, 100);
    m.add(kInst, 3, Side::Buy, 100'0000, 100);
    m.remove(kInst, 1);
    m.remove(kInst, 2);
    REQUIRE(m.live_order_count() == 1);
    REQUIRE(m.stats().max_live_orders == 3);
}

// ---------------------------------------------------------------------------
// reduce (X)
// ---------------------------------------------------------------------------

TEST_CASE("reduce takes qty off the order and nothing else") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 500);
    m.add(kInst, 2, Side::Buy, 100'0000, 100);
    m.reduce(kInst, 1, 100);
    require_quote(m.best_bid(kInst), 100'0000, 500, 2);
    REQUIRE(m.live_order_count() == 2);
    require_no_rejections(m);
    REQUIRE(m.stats().reduce_to_zero == 0);
}

TEST_CASE("reduce by more than the order holds is an underflow and is not applied") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.reduce(kInst, 1, 500);
    REQUIRE(m.stats().underflow == 1);
    require_quote(m.best_bid(kInst), 100'0000, 100, 1);   // unchanged, not wrapped
}

TEST_CASE("reduce to exactly zero leaves the order resting until a remove") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.reduce(kInst, 1, 100);
    REQUIRE(m.stats().reduce_to_zero == 1);
    REQUIRE(m.live_order_count() == 1);
    require_quote(m.best_bid(kInst), 100'0000, 0, 1);   // level exists, 1 order, 0 shares
    m.remove(kInst, 1);                                  // the D that follows
    REQUIRE(m.stats().ref_miss == 0);
    REQUIRE(m.live_order_count() == 0);
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
}

TEST_CASE("reduce on an unknown id is a ref miss") {
    Market m;
    m.reduce(kInst, 1, 100);
    REQUIRE(m.stats().ref_miss == 1);
    REQUIRE(m.stats().underflow == 0);
}

// ---------------------------------------------------------------------------
// execute (E / C)
// ---------------------------------------------------------------------------

TEST_CASE("partial execute reduces qty and keeps the order") {
    Market m;
    m.add(kInst, 1, Side::Sell, 101'0000, 300);
    m.execute(kInst, 1, 100);
    require_quote(m.best_ask(kInst), 101'0000, 200, 1);
    REQUIRE(m.live_order_count() == 1);
    REQUIRE(m.stats().full_fills == 0);
    require_no_rejections(m);
}

TEST_CASE("full execute removes the order and counts a full fill") {
    Market m;
    m.add(kInst, 1, Side::Sell, 101'0000, 300);
    m.execute(kInst, 1, 300);
    REQUIRE(m.stats().full_fills == 1);
    REQUIRE(m.live_order_count() == 0);
    REQUIRE_FALSE(m.best_ask(kInst).has_value());
    m.remove(kInst, 1);   // no D follows a full fill; one arriving is a miss
    REQUIRE(m.stats().ref_miss == 1);
}

TEST_CASE("execute in two partials that sum to the size is a full fill") {
    Market m;
    m.add(kInst, 1, Side::Sell, 101'0000, 300);
    m.execute(kInst, 1, 100);
    m.execute(kInst, 1, 200);
    REQUIRE(m.stats().full_fills == 1);
    REQUIRE(m.live_order_count() == 0);
}

TEST_CASE("execute by more than the order holds is an underflow and is not applied") {
    Market m;
    m.add(kInst, 1, Side::Sell, 101'0000, 100);
    m.execute(kInst, 1, 101);
    REQUIRE(m.stats().underflow == 1);
    REQUIRE(m.stats().full_fills == 0);
    require_quote(m.best_ask(kInst), 101'0000, 100, 1);
    REQUIRE(m.live_order_count() == 1);
}

// ---------------------------------------------------------------------------
// replace (U)
// ---------------------------------------------------------------------------

TEST_CASE("replace to a new price moves the order and retires the old id") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.replace(kInst, 1, 2, 99'0000, 150);
    require_quote(m.best_bid(kInst), 99'0000, 150, 1);
    REQUIRE(m.live_order_count() == 1);
    require_no_rejections(m);

    m.remove(kInst, 1);   // old id is dead
    REQUIRE(m.stats().ref_miss == 1);
    REQUIRE(m.live_order_count() == 1);

    m.remove(kInst, 2);   // new id is live
    REQUIRE(m.stats().ref_miss == 1);
    REQUIRE(m.live_order_count() == 0);
}

TEST_CASE("replace inherits the side from the original") {
    Market m;
    m.add(kInst, 1, Side::Sell, 101'0000, 100);
    m.replace(kInst, 1, 2, 102'0000, 100);
    require_quote(m.best_ask(kInst), 102'0000, 100, 1);
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
}

TEST_CASE("replace at the same price keeps the level with a new id") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.add(kInst, 2, Side::Buy, 100'0000, 200);
    m.replace(kInst, 1, 3, 100'0000, 100);
    require_quote(m.best_bid(kInst), 100'0000, 300, 2);
    REQUIRE(m.live_order_count() == 2);
    require_no_rejections(m);
    // Queue position is not observable through the read API yet: the
    // "replace goes to the back" check needs step 3's FIFO assertion on
    // execute, or order-level detail from depth(). Add it there.
}

TEST_CASE("replace of an unknown id is a ref miss and adds nothing") {
    Market m;
    m.replace(kInst, 1, 2, 100'0000, 100);
    REQUIRE(m.stats().ref_miss == 1);
    REQUIRE(m.live_order_count() == 0);
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
}

TEST_CASE("replace with the wrong instrument is counted and not applied") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 100);
    m.replace(kInst + 1, 1, 2, 99'0000, 100);
    REQUIRE(m.stats().instrument_mismatch == 1);
    require_quote(m.best_bid(kInst), 100'0000, 100, 1);
    REQUIRE(m.live_order_count() == 1);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("a full lifecycle closes with nothing live and no rejections") {
    Market m;
    m.add(kInst, 1, Side::Buy, 100'0000, 500);
    m.add(kInst, 2, Side::Buy, 99'0000, 300);
    m.add(kInst, 3, Side::Sell, 101'0000, 400);
    m.reduce(kInst, 1, 100);          // X
    m.execute(kInst, 3, 150);         // E partial
    m.replace(kInst, 2, 4, 100'0000, 300);   // U joins the best level
    require_quote(m.best_bid(kInst), 100'0000, 700, 2);
    require_quote(m.best_ask(kInst), 101'0000, 250, 1);
    m.execute(kInst, 3, 250);         // E full
    m.remove(kInst, 1);               // D
    m.remove(kInst, 4);               // D
    REQUIRE(m.live_order_count() == 0);
    REQUIRE(m.stats().max_live_orders == 3);
    REQUIRE(m.stats().full_fills == 1);
    REQUIRE_FALSE(m.best_bid(kInst).has_value());
    REQUIRE_FALSE(m.best_ask(kInst).has_value());
    require_no_rejections(m);
}
