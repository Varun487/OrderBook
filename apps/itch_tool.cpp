#include <iostream>
#include <iomanip>
#include <cstdint>
#include <array>

#include "itch.hpp"
#include "mmap.hpp"

int main(int argc, char** argv) {
    // Handle command line arguments
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " path/to/<itch-file>" << '\n';
        return 1;
    }

    // Version
    std::cout << "itch_tool version: " << itch::version() << '\n';

    // Mapped file object
    io::MappedFile file(argv[1]);

    // File data
    const auto data = file.bytes();

    std::cout << "Mapped file " << argv[1] << " of size " << data.size() << '\n';

    // Keep count of different message types
    std::array<std::uint64_t, 256> counts{};

    // Keep count of invalid messages per type
    std::array<std::uint64_t, 256> invalid{};

    // Print only the first 20 invalid messages
    std::size_t reports = 0;
    std::size_t unknown_reports = 0;
    constexpr std::size_t kMaxReports = 20;

    // Check Timestamps (in nanoseconds)
    std::uint64_t prev_ts = 0;
    std::uint64_t ts_violations = 0;

    // Main parse
    std::size_t i = 0;
    while (i + 2 <= data.size()) {
        // Need to convert from big endian to a little endian as this is speread over 2 bytes
        const std::uint16_t len = itch::read_be<std::uint16_t>(data.data() + i);

        // Ensures that the frame length is not out of the file's bounds
        if (len == 0 || i + 2 + len > data.size()) {
            std::cerr << "bad frame at offset " << i << ": len " << len << '\n';
            return 1;
        }

        // No need to convert from big endian to little endian as this is a single byte.
        const auto type = std::to_integer<std::uint8_t>(data[i + 2]);

        // Ensure message type and length match
        const std::size_t size = itch::expected_size(type);
        if (size == 0) {
            // Unknown type is not an error
            if (unknown_reports < kMaxReports) {
                std::cout << "unknown type '" << static_cast<char>(type)
                          << "' (0x" << std::hex << static_cast<int>(type) << std::dec
                          << ") at offset " << i << '\n';
                if (++unknown_reports == kMaxReports)
                    std::cout << "(further unknown types counted, not printed)\n";
            }
        }
        else if (len != size) {
            // Length not matching expexted type size is an error
            std::cerr << "expected size " << size << " for type '"
                      << static_cast<char>(type) << "' but saw length of " << len
                      << " at offset " << i << '\n';
            return 1;
        }
        else {
            // Timstamps being out of sync is not an error
            const std::uint64_t ts = itch::read_be48(data.data() + i + 2 + 5);
            if (ts < prev_ts) ts_violations++;
            prev_ts = ts;
        }

        // Lambda function to flag invalid messages
        const auto flag = [&](bool ok) {
            if (ok) return;
            invalid[type]++;
            if (reports < kMaxReports) {
                std::cerr << "invalid " << static_cast<char>(type)
                        << " at offset " << i << '\n';
                if (++reports == kMaxReports)
                    std::cerr << "(further invalid messages counted, not printed)\n";
            }
        };
        
        switch (type) {
            case 'A':{
                const itch::AddOrder A = itch::decode_add(data.data() + i + 2);
                flag(itch::valid_price(A.price) && 
                    itch::valid_shares(A.shares) && 
                    itch::valid_stock(A.stock) && 
                    itch::valid_side(A.side));
                break;
            }
            case 'F':{
                const itch::AddOrderMpid F = itch::decode_add_mpid(data.data() + i + 2);
                flag(itch::valid_price(F.base.price) && 
                    itch::valid_shares(F.base.shares) && 
                    itch::valid_stock(F.base.stock) && 
                    itch::valid_side(F.base.side));
                break;
            }
            case 'X':{
                const itch::CancelOrder X = itch::decode_cancel(data.data() + i + 2);
                flag(itch::valid_order_ref(X.order_ref) &&
                    itch::valid_shares(X.cancelled_shares));
                break;
            }
            case 'E':{
                const itch::ExecutedOrder E = itch::decode_execute(data.data() + i + 2);
                flag(itch::valid_order_ref(E.order_ref) &&
                    itch::valid_shares(E.executed_shares) &&
                    E.match_number != 0);
                break;
            }
            case 'D':{
                const itch::DeleteOrder D = itch::decode_delete(data.data() + i + 2);
                flag(itch::valid_order_ref(D.order_ref));
                break;
            }
            case 'C':{
                const itch::ExecutedWithPriceOrder C = itch::decode_execute_with_price(data.data() + i + 2);
                flag(itch::valid_order_ref(C.order_ref) &&
                    itch::valid_shares(C.executed_shares) &&
                    C.match_number != 0 &&
                    itch::valid_printable(C.printable) &&
                    itch::valid_price(C.execution_price));
                break;
            }
            case 'U':{
                const itch::ReplaceOrder U = itch::decode_replace(data.data() + i + 2);
                // orig != new is the only field-level guard against reading the
                // two refs at swapped offsets. It looks trivial; keep it.
                flag(itch::valid_order_ref(U.orig_order_ref) &&
                    itch::valid_order_ref(U.new_order_ref) &&
                    U.orig_order_ref != U.new_order_ref &&
                    itch::valid_shares(U.shares) &&
                    itch::valid_price(U.price));
                break;
            }
            default:
                break;
        }

        counts[type]++;

        i += 2 + len;
    }

    // Final check post loop
    if (i != data.size()) {
        std::cerr << "truncated tail: stopped at offset " << i << " of " << data.size() << '\n';
        return 1;
    }

    // Print counts of messages
    std::uint64_t total_msgs = 0;
    std::uint64_t total_invalid = 0;
    std::cout << '\n'
              << std::setw(4) << "type" << std::setw(6) << "dec"
              << std::setw(14) << "count" << std::setw(10) << "invalid" << '\n';
    for (int c = 0; c < 256; c++) {
        if (counts[c] > 0) {
            std::cout << std::setw(4) << static_cast<char>(c)
                      << std::setw(6) << c
                      << std::setw(14) << counts[c]
                      << std::setw(10) << invalid[c] << '\n';
            total_msgs += counts[c];
            total_invalid += invalid[c];
        }
    }
    std::cout << '\n'
              << "total messages       : " << total_msgs << '\n'
              << "invalid messages     : " << total_invalid << '\n'
              << "timestamp regressions: " << ts_violations << '\n';

    // Any invalid messages fails at runtime with a non-zero exit
    return (total_invalid == 0 && ts_violations == 0) ? 0 : 1;

}
