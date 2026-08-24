#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <cstring>

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

} // namespace itch
