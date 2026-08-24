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
            
            // Gives accessors size() and data() accessing size_ and ptr_ respectively
            std::span<const std::byte> bytes() const;
            
            // Rule of 3 -> If your class needs a descructor, copy constructor or copy assignment
            // it needs all 3
            
            // Copy constructor deleted
            // Runs when a new object is created as a copy of an existing one
            // Example: io::MappedFile b = a;
            MappedFile(const MappedFile&) = delete;

            // Copy assingment deleted
            // Runs when an already-existing object is overwritten with the contents of another
            // Example: io::MappedFile b("file2"); b = a;
            MappedFile& operator=(const MappedFile&) = delete;

        private:
            void* ptr_  = nullptr;
            std::size_t size_ = 0;

    };

} // namespace io
