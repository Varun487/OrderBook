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
 
} // namespace itch
