#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

#include "itch.hpp"

TEST_CASE("ensure version 1 of itch tool") {
    REQUIRE(itch::version() == 1);
}

TEST_CASE("read_be decodes big-endian u32") {
    std::array<std::byte, 4> buf{std::byte{0x01}, std::byte{0x02},
                                std::byte{0x03}, std::byte{0x04}};
    REQUIRE(itch::read_be<std::uint32_t>(buf.data()) == 0x01020304);
}

TEST_CASE("read_be decodes big-endian u16") {
    // Expectation in decimal on purpose: 0x01 0x2C big-endian is 300.
    std::array<std::byte, 2> buf{std::byte{0x01}, std::byte{0x2C}};
    REQUIRE(itch::read_be<std::uint16_t>(buf.data()) == 300);
}

TEST_CASE("read_be decodes big-endian u64") {
    std::array<std::byte, 8> buf{std::byte{0x01}, std::byte{0x02},
                                 std::byte{0x03}, std::byte{0x04},
                                 std::byte{0x05}, std::byte{0x06},
                                 std::byte{0x07}, std::byte{0x08}};
    REQUIRE(itch::read_be<std::uint64_t>(buf.data()) == 0x0102030405060708);
}

TEST_CASE("read_be passes a single byte through unchanged") {
    std::array<std::byte, 1> buf{std::byte{0xAB}};
    REQUIRE(itch::read_be<std::uint8_t>(buf.data()) == 0xAB);
}

TEST_CASE("read_be u32 with top bit set does not sign-extend") {
    std::array<std::byte, 4> buf{std::byte{0xFF}, std::byte{0x00},
                                 std::byte{0x00}, std::byte{0x01}};
    REQUIRE(itch::read_be<std::uint32_t>(buf.data()) == 0xFF000001);
}

TEST_CASE("read_be48 decodes a value wider than 32 bits") {
    std::array<std::byte, 6> buf{std::byte{0x01}, std::byte{0x02},
                                 std::byte{0x03}, std::byte{0x04},
                                 std::byte{0x05}, std::byte{0x06}};
    REQUIRE(itch::read_be48(buf.data()) == 0x010203040506);
}

TEST_CASE("read_be48 max value leaves the top 16 bits zero") {
    std::array<std::byte, 6> buf{std::byte{0xFF}, std::byte{0xFF},
                                 std::byte{0xFF}, std::byte{0xFF},
                                 std::byte{0xFF}, std::byte{0xFF}};
    REQUIRE(itch::read_be48(buf.data()) == 0x0000FFFFFFFFFFFF);
}
