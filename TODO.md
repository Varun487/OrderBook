1. Scaffolding C++ project - DONE
2. Parse - 
3. Naive order book implementation
4. Valiadte & test
5. Benchmark harness (need to run on Linux)
6. Optimization pass 1 — kill hot-path allocation (object pool for order nodes), measured before/after
7. Optimization pass 2 — flat price-level array, intrusive lists, cache-line struct layout, incremental BBO caching — each measured separately
8. Sequencing layer — gap detection, A/B arbitration, snapshot recovery, tested by artificially dropping messages
9. Stretch — multicast transport, SPSC-queued threading, CME MDP 3.0
