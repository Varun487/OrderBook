# ITCH 5.0 Feed Handler & Order Book

A C++20 market data feed handler and full-depth L3 order book, built against Nasdaq TotalView-ITCH 5.0. It decodes the raw binary feed, reconstructs exact exchange book state order-by-order.

## Build this project

Requirements: a C++20 compiler, CMake 3.25+, and ~15 GB free disk.

### Download and pre process data

Run from the project root. Raw file should be 8,251,407,909 bytes. Drop `-k` if you don't want to keep the 3.5 GB `.gz` alongside the 8.25 GB raw file.

```bash
mkdir -p data && cd data
curl -O "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz"
gunzip -k 12302019.NASDAQ_ITCH50.gz
```

### Debug

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/itch_tool data/12302019.NASDAQ_ITCH50
./build/tests
```

### Release

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/itch_tool data/12302019.NASDAQ_ITCH50
./build-release/tests
```

## Initial Results

- Total messages on 30th Dec 2019: 268,744,780
- No invalid messages, timestamp regressions, length mismatches or unknown types

| Type | Meaning | Count |
|---|---|---:|
| `A` | Add Order | 117,145,568 |
| `D` | Order Delete | 114,360,997 |
| `U` | Order Replace | 21,639,067 |
| `E` | Order Executed | 5,722,824 |
| `I` | NOII (imbalance) | 4,024,315 |
| `X` | Order Cancel | 2,787,676 |
| `F` | Add Order w/ MPID | 1,485,888 |
| `P` | Trade (non-cross) | 1,218,602 |
| `L` | Market Participant Position | 215,161 |
| `C` | Order Executed w/ Price | 99,917 |
| `Q` | Cross Trade | 17,836 |
| `Y` | Reg SHO Restriction | 9,013 |
| `H` | Stock Trading Action | 8,966 |
| `R` | Stock Directory | 8,906 |
| `J` | LULD Auction Collar | 34 |
| `S` | System Event | 6 |
| `K` | IPO Quoting Period | 3 |
| `V` | MWCB Decline Level | 1 |
