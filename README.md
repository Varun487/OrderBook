# ITCH 5.0 Feed Handler & Order Book

A from-scratch C++20 market data feed handler and full-depth L3 order book, built against Nasdaq TotalView-ITCH 5.0. It decodes the raw binary feed, reconstructs exact exchange book state order-by-order, and then makes that reconstruction fast — with a benchmark harness and optimization passes.


## Data

Sourcing ITCH 5.0 from Nasdaq: https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/

This project uses data from 12/30/2019: https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz

## Build this project

1. Run the commands below to setup the data folder and download the required data:
```bash
mkdir -p data && cd data
curl -O "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/12302019.NASDAQ_ITCH50.gz"
gunzip -k 12302019.NASDAQ_ITCH50.gz
```
2. Run this command in the root directory of the project to setup the `build` directory via cmake:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```
3. Compile the code:
```bash
cmake --build build
```
4. Run the code:
```bash
./build/itch_tool data/12302019.NASDAQ_ITCH50
```
5. Run the tests:
```bash
./build/tests
```
