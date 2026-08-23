#include "mmap.hpp"

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdexcept>
#include <string>
#include <cstddef>
#include <span>


namespace io {

    MappedFile::MappedFile(const char* path) {
        // std::cout << "File: " << path << std::endl;
        
        // POSIX call to read a file
        // Choosing this instead of fstream as we want to mmap the file
        // and not worry about edge cases where the buffer cuts off the 
        // message's bytes near the end
        int fd = open(path, O_RDONLY);
        // std::cout << "Opened: " << path << std::endl;

        // Check for errors
        if (fd == -1) {
            throw std::runtime_error(std::string("open failed for ") + path + ": " + std::strerror(errno));
        }

        // Get file's metadata 
        struct stat st;
        if (fstat(fd, &st) != 0) {
            close(fd);
            throw std::runtime_error(std::string("fstat failed for ") + path + ": " + std::strerror(errno));
        }
        // std::cout << "Size of file: " << st.st_size << std::endl;

        // mmap the file to make it a contiguous set of bits
        char* mapped_data = static_cast<char*>(mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
        // std::cout << "Mmaped data for file: " << path << std::endl;
        if (mapped_data == MAP_FAILED) {
            close(fd);
            throw std::runtime_error(std::string("mmap failed for ") + path + ": " + std::strerror(errno));
        }
        
        // Close the file
        close(fd);
        // std::cout << "Closed: " << path << std::endl;

        // Store data
        ptr_ = mapped_data;
        size_ = st.st_size;

    }

    MappedFile::~MappedFile() {
        munmap(ptr_, size_);
        // std::cout << "Unmaped data." << std::endl;
    }

    std::span<const std::byte> MappedFile::bytes() const {
        return {static_cast<const std::byte*>(ptr_), size_};
    }

} // namespace io
