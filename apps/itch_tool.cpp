#include <iostream>
#include <cstdint>
#include <array>

#include "itch.hpp"
#include "mmap.hpp"

int main(int argc, char** argv) {
    // Handle command line arguments
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " path/to/<itch-file>" << std::endl;
        return 1;
    }

    // Version
    std::cout << "itch_tool version: " << itch::version() << std::endl;

    // Mapped file object
    io::MappedFile file(argv[1]);

    // File data
    const auto data = file.bytes();

    std::cout << "Mapped file " << argv[1] << " of size " << data.size() << std::endl;

    std::array<std::uint64_t, 256> counts{};
    
    // Main parse
    std::size_t i = 0;
    while (i + 2 <= data.size()) {
        // Need to convert from big endian to a little endian as this is speread over 2 bytes
        const std::uint16_t len = itch::read_be<std::uint16_t>(data.data() + i);

        // Ensures that the frame length is not out of the file's bounds
        if (len == 0 || i + 2 + len > data.size()) {
            std::cerr << "bad frame at offset " << i << ": len " << len << std::endl;
            return 1;
        }

        // No need to convert from big endian to little endian as this is a single byte.
        const auto type = std::to_integer<std::uint8_t>(file.bytes()[i + 2]);
        
        // std::cout << "Msg len (bytes): " << len << ", Msg type: " << type << ", i: " << i << std::endl;

        if (type == 'A') {
            const itch::AddOrder A = itch::decode_add(data.data() + i + 2);
            // std::cout
            //   << "A off="   << i
            //   << " ts="     << A.timestamp
            //   << " ref="    << A.order_ref                                                                                                                                                           
            //   << " locate=" << A.stock_locate
            //   << " track="  << A.tracking_number
            //   << " side="   << A.side                                                                                                                                                                
            //   << " shares=" << A.shares
            //   << " stock='" << std::string_view(A.stock.data(), A.stock.size()) << '\''
            //   << " price="  << A.price
            //   << '\n';
        }
        else if (type == 'F') {
            const itch::AddOrderMpid F = itch::decode_add_mpid(data.data() + i + 2);
            // std::cout
            //   << "F off="   << i
            //   << " ts="     << F.base.timestamp
            //   << " ref="    << F.base.order_ref                                                                                                                                                           
            //   << " locate=" << F.base.stock_locate
            //   << " track="  << F.base.tracking_number
            //   << " side="   << F.base.side                                                                                                                                                                
            //   << " shares=" << F.base.shares
            //   << " stock='" << std::string_view(F.base.stock.data(), F.base.stock.size()) << '\''
            //   << " price="  << F.base.price
            //   << " mpid='" << std::string_view(F.mpid.data(), F.mpid.size()) << '\''
            //   << '\n';
        }

        counts[type]++;

        i += 2 + len;
    }

    // Final check post loop
    if (i != data.size()) {
        std::cerr << "truncated tail: stopped at offset " << i << " of " << data.size() << std::endl;
        return 1;
    }

    // Print counts of messages
    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            std::cout << static_cast<char>(i) << " " << i  << " " << counts[i] << std::endl;
        }
    }
    
    return 0;
}
