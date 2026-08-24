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
    
    std::size_t i = 0;
    while (i + 2 <= data.size()) {
        const std::uint16_t len = itch::read_be<std::uint16_t>(data.data() + i);

        // Ensures that the frame length is not out of the file's bounds
        if (len == 0 || i + 2 + len > data.size()) {
            std::cerr << "bad frame at offset " << i << ": len " << len << std::endl;
            return 1;
        }

        const auto type = std::to_integer<std::uint8_t>(file.bytes()[i + 2]);
        
        // std::cout << "Msg len (bytes): " << len << ", Msg type: " << type << ", i: " << i << std::endl;

        counts[type]++;

        i += 2 + len;
    }

    if (i != data.size()) {
        std::cerr << "truncated tail: stopped at offset " << i << " of " << data.size() << std::endl;
        return 1;
    }

    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            std::cout << static_cast<char>(i) << " " << counts[i] << std::endl;
        }
    }
    
    return 0;
}
