# ITCH 5.0 Feed Handler & Order Book

A from-scratch C++20 market data feed handler and full-depth L3 order book, built against Nasdaq TotalView-ITCH 5.0. It decodes the raw binary feed, reconstructs exact exchange book state order-by-order, and then makes that reconstruction fast — with a benchmark harness and optimization passes.

## Build this project

Requirements: a C++20 compiler, CMake 3.25+, and ~15 GB free disk. Catch2 v3 is
fetched automatically at configure time.

### Download and pre process data

Run from the project root. Raw file should be 8,251,407,909 bytes. Drop `-k` if you don't want to keep the 3.5 GB `.gz` alongside the 8.25 GB raw file.

```bash
mkdir -p data && cd data
curl -O "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz"
gunzip -k 12302019.NASDAQ_ITCH50.gz
head -c 2000000000 12302019.NASDAQ_ITCH50 > 12302019.NASDAQ_ITCH50_slice_2g.itch
head -c 500000000 12302019.NASDAQ_ITCH50 > 12302019.NASDAQ_ITCH50_slice_500m.itch
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

## Benchmarks

### Initial framing loop results

Reads the 2-byte big-endian length prefix, reads the type byte, counts it, strides to the next frame.

| | |
|---|---|
| Machine | MacBook Air (M1, 2020), 8 GB RAM |
| OS | macOS 26.5.1 (25F80) |
| Compiler | Apple Clang 17.0.0, `-O2` via `CMAKE_BUILD_TYPE=Release` |
| Input | `12302019.NASDAQ_ITCH50`, 8,251,407,909 bytes |
| Date | 2026-08-23 |

Shell `time` around the whole process, wall-clock only — rough local signal, not a benchmark. Distributions, pinning, and subtracted timer overhead come with the harness in step 4, on x86 Linux.

**Full file** — `sudo purge` before the cold run:

| run | user | sys | total | cpu |
|---|---|---|---|---|
| cold | 1.18s | 3.23s | 33.2s | 13% |
| warm 1 | 1.15s | 3.36s | 31.8s | 14% |
| warm 2 | 1.10s | 3.42s | 32.1s | 14% |
| warm 3 | 1.07s | 3.77s | 31.9s | 15% |

**2 GB slice** — smaller than RAM, still no warming:

| run | user | sys | total | cpu |
|---|---|---|---|---|
| cold | 0.28s | 0.93s | 10.4s | 11% |
| warm 1 | 0.27s | 0.99s | 10.4s | 12% |
| warm 2 | 0.29s | 0.94s | 10.2s | 11% |
| warm 3 | 0.27s | 1.04s | 10.7s | 12% |

**500 MB slice** — warming visible:

| run | real | user | sys |
|---|---|---|---|
| cold | 2.82s | 0.06s | 0.21s |
| warm 1 | 0.10s | 0.05s | 0.03s |
| warm 2 | 0.08s | 0.05s | 0.03s |

Slice runs end in `bad frame at offset ...` — the truncation guard firing on a mid-message cut, as designed. Timing is unaffected.

**Findings:**

1. The parse loop costs ~1.1s CPU for the full day: 8.25 GB / 1.07s ≈ **7.7 GB/s**, ~250M messages/s.
2. Wall time is I/O — 13–15% CPU means the process is asleep on the SSD ~85% of the time. Full-file timing on this machine measures the SSD.
3. The full file (8.25 GB) is larger than RAM (8 GB) so it can never be cached. Warming needs the file to fit in *available* page cache. The 2 GB slice fits in RAM but still shows no warming — `vm_stat` reported ~58 MB free at measurement time. At 500 MB the second run is **28× faster** and collapses to `real ≈ user + sys`.

### Message counts — 12/30/2019

268,744,780 messages. Multiplying each count by its type's fixed frame size (message size + 2-byte length prefix) sums to exactly 8,251,407,909 bytes — the file size.

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

Adds (`A`+`F` = 118.6M) slightly exceed deletes (`D` 114.4M) — most orders cancel unfilled. Executions are an order of magnitude rarer. Per-symbol types agree with each other (`R` 8,906, `H` 8,966, `Y` 9,013). No `W` — no circuit breaker tripped that day.

