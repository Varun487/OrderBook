#pragma once

#include <array>

namespace book {

    // Order - Resting in the book
    struct Order {
        OrderId id;
        Qty     qty;                                                                                                                                                                                 
    };

    // OrderRef - Maps to the orders position in the book
    struct OrderRef {
        InstrumentId instrument;
        Side  side;
        Price price;
        std::list<Order>::iterator pos;
    };
  

} // namespace book