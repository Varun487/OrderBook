#include <iostream>
#include <iomanip>
#include <cstdint>
#include <array>
#include <vector>
#include <string_view>
#include <unordered_map>

#include "itch.hpp"
#include "mmap.hpp"

int main(int argc, char** argv) {
    // Handle command line arguments
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " path/to/<itch-file>" << '\n';
        return 1;
    }

    // Version
    std::cout << "\n\n\n" << "itch_tool version: " << itch::version() << '\n';

    // Mapped file object
    io::MappedFile file(argv[1]);

    // File data
    const auto data = file.bytes();

    std::cout << "Mapped file " << argv[1] << " of size " << data.size() << '\n';

    // Count per-message-type statistics
    struct TypeStats {
        std::uint64_t count     = 0;
        std::uint64_t invalid   = 0;
        std::uint64_t ref_miss  = 0;
        std::uint64_t underflow = 0;
    };
    std::array<TypeStats, 256> stats{};

    // Print only the first 20 invalid messages
    std::size_t reports = 0;
    std::size_t unknown_reports = 0;
    constexpr std::size_t kMaxReports = 20;

    // Check Timestamps (in nanoseconds)
    std::uint64_t prev_ts = 0;
    std::uint64_t ts_violations = 0;

    // Symbol table
    std::vector<std::array<char, 8>> symbols;
    std::uint64_t symbol_dupes = 0;   // same locate, different symbol

    // Live Order reference map
    struct Order {
        std::uint16_t stock_locate;
        std::uint16_t tracking_number;
        std::uint64_t timestamp;
        std::uint64_t order_ref;
        char side;
        std::uint32_t shares;
        std::array<char, 8> stock;
        std::uint32_t price;
    };
    std::unordered_map<uint64_t, Order> order_ref_map;
    std::size_t peak_ref_size = 0;

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
            stats[type].invalid++;
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
                    itch::valid_order_ref(A.order_ref) &&
                    itch::valid_side(A.side));
                order_ref_map[A.order_ref] = Order {
                    .stock_locate = A.stock_locate,
                    .tracking_number = A.tracking_number,
                    .timestamp = A.timestamp,
                    .order_ref = A.order_ref,
                    .side = A.side,
                    .shares = A.shares,
                    .stock = A.stock,
                    .price = A.price
                };
                if (order_ref_map.size() > peak_ref_size) peak_ref_size = order_ref_map.size();
                break;
            }
            case 'F':{
                const itch::AddOrderMpid F = itch::decode_add_mpid(data.data() + i + 2);
                flag(itch::valid_price(F.base.price) && 
                    itch::valid_shares(F.base.shares) && 
                    itch::valid_stock(F.base.stock) && 
                    itch::valid_order_ref(F.base.order_ref) &&
                    itch::valid_side(F.base.side));
                order_ref_map[F.base.order_ref] = Order {
                    .stock_locate = F.base.stock_locate,
                    .tracking_number = F.base.tracking_number,
                    .timestamp = F.base.timestamp,
                    .order_ref = F.base.order_ref,
                    .side = F.base.side,
                    .shares = F.base.shares,
                    .stock = F.base.stock,
                    .price = F.base.price
                };
                if (order_ref_map.size() > peak_ref_size) peak_ref_size = order_ref_map.size();
                break;
            }
            case 'X':{
                const itch::CancelOrder X = itch::decode_cancel(data.data() + i + 2);
                flag(itch::valid_order_ref(X.order_ref) &&
                    itch::valid_shares(X.cancelled_shares));
                if (order_ref_map.find(X.order_ref) == order_ref_map.end()) {
                    stats['X'].ref_miss++;
                    break;
                }
                if (order_ref_map[X.order_ref].shares >= X.cancelled_shares) {
                    order_ref_map[X.order_ref].shares -= X.cancelled_shares;
                } else {
                    stats['X'].underflow++;
                }
                break;
            }
            case 'E':{
                const itch::ExecutedOrder E = itch::decode_execute(data.data() + i + 2);
                flag(itch::valid_order_ref(E.order_ref) &&
                    itch::valid_shares(E.executed_shares) &&
                    E.match_number != 0);
                if (order_ref_map.find(E.order_ref) == order_ref_map.end()) {
                    stats['E'].ref_miss++;
                    break;
                }
                if (order_ref_map[E.order_ref].shares < E.executed_shares) {
                    stats['E'].underflow++;
                    break;
                }
                order_ref_map[E.order_ref].shares -= E.executed_shares;

                if (order_ref_map[E.order_ref].shares == 0) {
                    order_ref_map.erase(E.order_ref);
                }
                break;
            }
            case 'D':{
                const itch::DeleteOrder D = itch::decode_delete(data.data() + i + 2);
                flag(itch::valid_order_ref(D.order_ref));
                if (order_ref_map.erase(D.order_ref) == 0) stats['D'].ref_miss++;
                break;
            }
            case 'C':{
                const itch::ExecutedWithPriceOrder C = itch::decode_execute_with_price(data.data() + i + 2);
                flag(itch::valid_order_ref(C.order_ref) &&
                    itch::valid_shares(C.executed_shares) &&
                    C.match_number != 0 &&
                    itch::valid_printable(C.printable) &&
                    itch::valid_price(C.execution_price));
                if (order_ref_map.find(C.order_ref) == order_ref_map.end()) {
                    stats['C'].ref_miss++;
                    break;
                }
                if (order_ref_map[C.order_ref].shares < C.executed_shares) {
                    stats['C'].underflow++;
                    break;
                }
                order_ref_map[C.order_ref].shares -= C.executed_shares;

                if (order_ref_map[C.order_ref].shares == 0) {
                    order_ref_map.erase(C.order_ref);
                }
                break;
            }
            case 'U':{
                const itch::ReplaceOrder U = itch::decode_replace(data.data() + i + 2);
                flag(itch::valid_order_ref(U.orig_order_ref) &&
                    itch::valid_order_ref(U.new_order_ref) &&
                    U.orig_order_ref != U.new_order_ref &&
                    itch::valid_shares(U.shares) &&
                    itch::valid_price(U.price));
                if (order_ref_map.find(U.orig_order_ref) == order_ref_map.end()) {
                    stats['U'].ref_miss++;
                    break;
                }
                order_ref_map[U.new_order_ref] = Order {
                    .stock_locate = U.stock_locate,
                    .tracking_number = U.tracking_number,
                    .timestamp = U.timestamp,
                    .order_ref = U.new_order_ref,
                    .side = order_ref_map[U.orig_order_ref].side,
                    .shares = U.shares,
                    .stock = order_ref_map[U.orig_order_ref].stock,
                    .price = U.price
                };
                order_ref_map.erase(U.orig_order_ref);
                break;
            }
            case 'R': {
                const itch::StockDirectory R = itch::decode_stock_directory(data.data() + i + 2);
                flag(itch::valid_stock(R.stock) && itch::valid_stock_directory(R));

                // Build the symbol table
                if (R.stock_locate >= symbols.size()) {
                    symbols.resize(R.stock_locate + 1);
                }
                // Check if symbol already exists, record if dupe
                const auto& prev = symbols[R.stock_locate];
                if(prev[0] != '\0' && prev != R.stock) {
                    symbol_dupes++;
                    if (reports < kMaxReports) {
                        std::cerr << "locate " << R.stock_locate << " reassigned: \""                                                                                                            
                                << std::string_view(prev.data(), prev.size()) << "\" -> \""
                                << std::string_view(R.stock.data(), R.stock.size())
                                << "\" at offset " << i << '\n';
                        ++reports;
                    }
                }
                // Add symbol to the table
                symbols[R.stock_locate] = R.stock;

                break;
            }
            default:
                break;
        }

        stats[type].count++;

        i += 2 + len;
    }

    // Final check post loop
    if (i != data.size()) {
        std::cerr << "truncated tail: stopped at offset " << i << " of " << data.size() << '\n';
        return 1;
    }

    // Print counts table of messages
    TypeStats totals;
    std::cout << '\n'
            << std::setw(4)  << "type"     << std::setw(6)  << "dec"
            << std::setw(14) << "count"    << std::setw(10) << "invalid"
            << std::setw(12) << "ref miss" << std::setw(12) << "underflow" << '\n';
    for (int c = 0; c < 256; c++) {
        const TypeStats& s = stats[c];
        if (s.count == 0) continue;   // this type never occurred

        std::cout << std::setw(4)  << static_cast<char>(c)
                << std::setw(6)  << c
                << std::setw(14) << s.count
                << std::setw(10) << s.invalid
                << std::setw(12) << s.ref_miss
                << std::setw(12) << s.underflow << '\n';

        totals.count     += s.count;
        totals.invalid   += s.invalid;
        totals.ref_miss  += s.ref_miss;
        totals.underflow += s.underflow;
    }

    // Print message stats
    std::cout << '\n'
              << "total messages       : " << totals.count     << '\n'
              << "invalid messages     : " << totals.invalid   << '\n'
              << "order ref misses     : " << totals.ref_miss  << '\n'
              << "quantity underflows  : " << totals.underflow << '\n'
              << "timestamp regressions: " << ts_violations    << "\n\n\n";

    // Print symbol tables stats
    std::uint64_t symbols_written = 0;
    for (std::size_t s = 1; s < symbols.size(); ++s)
        if (symbols[s][0] != '\0') symbols_written++;

    const std::size_t max_locate = symbols.empty() ? 0 : symbols.size() - 1;

    std::cout << "max stock_locate     : " << max_locate << '\n'
            << "symbols written      : " << symbols_written << '\n'
            << "R messages           : " << stats['R'].count << '\n'
            << "locate gaps          : " << max_locate - symbols_written << '\n'
            << "locate reassignments : " << symbol_dupes << "\n\n\n";
    
    // Print peak live order ref size
    std::cout << "Peak live orders size: " << peak_ref_size << '\n'
            << "Orders live at the close: " << order_ref_map.size() << "\n\n\n";

    // Any invalid message, ref miss, underflow or symbol dupe fails at runtime
    // with a non-zero exit, so both humans and && chains see it.
    return (totals.invalid == 0 && totals.ref_miss == 0 && totals.underflow == 0 &&
            ts_violations == 0 && symbol_dupes == 0) ? 0 : 1;

}
