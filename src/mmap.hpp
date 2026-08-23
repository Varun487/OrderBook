#pragma once

#include <iostream>
#include <cstddef>
#include <span>


namespace io {

    class MappedFile {

        public:
            // explicit ensures the there's no implcit conversion of an argument to const char*
            // Opens the file at the path and exposes it's bytes as one contiguous read-only range
            explicit MappedFile(const char* path);
            ~MappedFile();
            std::span<const std::byte> bytes() const;

        private:
            void* ptr_  = nullptr;
            std::size_t size_ = 0;

    };

} // namespace io
