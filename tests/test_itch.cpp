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
// 'R' — Stock Directory.
//
// R is not a book message: it establishes stock_locate -> symbol once, before
// the session opens. Its value as a test is that bytes +19..+38 are almost all
// single-character enums, so a one-byte offset slip garbles several fields at
// once and the frame below catches it.
// ---------------------------------------------------------------------------

namespace {

// This is a *decoder* fixture, not a validator fixture. The ten single-byte
// enum fields carry ten pairwise-distinct values so a swapped pair of offsets
// fails two assertions instead of none. Where the spec's legal set was already
// spent by an earlier field, the byte here is a deliberate sentinel ('#', '@')
// rather than a legal value: decode_stock_directory does no validation, so
// reusing a legal 'Y' or 'N' would only weaken the test.
constexpr std::array<std::byte, 39> kStockDirectoryFrame = {
    std::byte{'R'},                                 // +0  message type
    0x22_b, 0xCA_b,                                 // +1  stock_locate    = 8906
    0x00_b, 0x05_b,                                 // +3  tracking_number = 5
    0x0A_b, 0x1B_b, 0x2C_b, 0x3D_b, 0x4E_b, 0x5F_b, // +5  timestamp = 0x0A1B2C3D4E5F
    std::byte{'T'}, std::byte{'S'},                 // +11 stock = "TSLA    "
    std::byte{'L'}, std::byte{'A'},                 //     (space-padded to 8)
    std::byte{' '}, std::byte{' '},                 //
    std::byte{' '}, std::byte{' '},                 //
    std::byte{'Q'},                                 // +19 market_category
    std::byte{'D'},                                 // +20 financial_status_indicator
    0x00_b, 0x00_b, 0x00_b, 0x64_b,                 // +21 round_lot_size = 100
    std::byte{'Y'},                                 // +25 round_lots_only
    std::byte{'B'},                                 // +26 issue_classification
    std::byte{'E'}, std::byte{'U'},                 // +27 issue_sub_type = "EU"
    std::byte{'T'},                                 // +29 authenticity
    std::byte{'N'},                                 // +30 short_sale_threshold
    std::byte{'Z'},                                 // +31 ipo_flag
    std::byte{'2'},                                 // +32 luld_reference_price_tier
    std::byte{'#'},                                 // +33 etp_flag           (sentinel)
    0x00_b, 0x00_b, 0x00_b, 0x03_b,                 // +34 etp_leverage_factor = 3
    std::byte{'@'},                                 // +38 inverse_indicator  (sentinel)
};

std::string_view as_view(const std::array<char, 2>& a) {
    return std::string_view(a.data(), a.size());
}

} // namespace

// Ties the fixture to the code's own size table rather than to the comment on
// the line above it. Growing the struct without growing expected_size, or the
// reverse, stops the build here.
static_assert(kStockDirectoryFrame.size() == itch::expected_size('R'),
              "the 'R' test frame must be exactly one wire frame long");

TEST_CASE("decode_stock_directory reads every field of an 'R' frame") {
    const itch::StockDirectory m =
        itch::decode_stock_directory(kStockDirectoryFrame.data());

    REQUIRE(m.stock_locate == 8906);
    REQUIRE(m.tracking_number == 5);
    REQUIRE(m.timestamp == 0x0A1B2C3D4E5FULL);
    REQUIRE(as_view(m.stock) == "TSLA    ");
    REQUIRE(m.market_category == 'Q');
    REQUIRE(m.financial_status_indicator == 'D');
    REQUIRE(m.round_lot_size == 100);
    REQUIRE(m.round_lots_only == 'Y');
    REQUIRE(m.issue_classification == 'B');
    REQUIRE(as_view(m.issue_sub_type) == "EU");
    REQUIRE(m.authenticity == 'T');
    REQUIRE(m.short_sale_threshold == 'N');
    REQUIRE(m.ipo_flag == 'Z');
    REQUIRE(m.luld_reference_price_tier == '2');
    REQUIRE(m.etp_flag == '#');
    REQUIRE(m.etp_leverage_factor == 3);
    REQUIRE(m.inverse_indicator == '@');
}

TEST_CASE("'R' single-byte fields are pairwise distinct in the fixture") {
    // The property the fixture depends on. If someone edits a value above and
    // collides two fields, the offset test silently weakens; this fails first.
    const itch::StockDirectory m =
        itch::decode_stock_directory(kStockDirectoryFrame.data());

    const std::array<char, 10> flags = {
        m.market_category, m.financial_status_indicator, m.round_lots_only,
        m.issue_classification, m.authenticity, m.short_sale_threshold,
        m.ipo_flag, m.luld_reference_price_tier, m.etp_flag, m.inverse_indicator
    };

    for (std::size_t a = 0; a < flags.size(); ++a)
        for (std::size_t b = a + 1; b < flags.size(); ++b) {
            INFO("fields " << a << " and " << b << " both hold '" << flags[a] << "'");
            CHECK(flags[a] != flags[b]);
        }
}

TEST_CASE("LULD reference price tier is a character, not an integer") {
    const itch::StockDirectory m =
        itch::decode_stock_directory(kStockDirectoryFrame.data());

    // The spec types this field Alpha and gives its values as 1 and 2, but the
    // bytes on the wire are '1' (0x31) and '2' (0x32). Comparing against the
    // integer 2 compiles clean, warns about nothing, and is false for every
    // message in the file. Same shape as `side`, which is 'B'/'S' and not 0/1.
    REQUIRE(m.luld_reference_price_tier == '2');
    REQUIRE(m.luld_reference_price_tier != 2);
    REQUIRE(m.luld_reference_price_tier == 0x32);
}

TEST_CASE("issue_sub_type is two characters, not a big-endian u16") {
    const itch::StockDirectory m =
        itch::decode_stock_directory(kStockDirectoryFrame.data());

    // Routing this field through read_be<uint16_t> would compile and would
    // spell "UE". It is two independent codes from Appendix E, so it has to go
    // through read_chars like `stock` and `mpid` do.
    REQUIRE(m.issue_sub_type[0] == 'E');
    REQUIRE(m.issue_sub_type[1] == 'U');
    REQUIRE(as_view(m.issue_sub_type) != "UE");
}

TEST_CASE("the two 'R' integers are read at their own offsets") {
    const itch::StockDirectory m =
        itch::decode_stock_directory(kStockDirectoryFrame.data());

    // round_lot_size at +21 sits between two single-char enums, and
    // etp_leverage_factor at +34 is followed by one. Reading either a byte
    // early or a byte late pulls a character into the low or high byte and the
    // value stops being small.
    REQUIRE(m.round_lot_size == 100);
    REQUIRE(m.etp_leverage_factor == 3);
}

// ---------------------------------------------------------------------------
// 'R' field validation.
//
// kStockDirectoryFrame above is deliberately not spec-legal — it trades legal
// values for pairwise-distinct ones so it can catch offset drift. Validation
// needs the opposite fixture: a record that is legal in every field, so a test
// can make exactly one thing wrong at a time.
// ---------------------------------------------------------------------------

namespace {

// Every field spec-legal. Modelled on a plain Nasdaq-listed common stock.
constexpr std::array<std::byte, 39> kLegalDirectoryFrame = {
    std::byte{'R'},                                 // +0  message type
    0x00_b, 0x01_b,                                 // +1  stock_locate    = 1
    0x00_b, 0x00_b,                                 // +3  tracking_number = 0
    0x00_b, 0x00_b, 0x00_b, 0x00_b, 0x00_b, 0x01_b, // +5  timestamp = 1
    std::byte{'A'}, std::byte{'A'},                 // +11 stock = "AAPL    "
    std::byte{'P'}, std::byte{'L'},                 //
    std::byte{' '}, std::byte{' '},                 //
    std::byte{' '}, std::byte{' '},                 //
    std::byte{'Q'},                                 // +19 Nasdaq Global Select
    std::byte{'N'},                                 // +20 Normal
    0x00_b, 0x00_b, 0x00_b, 0x64_b,                 // +21 round_lot_size = 100
    std::byte{'N'},                                 // +25 odd lots allowed
    std::byte{'C'},                                 // +26 Common Stock
    std::byte{' '}, std::byte{' '},                 // +27 issue_sub_type = blank
    std::byte{'P'},                                 // +29 Live/Production
    std::byte{'N'},                                 // +30 not restricted
    std::byte{'N'},                                 // +31 not a new IPO
    std::byte{'1'},                                 // +32 Tier 1  (character, not 1)
    std::byte{'N'},                                 // +33 not an ETP
    0x00_b, 0x00_b, 0x00_b, 0x00_b,                 // +34 etp_leverage_factor = 0
    std::byte{'N'},                                 // +38 not inverse
};

// The ten single-character enum fields, by wire offset. Poking one of these is
// how each test below makes exactly one thing wrong.
constexpr std::array<std::size_t, 10> kFlagOffsets = {
    19, 20, 25, 26, 29, 30, 31, 32, 33, 38
};

itch::StockDirectory decode_with(std::size_t offset, char value) {
    auto frame = kLegalDirectoryFrame;   // copy; the fixture stays constexpr
    frame[offset] = static_cast<std::byte>(value);
    return itch::decode_stock_directory(frame.data());
}

} // namespace

TEST_CASE("one_of matches membership, not ordering") {
    STATIC_REQUIRE(itch::one_of('Q', "QGSNAPMZV"));
    STATIC_REQUIRE(itch::one_of('V', "QGSNAPMZV"));   // last element
    STATIC_REQUIRE(!itch::one_of('B', "QGSNAPMZV"));
    STATIC_REQUIRE(!itch::one_of('q', "QGSNAPMZV"));  // case is significant

    // The empty set accepts nothing, including the space that every caller
    // tests for separately.
    STATIC_REQUIRE(!itch::one_of(' ', ""));

    // A space inside the set would work, but no caller spells it that way on
    // purpose — see the note in valid_stock_directory.
    STATIC_REQUIRE(itch::one_of(' ', " "));
    STATIC_REQUIRE(!itch::one_of(' ', "YN"));
}

TEST_CASE("one_of does not treat a high byte as a member") {
    // char is signed on both dev platforms. A byte >= 0x80 must not sign-extend
    // into something that matches, and must not be UB the way std::isupper
    // would be here.
    const char high = static_cast<char>(0x8A);
    REQUIRE(!itch::one_of(high, "QGSNAPMZV"));
    REQUIRE(!itch::one_of(high, ""));
}

TEST_CASE("valid_stock_directory accepts a fully legal record") {
    REQUIRE(itch::valid_stock_directory(
        itch::decode_stock_directory(kLegalDirectoryFrame.data())));
}

TEST_CASE("valid_stock_directory rejects a bad value in any flag field") {
    // '#' is in no allowed set and is not a space, so it is illegal for all ten
    // fields. A field the validator forgot to check would pass here and name
    // its own offset in the failure.
    for (const std::size_t off : kFlagOffsets) {
        INFO("offset +" << off);
        CHECK_FALSE(itch::valid_stock_directory(decode_with(off, '#')));
    }
}

TEST_CASE("valid_stock_directory allows <space> where the spec documents it") {
    // Market Category, Financial Status, Short Sale Threshold, IPO Flag, LULD
    // Tier and ETP Flag all list <space> as "not available" in section 1.2.1.
    for (const std::size_t off : {19u, 20u, 30u, 31u, 32u, 33u}) {
        INFO("offset +" << off);
        CHECK(itch::valid_stock_directory(decode_with(off, ' ')));
    }
}

TEST_CASE("valid_stock_directory rejects <space> where the spec documents none") {
    // Round Lots Only (+25), Issue Classification (+26) and Inverse Indicator
    // (+38) list no <space> value. These three allowed one for a while on the
    // reasoning that the spec defines them only for a subset of issues — but a
    // permission that never gets exercised is not knowledge, so they were
    // tightened to the documented sets and the full-day run is what settles it.
    //
    // If a run ever rejects records here, that is a finding rather than a bug:
    // put the space back for the field that needs it, and note which file
    // carried it.
    for (const std::size_t off : {25u, 26u, 38u}) {
        INFO("offset +" << off);
        CHECK_FALSE(itch::valid_stock_directory(decode_with(off, ' ')));
    }
}

TEST_CASE("valid_stock_directory requires a real authenticity value") {
    // The one flag field with no <space> escape. It is the only thing that
    // separates a live issue from a test issue, so a blank one is a record we
    // cannot interpret rather than a record with a field withheld.
    CHECK(itch::valid_stock_directory(decode_with(29, 'P')));
    CHECK(itch::valid_stock_directory(decode_with(29, 'T')));
    CHECK_FALSE(itch::valid_stock_directory(decode_with(29, ' ')));
}

TEST_CASE("valid_stock_directory reads the LULD tier as a character") {
    CHECK(itch::valid_stock_directory(decode_with(32, '1')));
    CHECK(itch::valid_stock_directory(decode_with(32, '2')));

    // The integers 1 and 2 are control bytes 0x01/0x02, not tiers.
    CHECK_FALSE(itch::valid_stock_directory(decode_with(32, static_cast<char>(1))));
    CHECK_FALSE(itch::valid_stock_directory(decode_with(32, static_cast<char>(2))));
}

TEST_CASE("valid_stock_directory knows the values a guess would miss") {
    // 'M' is NYSE Texas and 'Z' is BATS Z — both real Market Category values
    // that an from-memory list drops.
    CHECK(itch::valid_stock_directory(decode_with(19, 'M')));
    CHECK(itch::valid_stock_directory(decode_with(19, 'Z')));

    // IPO Flag has a third value: 'Z', a non-IPO new listing. Y/N/space is the
    // natural guess and would reject real records.
    CHECK(itch::valid_stock_directory(decode_with(31, 'Z')));

    // Financial Status 'C' was added to the spec after 5.0 shipped.
    CHECK(itch::valid_stock_directory(decode_with(20, 'C')));
}

TEST_CASE("valid_stock_directory ignores the fields it does not police") {
    // issue_sub_type is a ~40-code appendix, and neither integer has a
    // documented zero rule, so none of them are validated. Asserting that here
    // makes the omission a decision rather than an oversight.
    auto frame = kLegalDirectoryFrame;
    frame[27] = std::byte{'#'};   // issue_sub_type
    frame[28] = std::byte{'#'};
    frame[21] = std::byte{0xFF};  // round_lot_size, absurd but unpoliced
    frame[34] = std::byte{0xFF};  // etp_leverage_factor

    REQUIRE(itch::valid_stock_directory(itch::decode_stock_directory(frame.data())));
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
