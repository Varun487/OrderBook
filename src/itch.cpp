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
} // namespace itch
