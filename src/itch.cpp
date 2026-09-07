#include "itch.hpp"

namespace itch {

    std::uint64_t version() {
        return 1;
    }

    std::uint64_t read_be48(const std::byte* p) {
        std::uint64_t v = (static_cast<uint64_t>(p[0]) << 40)
                    | (static_cast<uint64_t>(p[1]) << 32)
                    | (static_cast<uint64_t>(p[2]) << 24)
                    | (static_cast<uint64_t>(p[3]) << 16)
                    | (static_cast<uint64_t>(p[4]) << 8)
                    | (static_cast<uint64_t>(p[5]));
        return v;
    }

    AddOrder decode_add(const std::byte* p) {
        return AddOrder {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .order_ref = itch::read_be<std::uint64_t>(p+11),
            .side = std::to_integer<char>(p[19]),
            .shares = itch::read_be<std::uint32_t>(p+20),
            .stock = itch::read_chars<8>(p + 24),
            .price = itch::read_be<std::uint32_t>(p+32)
        };
    }

    AddOrderMpid decode_add_mpid(const std::byte* p) {
        const itch::AddOrder m = itch::decode_add(p);
        return AddOrderMpid {
            .base = m,
            .mpid = itch::read_chars<4>(p + 36)
        };
    }

    DeleteOrder decode_delete(const std::byte* p) { 
        return DeleteOrder {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .order_ref = itch::read_be<std::uint64_t>(p+11)
        };
    }

    CancelOrder decode_cancel(const std::byte* p) { 
        return CancelOrder {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .order_ref = itch::read_be<std::uint64_t>(p+11),
            .cancelled_shares = itch::read_be<std::uint32_t>(p+19)
        };
    }

    ExecutedOrder decode_execute(const std::byte* p) {
        return ExecutedOrder {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .order_ref = itch::read_be<std::uint64_t>(p+11),
            .executed_shares = itch::read_be<std::uint32_t>(p+19),
            .match_number = itch::read_be<std::uint64_t>(p+23)
        };
    }

    ExecutedWithPriceOrder decode_execute_with_price(const std::byte* p) {
        return ExecutedWithPriceOrder {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .order_ref = itch::read_be<std::uint64_t>(p+11),
            .executed_shares = itch::read_be<std::uint32_t>(p+19),
            .match_number = itch::read_be<std::uint64_t>(p+23),
            .printable = std::to_integer<char>(p[31]),
            .execution_price = itch::read_be<std::uint32_t>(p+32)
        };
    }
    
    ReplaceOrder decode_replace(const std::byte* p) {
        return ReplaceOrder {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .orig_order_ref = itch::read_be<std::uint64_t>(p+11),
            .new_order_ref = itch::read_be<std::uint64_t>(p+19),
            .shares = itch::read_be<std::uint32_t>(p+27),
            .price = itch::read_be<std::uint32_t>(p+31)
        };
    }

    StockDirectory decode_stock_directory(const std::byte* p) {
        return StockDirectory {
            .stock_locate = itch::read_be<std::uint16_t>(p+1),
            .tracking_number = itch::read_be<std::uint16_t>(p+3),
            .timestamp = itch::read_be48(p+5),
            .stock = itch::read_chars<8>(p+11),
            .market_category = std::to_integer<char>(p[19]),
            .financial_status_indicator = std::to_integer<char>(p[20]),
            .round_lot_size = itch::read_be<std::uint32_t>(p+21),
            .round_lots_only = std::to_integer<char>(p[25]),
            .issue_classification = std::to_integer<char>(p[26]),
            .issue_sub_type = itch::read_chars<2>(p+27),
            .authenticity = std::to_integer<char>(p[29]),
            .short_sale_threshold = std::to_integer<char>(p[30]),
            .ipo_flag = std::to_integer<char>(p[31]),
            .luld_reference_price_tier = std::to_integer<char>(p[32]),
            .etp_flag = std::to_integer<char>(p[33]),
            .etp_leverage_factor = itch::read_be<std::uint32_t>(p+34),
            .inverse_indicator = std::to_integer<char>(p[38])
        };
    }

    bool valid_side(char side) {
        return (side == 'B' || side == 'S');
    }

    bool valid_shares(std::uint32_t shares) {
        return (shares != 0);
    }

    bool valid_price(std::uint32_t price) {
        return (price != 0);
    }

    bool valid_stock(std::array<char, 8> stock) {
        bool padding = false;
        for (std::size_t k = 0; k < stock.size(); ++k) {
            const char c = stock[k];

            if (c == ' ') { padding = true; continue; }
            if (padding) return false; // "AA PL   " — space then a char

            const bool alpha = (c >= 'A' && c <= 'Z');
            const bool punct = (c == '.' || c == '-' || c == '+' ||
                                c == '=' || c == '^' || c == '*');

            if (k == 0 ? !alpha : !(alpha || punct)) return false;
        }
        return stock[0] != ' '; // reject an all-space field    
    }

    bool valid_order_ref(std::uint64_t ref) {
        return ref != 0;
    }

    bool valid_printable(char printable) {
        return (printable == 'Y' || printable == 'N');
    }

    bool valid_stock_directory(const StockDirectory& m) {
        // Checks based on section 1.2.1 of the Spec
        const bool ok_category = m.market_category == ' ' || one_of(m.market_category, "QGSNAPMZV");
  
        const bool ok_financial = m.financial_status_indicator == ' ' || one_of(m.financial_status_indicator, "DEQSGHJKCN");

        const bool ok_lots_only = one_of(m.round_lots_only, "YN");

        const bool ok_classification = one_of(m.issue_classification, "ABCFILNOPQRSTUVW");

        const bool ok_authenticity = one_of(m.authenticity, "PT");

        const bool ok_threshold = m.short_sale_threshold == ' ' || one_of(m.short_sale_threshold, "YN");

        const bool ok_ipo = m.ipo_flag == ' ' || one_of(m.ipo_flag, "YNZ");

        const bool ok_luld = m.luld_reference_price_tier == ' ' || one_of(m.luld_reference_price_tier, "12");

        const bool ok_etp = m.etp_flag == ' ' || one_of(m.etp_flag, "YN");

        const bool ok_inverse = one_of(m.inverse_indicator, "YN");
  
        return ok_category && ok_financial && ok_lots_only && ok_classification                                                                                                                  
              && ok_authenticity && ok_threshold && ok_ipo && ok_luld && ok_etp
              && ok_inverse;
    }

} // namespace itch
