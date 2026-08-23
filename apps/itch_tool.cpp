#include <iostream>

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
    std::cout << "Mapped file " << argv[1] << " of size " << file.bytes().size() << std::endl;

    return 0;
}
