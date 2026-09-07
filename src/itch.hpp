#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>


namespace itch {

    // Return the current version
    std::uint64_t version();

    // The ITCH file is big-endian: every number in it is stored most-significant-byte first. 
    // Any x86 or ARM machine you'll run this on is little-endian: it stores numbers 
    // least-significant-byte first.
    // This function turns the big endian into a little endian format for the CPU.
    // This function lives in hpp as it's a template. 
    // A template isn't a function; it's a recipe the compiler uses to generate a function 
    // the moment it sees a call like read_be<uint32_t>(p). To generate that code, 
    // the compiler must be able to see the full recipe — the body — in that same translation unit.
    template <typename T>
    T read_be(const std::byte* p) {
        static_assert(
            sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, 
            "read_be supports 1/2/4/8-byte types; use read_be48 for timestamps"
        );
        T v;
        std::memcpy(&v, p, sizeof(T));
        if constexpr (sizeof(T) == 1) return v; // Just 1 byte, nothing to reverse
        else if constexpr (sizeof(T) == 2) return __builtin_bswap16(v);
        else if constexpr (sizeof(T) == 4) return __builtin_bswap32(v);
        else if constexpr (sizeof(T) == 8) return __builtin_bswap64(v);
    }

    // Specific to timestamps as they are 6 bytes
    // value = B0·256⁵ + B1·256⁴ + B2·256³ + B3·256² + B4·256 + B5 (big endian)
    //    = (B0 << 40) | (B1 << 32) | (B2 << 24) | (B3 << 16) | (B4 << 8) | B5
    // We shift p[0] by 40 bits (5 bytes) regardless of the big endianess of
    // orginal byte stream as this arithmetic is performed at the register level
    // so this is correct regardless of the little or big endianess of the CPU.
    // The register autmatically takes care of shifting the bytes correctly
    // regardless of whether it's MSB or LSB. In the read_be function, we need
    // the bswap as memcpy does a simple byte copy without caring about endianess.
    // So it copies big endian values in the file directly to uint, which is the 
    // mirror image of the actual value as the computer is little endian.
    // Whenever we touch memory directly (memcpy) we nee a swap. But, if we are
    // computing a value, we don't.
    std::uint64_t read_be48(const std::byte* p);

    // Helper function for reading stock name
    template <std::size_t N>
    std::array<char, N> read_chars(const std::byte* p) {
        std::array<char, N> a;
        std::memcpy(a.data(), p, N);                                                                                                                                                                   
        return a;
    }

    // Helper function to see if a char is present in a string
    constexpr bool one_of(char c, std::string_view allowed) {
        return allowed.find(c) != std::string_view::npos;
    }

    // Return expected length in bytes for each message type
    // constexpr implies an inline function, which must be defined in hpp
    constexpr std::size_t expected_size(std::uint8_t type) {
        switch (type)
        {
            case 'A': return 36;  // Add Order (no MPID)
            case 'F': return 40;  // Add Order with MPID
            case 'E': return 31;  // Order Executed
            case 'C': return 36;  // Order Executed with Price
            case 'X': return 23;  // Order Cancel (partial reduction)
            case 'D': return 19;  // Order Delete
            case 'U': return 35;  // Order Replace
            case 'P': return 44;  // Trade (non-cross)
            case 'Q': return 40;  // Cross Trade
            case 'B': return 19;  // Broken Trade
            case 'S': return 12;  // System Event
            case 'R': return 39;  // Stock Directory
            case 'H': return 25;  // Stock Trading Action
            case 'h': return 21;  // Operational Halt  (lowercase — not 'H')
            case 'Y': return 20;  // Reg SHO Restriction
            case 'L': return 26;  // Market Participant Position
            case 'V': return 35;  // MWCB Decline Level
            case 'W': return 12;  // MWCB Status
            case 'K': return 28;  // IPO Quoting Period Update
            case 'J': return 35;  // LULD Auction Collar
            case 'I': return 50;  // NOII / imbalance
            case 'N': return 20;  // Retail Price Improvement Indicator
            default: return 0;
        }
    }

    // Structs for each message type
    // A — Add order, 36 bytes (Refer section 1.3.1 in the spec)
    struct AddOrder {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t order_ref;
        char side;
        std::uint32_t shares;
        std::array<char, 8> stock;
        std::uint32_t price;
    };

    // F — Add order mpid, 40 bytes (Refer section 1.3.2 in the spec)
    struct AddOrderMpid {        // 'F' — 40 bytes on the wire                                                                                                      
        AddOrder base;                                                                                                                                                                                 
        std::array<char, 4> mpid;
    };

    // D - Delete order, 19 bytes (Refer section 1.4.4 in the spec)
    struct DeleteOrder {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t order_ref;
    };

    // X - Cancel order, 23 bytes (Refer section 1.4.3 in the spec)
    struct CancelOrder {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t order_ref;
        std::uint32_t cancelled_shares;
    };

    // E - Executed order, 31 bytes (Refer section 1.4.1 in the spec)
    struct ExecutedOrder {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t order_ref;
        std::uint32_t executed_shares;
        std::uint64_t match_number;
    };

    // C - Order Executed With Price, 36 bytes (Refer section 1.4.2 in the spec)
    struct ExecutedWithPriceOrder {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t order_ref;
        std::uint32_t executed_shares;
        std::uint64_t match_number;
        char printable;
        std::uint32_t execution_price;
    };

    // U - Replace order, 35 bytes (Refer section 1.4.5 in the spec)
    struct ReplaceOrder {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t orig_order_ref;
        std::uint64_t new_order_ref;
        std::uint32_t shares;
        std::uint32_t price;
    };

    // R - Stock Directory, 39 bytes (Refer section 1.2.1 in the spec)
    struct StockDirectory {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::array<char, 8> stock;
        char market_category;
        char financial_status_indicator;
        std::uint32_t round_lot_size;
        char round_lots_only;
        char issue_classification;
        std::array<char, 2> issue_sub_type;
        char authenticity;
        char short_sale_threshold;
        char ipo_flag;
        char luld_reference_price_tier;
        char etp_flag;
        std::uint32_t etp_leverage_factor;
        char inverse_indicator;
    };

    // Decode functions for the struct
    AddOrder decode_add(const std::byte* p); // A
    AddOrderMpid decode_add_mpid(const std::byte* p); // F
    DeleteOrder decode_delete(const std::byte* p); // D
    CancelOrder decode_cancel(const std::byte* p); // X
    ExecutedOrder decode_execute(const std::byte* p); // E
    ExecutedWithPriceOrder decode_execute_with_price(const std::byte* p); // C
    ReplaceOrder decode_replace(const std::byte* p); // U
    StockDirectory decode_stock_directory(const std::byte* p); // R

    // Message validation functions
    bool valid_side(char side);
    bool valid_shares(std::uint32_t shares);
    bool valid_price(std::uint32_t price);
    bool valid_stock(std::array<char, 8> stock);
    bool valid_order_ref(std::uint64_t ref);
    bool valid_printable(char printable);
    bool valid_stock_directory(const StockDirectory& m);

} // namespace itch
