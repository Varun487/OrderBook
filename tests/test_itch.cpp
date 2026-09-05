#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

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

// ---------------------------------------------------------------------------
// Decoder functions tests.
// ---------------------------------------------------------------------------

namespace {

constexpr std::byte operator""_b(unsigned long long v) {
    return static_cast<std::byte>(v);
}

// 'A' — Add Order (No MPID), 36 bytes. Spec section 4.3.1.
constexpr std::array<std::byte, 36> kAddFrame = {
    std::byte{'A'},                                 // +0  message type
    0x12_b, 0x34_b,                                 // +1  stock_locate    = 0x1234
    0x56_b, 0x78_b,                                 // +3  tracking_number = 0x5678
    0x1A_b, 0x2B_b, 0x3C_b, 0x4D_b, 0x5E_b, 0x6F_b, // +5  timestamp = 0x1A2B3C4D5E6F
    0x11_b, 0x22_b, 0x33_b, 0x44_b,                 // +11 order_ref
    0x55_b, 0x66_b, 0x77_b, 0x88_b,                 //     = 0x1122334455667788
    std::byte{'B'},                                 // +19 side = buy
    0x00_b, 0x00_b, 0x00_b, 0x64_b,                 // +20 shares = 100
    std::byte{'A'}, std::byte{'A'},                 // +24 stock = "AAPL    "
    std::byte{'P'}, std::byte{'L'},                 //     (space-padded to 8,
    std::byte{' '}, std::byte{' '},                 //      not null-terminated)
    std::byte{' '}, std::byte{' '},                 //
    0x00_b, 0x12_b, 0xD6_b, 0x44_b,                 // +32 price = 1234500 = $123.4500
};

// 'F' — Add Order with MPID, 40 bytes. Spec section 4.3.2.
// Deliberately different values from kAddFrame so a copy-paste in
// decode_add_mpid that ignores its own `p` shows up as a failure.
constexpr std::array<std::byte, 40> kAddMpidFrame = {
    std::byte{'F'},                                 // +0  message type
    0x00_b, 0x2A_b,                                 // +1  stock_locate    = 42
    0x00_b, 0x07_b,                                 // +3  tracking_number = 7
    0xAB_b, 0xCD_b, 0xEF_b, 0x01_b, 0x23_b, 0x45_b, // +5  timestamp = 0xABCDEF012345
    0x0F_b, 0x1E_b, 0x2D_b, 0x3C_b,                 // +11 order_ref
    0x4B_b, 0x5A_b, 0x69_b, 0x78_b,                 //     = 0x0F1E2D3C4B5A6978
    std::byte{'S'},                                 // +19 side = sell
    0x00_b, 0x00_b, 0x27_b, 0x10_b,                 // +20 shares = 10000
    std::byte{'M'}, std::byte{'S'},                 // +24 stock = "MSFT    "
    std::byte{'F'}, std::byte{'T'},                 //
    std::byte{' '}, std::byte{' '},                 //
    std::byte{' '}, std::byte{' '},                 //
    0x00_b, 0x0F_b, 0x42_b, 0x40_b,                 // +32 price = 1000000 = $100.0000
    std::byte{'N'}, std::byte{'S'},                 // +36 mpid = "NSDQ"
    std::byte{'D'}, std::byte{'Q'},                 //
};

std::string_view as_view(const std::array<char, 8>& a) {
    return std::string_view(a.data(), a.size());
}

std::string_view as_view(const std::array<char, 4>& a) {
    return std::string_view(a.data(), a.size());
}

} // namespace

// The driver hands `m.base` to the same helper as an 'A'. That only works while
// `base` sits at offset 0, so make the wrong code fail at compile time rather
// than trusting a comment.
static_assert(offsetof(itch::AddOrderMpid, base) == 0,
              "AddOrder must stay the first member of AddOrderMpid");

TEST_CASE("decode_add reads every field of an 'A' frame") {
    const itch::AddOrder m = itch::decode_add(kAddFrame.data());

    REQUIRE(m.stock_locate == 0x1234);
    REQUIRE(m.tracking_number == 0x5678);
    REQUIRE(m.timestamp == 0x1A2B3C4D5E6FULL);
    REQUIRE(m.order_ref == 0x1122334455667788ULL);
    REQUIRE(m.side == 'B');
    REQUIRE(m.shares == 100);
    REQUIRE(as_view(m.stock) == "AAPL    ");
    REQUIRE(m.price == 1234500);
}

TEST_CASE("decode_add leaves the symbol space-padded, not truncated") {
    const itch::AddOrder m = itch::decode_add(kAddFrame.data());

    // The wire field is fixed-width and space-padded. Nothing in the decoder
    // trims it, and nothing downstream may assume a terminator.
    REQUIRE(m.stock.size() == 8);
    REQUIRE(m.stock[3] == 'L');
    REQUIRE(m.stock[4] == ' ');
    REQUIRE(m.stock[7] == ' ');
}

TEST_CASE("decode_add_mpid reads the embedded 'A' fields") {
    const itch::AddOrderMpid m = itch::decode_add_mpid(kAddMpidFrame.data());

    REQUIRE(m.base.stock_locate == 42);
    REQUIRE(m.base.tracking_number == 7);
    REQUIRE(m.base.timestamp == 0xABCDEF012345ULL);
    REQUIRE(m.base.order_ref == 0x0F1E2D3C4B5A6978ULL);
    REQUIRE(m.base.side == 'S');
    REQUIRE(m.base.shares == 10000);
    REQUIRE(as_view(m.base.stock) == "MSFT    ");
    REQUIRE(m.base.price == 1000000);
}

TEST_CASE("decode_add_mpid reads the MPID at +36") {
    const itch::AddOrderMpid m = itch::decode_add_mpid(kAddMpidFrame.data());

    // Reading at +35 would pick up the low byte of price and spell "@NSD".
    REQUIRE(as_view(m.mpid) == "NSDQ");
}

TEST_CASE("decode_add and decode_add_mpid agree on the shared 36-byte prefix") {
    // 'F' is 'A' plus four trailing bytes, so decode_add over an 'F' frame is
    // well-defined. This is what pins the offset arithmetic to one place: if
    // decode_add_mpid ever stops delegating, these diverge.
    const itch::AddOrder direct = itch::decode_add(kAddMpidFrame.data());
    const itch::AddOrderMpid via_mpid = itch::decode_add_mpid(kAddMpidFrame.data());

    REQUIRE(direct.order_ref == via_mpid.base.order_ref);
    REQUIRE(direct.timestamp == via_mpid.base.timestamp);
    REQUIRE(direct.price == via_mpid.base.price);
    REQUIRE(as_view(direct.stock) == as_view(via_mpid.base.stock));
}

TEST_CASE("read_chars copies N bytes verbatim without swapping them") {
    // A character field is N independent chars, not one wide integer. If this
    // ever went through read_be the symbol would come back reversed.
    const auto stock = itch::read_chars<8>(kAddFrame.data() + 24);
    REQUIRE(as_view(stock) == "AAPL    ");

    const auto mpid = itch::read_chars<4>(kAddMpidFrame.data() + 36);
    REQUIRE(as_view(mpid) == "NSDQ");
}

// ---------------------------------------------------------------------------
// Five order-modification messages: D, X, E, C, U tests
// ---------------------------------------------------------------------------

namespace {

// 'D' — Order Delete, 19 bytes.
constexpr std::array<std::byte, 19> kDeleteFrame = {
    std::byte{'D'},                                 // +0  message type
    0x00_b, 0x63_b,                                 // +1  stock_locate    = 99
    0x00_b, 0x0B_b,                                 // +3  tracking_number = 11
    0x01_b, 0x23_b, 0x45_b, 0x67_b, 0x89_b, 0xAB_b, // +5  timestamp = 0x0123456789AB
    0xDE_b, 0xAD_b, 0xBE_b, 0xEF_b,                 // +11 order_ref
    0xCA_b, 0xFE_b, 0xBA_b, 0xBE_b,                 //     = 0xDEADBEEFCAFEBABE
};

// 'X' — Order Cancel, 23 bytes.
constexpr std::array<std::byte, 23> kCancelFrame = {
    std::byte{'X'},                                 // +0  message type
    0x01_b, 0x00_b,                                 // +1  stock_locate    = 256
    0x00_b, 0x02_b,                                 // +3  tracking_number = 2
    0x00_b, 0x00_b, 0x01_b, 0x00_b, 0x00_b, 0x00_b, // +5  timestamp = 16777216
    0x00_b, 0x00_b, 0x00_b, 0x00_b,                 // +11 order_ref = 1
    0x00_b, 0x00_b, 0x00_b, 0x01_b,                 //     (leading zeros on purpose)
    0x00_b, 0x00_b, 0x01_b, 0xF4_b,                 // +19 cancelled_shares = 500
};

// 'E' — Order Executed, 31 bytes.
constexpr std::array<std::byte, 31> kExecuteFrame = {
    std::byte{'E'},                                 // +0  message type
    0x0A_b, 0x0B_b,                                 // +1  stock_locate    = 0x0A0B
    0x0C_b, 0x0D_b,                                 // +3  tracking_number = 0x0C0D
    0x0E_b, 0x0F_b, 0x10_b, 0x11_b, 0x12_b, 0x13_b, // +5  timestamp = 0x0E0F10111213
    0x14_b, 0x15_b, 0x16_b, 0x17_b,                 // +11 order_ref
    0x18_b, 0x19_b, 0x1A_b, 0x1B_b,                 //     = 0x1415161718191A1B
    0x00_b, 0x00_b, 0x00_b, 0xC8_b,                 // +19 executed_shares = 200
    0x1C_b, 0x1D_b, 0x1E_b, 0x1F_b,                 // +23 match_number
    0x20_b, 0x21_b, 0x22_b, 0x23_b,                 //     = 0x1C1D1E1F20212223
};

// 'C' — Order Executed With Price, 36 bytes.
// The first 31 bytes are layout-identical to 'E' on purpose; the last test in
// this file leans on that.
constexpr std::array<std::byte, 36> kExecuteWithPriceFrame = {
    std::byte{'C'},                                 // +0  message type
    0x00_b, 0x2A_b,                                 // +1  stock_locate    = 42
    0x00_b, 0x01_b,                                 // +3  tracking_number = 1
    0xFF_b, 0xEE_b, 0xDD_b, 0xCC_b, 0xBB_b, 0xAA_b, // +5  timestamp = 0xFFEEDDCCBBAA
    0x01_b, 0x00_b, 0x00_b, 0x00_b,                 // +11 order_ref
    0x00_b, 0x00_b, 0x00_b, 0x00_b,                 //     = 0x0100000000000000
    0x00_b, 0x00_b, 0x00_b, 0x32_b,                 // +19 executed_shares = 50
    0x00_b, 0x00_b, 0x00_b, 0x00_b,                 // +23 match_number
    0x00_b, 0x00_b, 0x30_b, 0x39_b,                 //     = 12345
    std::byte{'N'},                                 // +31 printable = N
    0x00_b, 0x01_b, 0x86_b, 0xA0_b,                 // +32 execution_price = $10.0000
};

// 'U' — Order Replace, 35 bytes.
constexpr std::array<std::byte, 35> kReplaceFrame = {
    std::byte{'U'},                                 // +0  message type
    0x00_b, 0x07_b,                                 // +1  stock_locate    = 7
    0x00_b, 0x09_b,                                 // +3  tracking_number = 9
    0x00_b, 0x11_b, 0x22_b, 0x33_b, 0x44_b, 0x55_b, // +5  timestamp = 0x001122334455
    0x01_b, 0x02_b, 0x03_b, 0x04_b,                 // +11 orig_order_ref
    0x05_b, 0x06_b, 0x07_b, 0x08_b,                 //     = 0x0102030405060708
    0x11_b, 0x12_b, 0x13_b, 0x14_b,                 // +19 new_order_ref
    0x15_b, 0x16_b, 0x17_b, 0x18_b,                 //     = 0x1112131415161718
    0x00_b, 0x00_b, 0x03_b, 0xE8_b,                 // +27 shares = 1000
    0x00_b, 0x1E_b, 0x84_b, 0x80_b,                 // +31 price = $200.0000
};

} // namespace

TEST_CASE("decode_delete reads every field of a 'D' frame") {
    const itch::DeleteOrder m = itch::decode_delete(kDeleteFrame.data());

    REQUIRE(m.stock_locate == 99);
    REQUIRE(m.tracking_number == 11);
    REQUIRE(m.timestamp == 0x0123456789ABULL);
    REQUIRE(m.order_ref == 0xDEADBEEFCAFEBABEULL);
}

TEST_CASE("decode_cancel reads every field of an 'X' frame") {
    const itch::CancelOrder m = itch::decode_cancel(kCancelFrame.data());

    REQUIRE(m.stock_locate == 256);
    REQUIRE(m.tracking_number == 2);
    REQUIRE(m.timestamp == 16777216);
    REQUIRE(m.order_ref == 1);

    // A partial reduction. The decoder reports the cancelled quantity and
    // nothing else — whether the order survives is the book's call in step 2,
    // and the answer is always yes: the exchange sends a separate 'D'.
    REQUIRE(m.cancelled_shares == 500);
}

TEST_CASE("decode_execute reads every field of an 'E' frame") {
    const itch::ExecutedOrder m = itch::decode_execute(kExecuteFrame.data());

    REQUIRE(m.stock_locate == 0x0A0B);
    REQUIRE(m.tracking_number == 0x0C0D);
    REQUIRE(m.timestamp == 0x0E0F10111213ULL);
    REQUIRE(m.order_ref == 0x1415161718191A1BULL);
    REQUIRE(m.executed_shares == 200);
    REQUIRE(m.match_number == 0x1C1D1E1F20212223ULL);
}

TEST_CASE("decode_execute_with_price reads every field of a 'C' frame") {
    const itch::ExecutedWithPriceOrder m =
        itch::decode_execute_with_price(kExecuteWithPriceFrame.data());

    REQUIRE(m.stock_locate == 42);
    REQUIRE(m.tracking_number == 1);
    REQUIRE(m.timestamp == 0xFFEEDDCCBBAAULL);
    REQUIRE(m.order_ref == 0x0100000000000000ULL);
    REQUIRE(m.executed_shares == 50);
    REQUIRE(m.match_number == 12345);
    REQUIRE(m.printable == 'N');
    REQUIRE(m.execution_price == 100000);
}

TEST_CASE("decode_execute_with_price treats printable as data, not a filter") {
    const itch::ExecutedWithPriceOrder m =
        itch::decode_execute_with_price(kExecuteWithPriceFrame.data());

    // 'N' means "do not report in volume/stats". It does NOT mean "skip this
    // message" — the execution still hits the book. The decoder must hand the
    // flag through untouched rather than acting on it.
    REQUIRE(m.printable == 'N');
    REQUIRE(m.executed_shares == 50);
}

TEST_CASE("decode_replace keeps the original and new order refs distinct") {
    const itch::ReplaceOrder m = itch::decode_replace(kReplaceFrame.data());

    REQUIRE(m.stock_locate == 7);
    REQUIRE(m.tracking_number == 9);
    REQUIRE(m.timestamp == 0x001122334455ULL);

    // The trap. Swapping these two leaves the book plausible for millions of
    // messages before it visibly diverges, and no field ever looks nonsensical.
    // orig is at +11, new is at +19.
    REQUIRE(m.orig_order_ref == 0x0102030405060708ULL);
    REQUIRE(m.new_order_ref == 0x1112131415161718ULL);
    REQUIRE(m.orig_order_ref != m.new_order_ref);

    REQUIRE(m.shares == 1000);
    REQUIRE(m.price == 2000000);
}

TEST_CASE("'C' and 'E' decoders agree on their shared 31-byte prefix") {
    // ExecutedWithPriceOrder does not embed ExecutedOrder, so the offsets +1
    // through +23 are spelled out twice in itch.cpp. Nothing but this test
    // stops the two copies from drifting apart. 'C' is byte-identical to 'E'
    // for its first 31 bytes, so decoding one frame both ways must agree.
    const itch::ExecutedOrder as_e =
        itch::decode_execute(kExecuteWithPriceFrame.data());
    const itch::ExecutedWithPriceOrder as_c =
        itch::decode_execute_with_price(kExecuteWithPriceFrame.data());

    REQUIRE(as_e.stock_locate == as_c.stock_locate);
    REQUIRE(as_e.tracking_number == as_c.tracking_number);
    REQUIRE(as_e.timestamp == as_c.timestamp);
    REQUIRE(as_e.order_ref == as_c.order_ref);
    REQUIRE(as_e.executed_shares == as_c.executed_shares);
    REQUIRE(as_e.match_number == as_c.match_number);
}

// ---------------------------------------------------------------------------
// expected_size tests.
//
// This is the guard that made the rotated-switch bug loud, and it is what the
// driver trusts before handing a frame to a decoder. Restating the spec table
// verbatim would only prove the test was copied from the same place as the
// code, so most of what follows asserts the *relationships* the rest of the
// codebase actually depends on. expected_size is constexpr, so a wrong answer
// for a fixed type can fail at compile time rather than run time.
// ---------------------------------------------------------------------------

TEST_CASE("expected_size covers the seven decoded types") {
    // Each number is the last field's offset plus its width, read off the
    // decoder in itch.cpp — not copied out of the spec table a second time.
    STATIC_REQUIRE(itch::expected_size('A') == 36);  // price       at +32, 4 wide
    STATIC_REQUIRE(itch::expected_size('F') == 40);  // mpid        at +36, 4 wide
    STATIC_REQUIRE(itch::expected_size('D') == 19);  // order_ref   at +11, 8 wide
    STATIC_REQUIRE(itch::expected_size('X') == 23);  // cancelled   at +19, 4 wide
    STATIC_REQUIRE(itch::expected_size('E') == 31);  // match_num   at +23, 8 wide
    STATIC_REQUIRE(itch::expected_size('C') == 36);  // exec_price  at +32, 4 wide
    STATIC_REQUIRE(itch::expected_size('U') == 35);  // price       at +31, 4 wide
}

TEST_CASE("expected_size agrees with the decoders that share a prefix") {
    // decode_add_mpid delegates to decode_add and then reads the MPID at +36.
    // That read is only in bounds if 'F' is exactly 'A' plus the 4-byte MPID.
    STATIC_REQUIRE(itch::expected_size('F') == itch::expected_size('A') + 4);

    // 'C' is 'E' plus printable (1) and execution_price (4). Same invariant the
    // "'C' and 'E' decoders agree" test above checks from the other side.
    STATIC_REQUIRE(itch::expected_size('C') == itch::expected_size('E') + 5);
}

TEST_CASE("every known type is long enough to hold the common header") {
    // The driver reads the timestamp at +5..+10 off *every* type it recognises,
    // including the ones no decoder touches (P, Q, R, I...). A known type
    // shorter than 11 bytes would make that read run past the frame. Checked at
    // run time rather than with STATIC_REQUIRE so a failure names the type.
    for (int t = 0; t < 256; ++t) {
        const std::size_t size = itch::expected_size(static_cast<std::uint8_t>(t));
        if (size == 0) continue;  // unknown type; the driver skips it
        INFO("type '" << static_cast<char>(t) << "' (" << t << ")");
        CHECK(size >= 11);
    }
}

TEST_CASE("expected_size returns 0 for types the spec does not define") {
    // 0 is the driver's "unknown, skip it" signal, not an error. Anything that
    // is not a real ITCH type must land here rather than on a plausible size
    // that would let a corrupt frame pass the length check.
    STATIC_REQUIRE(itch::expected_size('G') == 0);
    STATIC_REQUIRE(itch::expected_size('Z') == 0);
    STATIC_REQUIRE(itch::expected_size(0x00) == 0);
    STATIC_REQUIRE(itch::expected_size(0xFF) == 0);

    // Lowercase is not a synonym for uppercase here: 'h' is Operational Halt
    // and 'H' is Stock Trading Action — two different messages of two different
    // sizes. 'a' is nothing at all.
    STATIC_REQUIRE(itch::expected_size('a') == 0);
    STATIC_REQUIRE(itch::expected_size('h') != itch::expected_size('H'));
}
