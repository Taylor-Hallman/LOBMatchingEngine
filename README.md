# Order Book Matching Engine

A C++23 limit order book matching engine, built in two parallel implementations — a
**naive** version using standard-library containers, and a hand-optimized,
low-latency version — with a shared test suite and a benchmark harness that
measures the real-world performance difference between them.

This project was built to explore the systems-level engineering that goes into
quant/HFT-style infrastructure: cache-conscious data structures, allocation-free
hot paths, and rigorous, reproducible performance measurement — not just "does it
match orders correctly," but "how fast, and why."

## Contents

- [Overview](#overview)
- [Architecture](#architecture)
  - [Naive implementation](#naive-implementation)
  - [Optimized implementation](#optimized-implementation)
- [Building](#building)
- [Testing](#testing)
- [Benchmarking](#benchmarking)
- [Results](#results)
- [Design decisions & tradeoffs](#design-decisions--tradeoffs)
- [Known limitations & future work](#known-limitations--future-work)

## Overview

Both implementations expose the same core interface:

- `placeOrder(Order&)` — submit a new limit order; matches against the resting
  book using price-time priority, and rests any unfilled remainder.
- `cancelOrder(uint64_t id)` — cancel a resting order by id.
- `getBestBuyPrice()` / `getBestSellPrice()` — top-of-book price queries.
- `getBidsAtPrice(price)` / `getAsksAtPrice(price)` — depth queries at a
  specific price level.
- `size()` — total resting order count.

Orders are modeled for a single equity-style instrument (a stock, not an
options contract), with integer-tick prices (no floating point) and standard
price-time priority matching: a resting order always trades at its own
displayed price, and orders at the same price fill in arrival order.

## Architecture

### Naive implementation

`namespace naive` — the baseline, built first, for correctness and as a
performance reference point.

- **Price levels:** `std::map<int64_t, std::deque<Order>>`, one for bids
  (`std::greater`) and one for asks (`std::less`).
- **Time priority:** FIFO via `std::deque` order.
- **Cancellation:** linear search/erase within a price level's deque — O(n)
  at that level.
- **Best price:** `std::map::begin()` — effectively free, since the tree
  maintains a cached leftmost/rightmost pointer incrementally on every
  insert/erase.

### Optimized implementation

`namespace optimized` — a low-latency redesign targeting the naive version's
two weak points: O(log n) price-level lookup and O(n) cancellation.

- **Price levels:** flat arrays (`m_bids`/`m_asks`), indexed directly by
  integer price tick, giving O(1) lookup with no hashing, no tree traversal,
  and better cache locality than a node-based container.
- **Order storage:** a custom pool allocator (`OrderPool`), backed by a
  `std::vector<std::variant<Order, size_t>>`. Free slots are threaded through
  a free list stored *inside* the unused slots themselves (the `size_t`
  alternative of the variant), so tracking free capacity costs no memory
  beyond the pool itself. The pool grows (doubling) when exhausted; all
  cross-references use pool **indices**, not raw pointers, so growth-driven
  reallocation never invalidates anything.
- **Time priority:** an intrusive doubly-linked list per price level — `next`/
  `prev` fields live directly on `Order`, threaded through pool storage, with
  each price level tracking its own `head`/`tail` for O(1) append and O(1)
  removal from anywhere in the chain (not just the ends).
- **Cancellation:** O(1) — an `unordered_map<id, poolIndex>` gives O(1)
  lookup, and the intrusive list gives O(1) unlink.
- **Best price:** a cached `m_bestBid`/`m_bestAsk`, updated in O(1) on
  insert. When the current best price level empties out, a bitmap of
  occupied price levels (one bit per tick, scanned via `std::countr_zero`/
  `countl_zero`) finds the next-best occupied level in a small, bounded
  number of word-scans — not a full linear scan of the price range.

This design is a deliberate memory-for-latency trade: both implementations'
price arrays and the optimized pool are sized for a bounded price range
(documented as ~$10,000 in integer cents, a reasonable ceiling for a typical
equity), at the cost of pre-allocating space for price levels that may never
be touched.

## Building

Requires CMake ≥ 3.25 and a C++23 compiler (developed against Clang 19).

```bash
mkdir build && cd build
cmake -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## Testing

Correctness is verified with a shared GoogleTest suite, run against **both**
implementations via `TYPED_TEST` — every test case executes once per
implementation, so a correctness bug in either one shows up as a distinct,
attributable failure.

Coverage includes: basic placement, exact/near/partial matches, multi-level
sweeps, price and time priority, best-price tracking after depletion,
cancellation (including mid-chain cancel, double-cancel, and cancel of a
nonexistent id), and post-cancel matching correctness.

```bash
cd build
ctest --output-on-failure
```

## Benchmarking

Performance is measured with Google Benchmark, using the same `BookType`
template pattern as the tests — every benchmark runs against both
implementations with identical, controlled inputs.

Each benchmark isolates one specific behavior (never a random mix of
behaviors) and is parameterized along the dimensions that plausibly affect
performance:

- **Book depth** — 100 to 1,000,000 resting orders.
- **Price concentration** — narrow range (heavy same-price collision, stresses
  chain-length) vs. wide range (sparse, stresses array/bitmap scanning).
- **Match rate** (`MixedTraffic` only) — 0–100% of incoming orders cross the
  book, letting the mix of pure-insert and pure-match cost be measured
  directly rather than blended by chance.
- **Fixed vs. varied quantity** — fixed quantities guarantee clean full
  fills, isolating match cost from partial-fill cost.

```bash
cd build
cmake --build . --target orderbook_benchmarks
./benchmarks/orderbook_benchmarks --benchmark_out=results.json --benchmark_out_format=json
```

For reliable numbers, pin the CPU governor before running (frequency scaling
otherwise adds real noise to nanosecond-scale measurements):

```bash
sudo cpupower frequency-set --governor performance
```

## Results

**Key — benchmark labels:**
| Label | Meaning |
|---|---|
| `narrow_range` | Order prices drawn from a tight band (e.g. $50.00–$150.00). Concentrates many orders on the same few price levels, stressing per-price-level chain length. |
| `wide_range` | Order prices drawn from (nearly) the full supported price range. Spreads orders thinly across many price levels, stressing array/bitmap sparsity instead of chain length. |
| `fixed_qty` | Every order uses the same fixed quantity, guaranteeing that every match is a clean full fill (no partial fills). Isolates match/removal cost from partial-fill cost. |
| `varied_qty` | Order quantity is randomized per order. Realistic, but means some matches fully fill and others partially fill, blending both costs into one measurement. |
| `depth` | Number of resting orders in the book at the time of measurement. |
| `match_pct` | (`MixedTraffic` only) Target percentage of incoming orders that cross and match against the book; the remainder rest as new orders. |

<details>
<summary><strong>Full benchmark results (click to expand)</strong></summary>
-------------------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                                                       Time             CPU   Iterations
-------------------------------------------------------------------------------------------------------------------------------------------------
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, false>/depth:100                                                66.8 ns         66.8 ns     10290262 narrow_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, false>/depth:1000                                               78.8 ns         78.7 ns      8704840 narrow_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, false>/depth:10000                                              64.4 ns         64.4 ns     11124721 narrow_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, false>/depth:100000                                             53.8 ns         53.8 ns     12845604 narrow_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, false>/depth:1000000                                            62.3 ns         62.2 ns     10242185 narrow_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, true>/depth:100                                                 64.8 ns         64.8 ns     10813363 wide_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, true>/depth:1000                                                85.6 ns         85.6 ns      8229246 wide_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, true>/depth:10000                                                151 ns          151 ns      4487341 wide_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, true>/depth:100000                                               477 ns          477 ns      1565836 wide_range
BM_PlaceOrder_NoMatch_Bids<naive::OrderBook, true>/depth:1000000                                              438 ns          438 ns      1543213 wide_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, false>/depth:100                                            16.6 ns         16.5 ns     40667987 narrow_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, false>/depth:1000                                           15.7 ns         15.7 ns     44510247 narrow_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, false>/depth:10000                                          18.8 ns         18.8 ns     37301065 narrow_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, false>/depth:100000                                         20.8 ns         20.8 ns     32544088 narrow_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, false>/depth:1000000                                        25.6 ns         25.6 ns     24929318 narrow_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, true>/depth:100                                             57.2 ns         57.1 ns     12481952 wide_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, true>/depth:1000                                            49.7 ns         49.7 ns     14110841 wide_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, true>/depth:10000                                           42.2 ns         42.2 ns     16457547 wide_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, true>/depth:100000                                          59.9 ns         59.9 ns     11455047 wide_range
BM_PlaceOrder_NoMatch_Bids<optimized::OrderBook, true>/depth:1000000                                          112 ns          112 ns      4738610 wide_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, false>/depth:100                                                63.1 ns         63.1 ns     11068523 narrow_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, false>/depth:1000                                               85.1 ns         85.1 ns      8292203 narrow_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, false>/depth:10000                                              64.3 ns         64.3 ns     11084404 narrow_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, false>/depth:100000                                             52.5 ns         52.5 ns     13340916 narrow_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, false>/depth:1000000                                            56.5 ns         56.5 ns     12350005 narrow_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, true>/depth:100                                                 64.1 ns         64.1 ns     11008753 wide_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, true>/depth:1000                                                85.1 ns         85.1 ns      7869502 wide_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, true>/depth:10000                                                153 ns          153 ns      4477941 wide_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, true>/depth:100000                                               428 ns          428 ns      1618522 wide_range
BM_PlaceOrder_NoMatch_Asks<naive::OrderBook, true>/depth:1000000                                              413 ns          413 ns      1497415 wide_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, false>/depth:100                                            16.3 ns         16.2 ns     41544561 narrow_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, false>/depth:1000                                           15.4 ns         15.4 ns     45550517 narrow_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, false>/depth:10000                                          18.4 ns         18.4 ns     37935216 narrow_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, false>/depth:100000                                         20.9 ns         20.9 ns     33436374 narrow_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, false>/depth:1000000                                        25.9 ns         25.9 ns     24627782 narrow_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, true>/depth:100                                             57.2 ns         57.1 ns     12366020 wide_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, true>/depth:1000                                            48.8 ns         48.8 ns     14542573 wide_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, true>/depth:10000                                           42.6 ns         42.6 ns     16388434 wide_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, true>/depth:100000                                          59.6 ns         59.6 ns     11572114 wide_range
BM_PlaceOrder_NoMatch_Asks<optimized::OrderBook, true>/depth:1000000                                          110 ns          110 ns      4637053 wide_range
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, false>/depth:100                                     37.8 ns         37.8 ns     18548824 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, false>/depth:1000                                    37.1 ns         37.1 ns     18796960 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, false>/depth:10000                                   29.4 ns         29.4 ns     23682267 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, false>/depth:100000                                  16.5 ns         16.5 ns     42794721 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, false>/depth:1000000                                 25.5 ns         25.5 ns     27654674 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, true>/depth:100                                      29.8 ns         29.8 ns     23196304 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, true>/depth:1000                                     26.9 ns         26.9 ns     25464862 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, true>/depth:10000                                    21.6 ns         21.6 ns     32355959 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, true>/depth:100000                                   10.1 ns         10.1 ns     65430974 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<naive::OrderBook, true>/depth:1000000                                  19.8 ns         19.8 ns     35405881 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, false>/depth:100                                 37.7 ns         37.6 ns     18667349 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, false>/depth:1000                                30.8 ns         30.8 ns     22561537 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, false>/depth:10000                               34.2 ns         34.2 ns     20617967 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, false>/depth:100000                              65.0 ns         64.9 ns     10678942 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, false>/depth:1000000                              239 ns          239 ns      3638060 varied_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, true>/depth:100                                  30.6 ns         30.6 ns     22936879 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, true>/depth:1000                                 22.7 ns         22.7 ns     30479383 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, true>/depth:10000                                27.3 ns         27.3 ns     26159400 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, true>/depth:100000                               56.9 ns         56.9 ns     11955814 fixed_qty
BM_PlaceOrder_AlwaysMatch_BidsResting<optimized::OrderBook, true>/depth:1000000                               219 ns          219 ns      3888766 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, false>/depth:100                                     38.2 ns         38.2 ns     18472999 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, false>/depth:1000                                    36.9 ns         36.9 ns     18872412 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, false>/depth:10000                                   29.8 ns         29.8 ns     23159317 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, false>/depth:100000                                  16.6 ns         16.6 ns     42433027 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, false>/depth:1000000                                 25.6 ns         25.6 ns     27296754 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, true>/depth:100                                      29.7 ns         29.6 ns     23781077 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, true>/depth:1000                                     28.5 ns         28.5 ns     24217934 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, true>/depth:10000                                    21.7 ns         21.7 ns     32261004 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, true>/depth:100000                                   9.73 ns         9.73 ns     71828170 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<naive::OrderBook, true>/depth:1000000                                  19.8 ns         19.8 ns     35326441 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, false>/depth:100                                 59.8 ns         59.8 ns     11759214 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, false>/depth:1000                                32.5 ns         32.5 ns     21290020 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, false>/depth:10000                               34.1 ns         34.1 ns     20714384 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, false>/depth:100000                              64.3 ns         64.3 ns     10586685 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, false>/depth:1000000                              238 ns          238 ns      3622372 varied_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, true>/depth:100                                  74.6 ns         74.6 ns      9385786 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, true>/depth:1000                                 29.1 ns         29.1 ns     24097778 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, true>/depth:10000                                27.5 ns         27.5 ns     25764793 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, true>/depth:100000                               61.5 ns         61.5 ns      9474135 fixed_qty
BM_PlaceOrder_AlwaysMatch_AsksResting<optimized::OrderBook, true>/depth:1000000                               225 ns          225 ns      3871340 fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100/match_pct:0                 61.4 ns         61.4 ns     11602009 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000/match_pct:0                79.8 ns         79.8 ns      8804128 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:10000/match_pct:0                102 ns          102 ns      6882759 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100000/match_pct:0               168 ns          168 ns      4211458 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000000/match_pct:0              175 ns          175 ns      3855858 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100/match_pct:30                48.7 ns         48.7 ns     14793732 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000/match_pct:30               59.4 ns         59.4 ns     11783089 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:10000/match_pct:30              73.0 ns         73.0 ns      9609930 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100000/match_pct:30              105 ns          105 ns      6573648 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000000/match_pct:30             105 ns          105 ns      6650352 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100/match_pct:70                27.1 ns         27.2 ns     25064439 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000/match_pct:70               31.5 ns         31.5 ns     22261152 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:10000/match_pct:70              35.4 ns         35.4 ns     19783636 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100000/match_pct:70             42.2 ns         42.2 ns     16514341 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000000/match_pct:70            41.1 ns         41.1 ns     16963411 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100/match_pct:100               13.2 ns         13.2 ns     52975555 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000/match_pct:100              11.6 ns         11.6 ns     60647644 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:10000/match_pct:100             10.8 ns         10.8 ns     65034654 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:100000/match_pct:100            10.4 ns         10.4 ns     67066935 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, false>/depth:1000000/match_pct:100           10.8 ns         10.8 ns     64955977 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100/match_pct:0                  60.4 ns         60.4 ns     11446118 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000/match_pct:0                 79.9 ns         79.9 ns      8760151 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:10000/match_pct:0                 101 ns          101 ns      6967022 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100000/match_pct:0                164 ns          164 ns      4257385 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000000/match_pct:0               175 ns          175 ns      4114770 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100/match_pct:30                 44.0 ns         44.0 ns     15562790 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000/match_pct:30                57.4 ns         57.4 ns     12211444 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:10000/match_pct:30               71.8 ns         71.5 ns      9870852 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100000/match_pct:30               105 ns          105 ns      6879392 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000000/match_pct:30              102 ns          102 ns      6862093 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100/match_pct:70                 22.5 ns         22.5 ns     31070038 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000/match_pct:70                27.2 ns         27.2 ns     25802322 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:10000/match_pct:70               31.1 ns         31.0 ns     22533296 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100000/match_pct:70              38.0 ns         38.0 ns     18462941 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000000/match_pct:70             36.3 ns         36.3 ns     19212348 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100/match_pct:100                5.74 ns         5.73 ns    121573889 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000/match_pct:100               4.48 ns         4.48 ns    156023754 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:10000/match_pct:100              4.24 ns         4.23 ns    165194809 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:100000/match_pct:100             4.24 ns         4.24 ns    164381126 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, false, true>/depth:1000000/match_pct:100            6.08 ns         6.08 ns    115845442 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100/match_pct:0                  61.3 ns         61.3 ns     11451113 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000/match_pct:0                 79.6 ns         79.6 ns      8803119 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:10000/match_pct:0                 103 ns          103 ns      6789526 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100000/match_pct:0                213 ns          213 ns      3281035 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000000/match_pct:0               609 ns          609 ns      1000000 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100/match_pct:30                 47.1 ns         47.1 ns     14870375 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000/match_pct:30                59.4 ns         59.4 ns     11787491 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:10000/match_pct:30               74.1 ns         74.1 ns      9446383 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100000/match_pct:30               137 ns          137 ns      5095107 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000000/match_pct:30              314 ns          314 ns      1889144 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100/match_pct:70                 27.6 ns         27.6 ns     25609234 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000/match_pct:70                31.6 ns         31.5 ns     22195384 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:10000/match_pct:70               36.0 ns         35.9 ns     19466791 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100000/match_pct:70              52.0 ns         52.0 ns     13617445 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000000/match_pct:70              106 ns          106 ns      6048993 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100/match_pct:100                13.0 ns         13.0 ns     54015935 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000/match_pct:100               11.6 ns         11.6 ns     60153908 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:10000/match_pct:100              10.9 ns         10.9 ns     65154345 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:100000/match_pct:100             10.5 ns         10.5 ns     66615098 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, false>/depth:1000000/match_pct:100            10.9 ns         10.9 ns     64233133 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100/match_pct:0                   59.1 ns         59.1 ns     11574969 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000/match_pct:0                  80.2 ns         80.1 ns      8747094 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:10000/match_pct:0                  103 ns          103 ns      6765214 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100000/match_pct:0                 221 ns          221 ns      3182994 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000000/match_pct:0                569 ns          569 ns      1000000 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100/match_pct:30                  45.4 ns         45.4 ns     15386501 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000/match_pct:30                 57.6 ns         57.6 ns     12138663 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:10000/match_pct:30                72.3 ns         72.3 ns      9673420 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100000/match_pct:30                136 ns          136 ns      5172226 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000000/match_pct:30               307 ns          307 ns      1886645 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100/match_pct:70                  23.1 ns         23.0 ns     30595827 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000/match_pct:70                 27.2 ns         27.2 ns     25745544 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:10000/match_pct:70                31.7 ns         31.7 ns     22117706 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100000/match_pct:70               47.0 ns         47.0 ns     14984927 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000000/match_pct:70               100 ns          100 ns      6150852 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100/match_pct:100                 5.77 ns         5.77 ns    120516465 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000/match_pct:100                4.62 ns         4.62 ns    154849564 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:10000/match_pct:100               5.11 ns         5.11 ns    137327117 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:100000/match_pct:100              5.11 ns         5.11 ns    135921082 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<naive::OrderBook, true, true>/depth:1000000/match_pct:100             6.15 ns         6.15 ns    110014378 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100/match_pct:0             19.3 ns         19.2 ns     37039656 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000/match_pct:0            15.7 ns         15.7 ns     45144161 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:10000/match_pct:0           17.3 ns         17.2 ns     40547626 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100000/match_pct:0          29.8 ns         29.8 ns     23297883 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:0         53.6 ns         53.6 ns     11838916 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100/match_pct:30            23.5 ns         23.4 ns     29932389 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000/match_pct:30           20.9 ns         20.9 ns     33248431 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:10000/match_pct:30          22.8 ns         22.8 ns     31145612 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100000/match_pct:30         32.0 ns         32.0 ns     22469268 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:30        51.1 ns         51.1 ns     14200882 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100/match_pct:70            30.3 ns         30.2 ns     23291969 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000/match_pct:70           26.9 ns         26.9 ns     26036528 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:10000/match_pct:70          28.3 ns         28.3 ns     24726045 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100000/match_pct:70         40.0 ns         40.0 ns     17853101 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:70        77.4 ns         77.4 ns     10000000 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100/match_pct:100           30.6 ns         30.5 ns     21998437 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000/match_pct:100          23.8 ns         23.8 ns     29583474 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:10000/match_pct:100         23.1 ns         23.1 ns     30352867 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:100000/match_pct:100        23.0 ns         23.0 ns     30391475 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:100       23.1 ns         23.1 ns     30284523 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100/match_pct:0              18.6 ns         18.6 ns     37004329 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000/match_pct:0             15.7 ns         15.7 ns     44944848 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:10000/match_pct:0            17.3 ns         17.3 ns     40453917 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100000/match_pct:0           29.8 ns         29.8 ns     23200068 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:0          53.8 ns         53.7 ns     12081419 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100/match_pct:30             21.6 ns         21.5 ns     33006042 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000/match_pct:30            18.6 ns         18.6 ns     37913593 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:10000/match_pct:30           20.8 ns         20.8 ns     34757237 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100000/match_pct:30          29.2 ns         29.2 ns     24242207 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:30         48.4 ns         48.4 ns     14697174 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100/match_pct:70             25.5 ns         25.4 ns     27510081 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000/match_pct:70            20.4 ns         20.4 ns     34272666 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:10000/match_pct:70           22.4 ns         22.4 ns     30864651 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100000/match_pct:70          35.4 ns         35.4 ns     20801731 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:70         76.2 ns         76.2 ns     10000000 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100/match_pct:100            27.5 ns         27.4 ns     26617217 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000/match_pct:100           15.1 ns         15.1 ns     46385110 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:10000/match_pct:100          14.4 ns         14.4 ns     48143485 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:100000/match_pct:100         14.6 ns         14.6 ns     48367217 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:100        14.9 ns         14.9 ns     46595522 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100/match_pct:0              60.0 ns         59.9 ns     12004501 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000/match_pct:0             54.3 ns         54.3 ns     13083327 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:10000/match_pct:0            49.2 ns         49.2 ns     13926567 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100000/match_pct:0           61.1 ns         61.1 ns     11001304 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:0          83.5 ns         83.5 ns      7978193 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100/match_pct:30             51.6 ns         51.5 ns     13847972 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000/match_pct:30            45.9 ns         45.9 ns     15277376 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:10000/match_pct:30           36.1 ns         36.1 ns     19442697 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100000/match_pct:30          48.4 ns         48.4 ns     14722415 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:30         69.4 ns         69.4 ns      9968481 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100/match_pct:70             40.8 ns         40.6 ns     17181598 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000/match_pct:70            35.5 ns         35.5 ns     19806340 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:10000/match_pct:70           33.6 ns         33.5 ns     20821629 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100000/match_pct:70          47.1 ns         47.1 ns     14819427 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:70         74.4 ns         74.4 ns      8259540 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100/match_pct:100            52.9 ns         52.8 ns     13281761 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000/match_pct:100           26.2 ns         26.2 ns     26731428 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:10000/match_pct:100          23.8 ns         23.8 ns     29396421 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:100000/match_pct:100         23.5 ns         23.5 ns     29603088 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:100        23.5 ns         23.5 ns     29727808 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100/match_pct:0               60.1 ns         60.0 ns     11888154 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000/match_pct:0              54.0 ns         54.0 ns     13102518 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:10000/match_pct:0             48.5 ns         48.5 ns     13985798 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100000/match_pct:0            61.5 ns         61.5 ns     10867000 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:0           85.7 ns         85.7 ns      7710472 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100/match_pct:30              51.2 ns         51.1 ns     13628794 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000/match_pct:30             44.8 ns         44.8 ns     15561621 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:10000/match_pct:30            33.2 ns         33.2 ns     20954453 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100000/match_pct:30           46.1 ns         46.1 ns     15120981 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:30          67.8 ns         67.8 ns     10289435 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100/match_pct:70              36.5 ns         36.3 ns     19145262 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000/match_pct:70             29.2 ns         29.2 ns     23801465 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:10000/match_pct:70            26.7 ns         26.7 ns     25787900 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100000/match_pct:70           40.8 ns         40.8 ns     17451547 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:70          76.8 ns         76.8 ns     10000000 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100/match_pct:100             66.5 ns         66.5 ns     10601816 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000/match_pct:100            19.0 ns         19.0 ns     36887710 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:10000/match_pct:100           14.7 ns         14.7 ns     47560650 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:100000/match_pct:100          14.5 ns         14.5 ns     48410789 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_BidsResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:100         14.8 ns         14.8 ns     46940518 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100/match_pct:0                 62.4 ns         62.4 ns     11073792 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000/match_pct:0                79.3 ns         79.3 ns      8828379 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:10000/match_pct:0                101 ns          101 ns      6928502 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100000/match_pct:0               166 ns          166 ns      4249958 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000000/match_pct:0              174 ns          174 ns      4082468 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100/match_pct:30                46.4 ns         46.4 ns     15032523 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000/match_pct:30               58.9 ns         58.8 ns     11904998 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:10000/match_pct:30              72.3 ns         72.3 ns      9657969 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100000/match_pct:30              105 ns          105 ns      6722754 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000000/match_pct:30             104 ns          104 ns      6841142 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100/match_pct:70                26.5 ns         26.4 ns     25942667 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000/match_pct:70               31.3 ns         31.3 ns     22377729 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:10000/match_pct:70              35.1 ns         35.1 ns     19907833 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100000/match_pct:70             42.3 ns         42.3 ns     16453343 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000000/match_pct:70            41.1 ns         41.1 ns     17223164 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100/match_pct:100               13.3 ns         13.3 ns     52790770 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000/match_pct:100              11.8 ns         11.8 ns     59252860 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:10000/match_pct:100             10.9 ns         10.9 ns     64331911 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:100000/match_pct:100            10.7 ns         10.7 ns     65836702 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, false>/depth:1000000/match_pct:100           11.1 ns         11.1 ns     62270570 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100/match_pct:0                  61.6 ns         61.6 ns     11181466 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000/match_pct:0                 79.5 ns         79.5 ns      8830170 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:10000/match_pct:0                 102 ns          102 ns      6886294 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100000/match_pct:0                166 ns          166 ns      4217171 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000000/match_pct:0               166 ns          166 ns      4518867 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100/match_pct:30                 43.4 ns         43.4 ns     15688359 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000/match_pct:30                57.1 ns         57.1 ns     12261735 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:10000/match_pct:30               70.5 ns         70.5 ns      9944613 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100000/match_pct:30               103 ns          103 ns      6869101 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000000/match_pct:30             99.9 ns         99.9 ns      6898129 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100/match_pct:70                 22.0 ns         22.0 ns     32074728 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000/match_pct:70                27.0 ns         27.0 ns     25922171 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:10000/match_pct:70               30.9 ns         30.9 ns     22720553 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100000/match_pct:70              38.5 ns         38.5 ns     18611567 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000000/match_pct:70             35.8 ns         35.8 ns     19177180 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100/match_pct:100                5.88 ns         5.87 ns    119146324 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000/match_pct:100               4.43 ns         4.44 ns    158183902 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:10000/match_pct:100              4.18 ns         4.18 ns    167504112 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:100000/match_pct:100             4.36 ns         4.36 ns    136018948 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, false, true>/depth:1000000/match_pct:100            6.11 ns         6.11 ns    114110162 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100/match_pct:0                  61.6 ns         61.6 ns     11380921 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000/match_pct:0                 79.8 ns         79.8 ns      8801252 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:10000/match_pct:0                 103 ns          103 ns      6772320 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100000/match_pct:0                212 ns          212 ns      3189759 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000000/match_pct:0               590 ns          590 ns      1000000 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100/match_pct:30                 45.7 ns         45.7 ns     15318116 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000/match_pct:30                59.0 ns         59.0 ns     11881142 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:10000/match_pct:30               74.0 ns         74.0 ns      9442389 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100000/match_pct:30               139 ns          139 ns      5064901 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000000/match_pct:30              315 ns          315 ns      1923755 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100/match_pct:70                 26.1 ns         26.1 ns     26031944 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000/match_pct:70                31.4 ns         31.4 ns     22287769 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:10000/match_pct:70               35.7 ns         35.7 ns     19601664 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100000/match_pct:70              51.9 ns         51.8 ns     13769230 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000000/match_pct:70              105 ns          105 ns      6140758 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100/match_pct:100                13.3 ns         13.2 ns     52948647 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000/match_pct:100               11.8 ns         11.8 ns     59671711 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:10000/match_pct:100              10.9 ns         10.9 ns     64344794 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:100000/match_pct:100             10.7 ns         10.7 ns     65833681 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, false>/depth:1000000/match_pct:100            11.2 ns         11.2 ns     61956837 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100/match_pct:0                   61.6 ns         61.6 ns     11232676 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000/match_pct:0                  79.7 ns         79.7 ns      8780262 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:10000/match_pct:0                  104 ns          104 ns      6833894 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100000/match_pct:0                 216 ns          216 ns      3226500 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000000/match_pct:0                605 ns          605 ns      1000000 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100/match_pct:30                  45.3 ns         45.3 ns     15574539 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000/match_pct:30                 57.2 ns         57.2 ns     12272757 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:10000/match_pct:30                72.0 ns         72.0 ns      9691781 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100000/match_pct:30                136 ns          136 ns      5105943 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000000/match_pct:30               317 ns          317 ns      1931598 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100/match_pct:70                  22.0 ns         22.0 ns     31500844 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000/match_pct:70                 27.0 ns         27.0 ns     25913633 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:10000/match_pct:70                31.4 ns         31.4 ns     22296730 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100000/match_pct:70               46.8 ns         46.8 ns     14970940 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000000/match_pct:70              98.2 ns         98.2 ns      6180182 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100/match_pct:100                 5.80 ns         5.80 ns    119874465 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000/match_pct:100                4.42 ns         4.42 ns    157171728 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:10000/match_pct:100               4.17 ns         4.17 ns    167763399 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:100000/match_pct:100              4.27 ns         4.27 ns    163837373 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<naive::OrderBook, true, true>/depth:1000000/match_pct:100             6.12 ns         6.12 ns    114889808 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100/match_pct:0             20.1 ns         20.0 ns     35778239 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000/match_pct:0            16.4 ns         16.4 ns     42411496 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:10000/match_pct:0           17.7 ns         17.7 ns     39059047 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100000/match_pct:0          31.4 ns         31.4 ns     22662487 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:0         54.1 ns         54.1 ns     12036987 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100/match_pct:30            23.7 ns         23.6 ns     29660178 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000/match_pct:30           21.1 ns         21.0 ns     33368083 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:10000/match_pct:30          22.8 ns         22.8 ns     30900708 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100000/match_pct:30         32.2 ns         32.2 ns     22648708 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:30        52.3 ns         52.3 ns     13999093 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100/match_pct:70            30.9 ns         30.8 ns     22561388 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000/match_pct:70           27.2 ns         27.2 ns     25753248 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:10000/match_pct:70          28.3 ns         28.3 ns     24838016 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100000/match_pct:70         42.1 ns         42.1 ns     16713800 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:70        76.4 ns         76.4 ns     10000000 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100/match_pct:100           51.6 ns         51.6 ns     13458230 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000/match_pct:100          26.0 ns         26.0 ns     27006987 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:10000/match_pct:100         23.6 ns         23.6 ns     29715700 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:100000/match_pct:100        23.5 ns         23.5 ns     29718312 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, false>/depth:1000000/match_pct:100       23.5 ns         23.5 ns     29726747 narrow_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100/match_pct:0              19.9 ns         19.8 ns     35606060 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000/match_pct:0             16.4 ns         16.4 ns     42762846 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:10000/match_pct:0            17.7 ns         17.7 ns     39582854 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100000/match_pct:0           31.4 ns         31.4 ns     23209611 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:0          55.4 ns         55.4 ns     12026975 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100/match_pct:30             21.5 ns         21.3 ns     32854419 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000/match_pct:30            18.6 ns         18.6 ns     37481076 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:10000/match_pct:30           20.5 ns         20.5 ns     34750186 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100000/match_pct:30          29.3 ns         29.3 ns     24118244 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:30         48.8 ns         48.8 ns     14646976 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100/match_pct:70             25.9 ns         25.8 ns     27137952 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000/match_pct:70            20.0 ns         20.0 ns     34859248 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:10000/match_pct:70           22.1 ns         22.1 ns     31247062 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100000/match_pct:70          34.6 ns         34.6 ns     20141660 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:70         75.8 ns         75.8 ns     10000000 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100/match_pct:100            66.2 ns         66.2 ns     10657855 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000/match_pct:100           18.9 ns         18.9 ns     37059593 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:10000/match_pct:100          14.5 ns         14.5 ns     48511268 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:100000/match_pct:100         14.2 ns         14.2 ns     49014171 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, false, true>/depth:1000000/match_pct:100        14.6 ns         14.6 ns     48040177 narrow_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100/match_pct:0              60.3 ns         60.2 ns     11871002 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000/match_pct:0             54.5 ns         54.5 ns     13074647 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:10000/match_pct:0            49.7 ns         49.7 ns     14218337 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100000/match_pct:0           61.0 ns         61.0 ns     11159117 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:0          84.0 ns         84.0 ns      7696426 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100/match_pct:30             51.8 ns         51.7 ns     13518212 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000/match_pct:30            46.1 ns         46.1 ns     15070589 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:10000/match_pct:30           36.3 ns         36.3 ns     19544500 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100000/match_pct:30          48.2 ns         48.2 ns     14732010 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:30         70.1 ns         70.1 ns     10214425 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100/match_pct:70             41.2 ns         41.1 ns     17021639 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000/match_pct:70            35.8 ns         35.8 ns     19850207 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:10000/match_pct:70           33.7 ns         33.7 ns     20691579 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100000/match_pct:70          49.2 ns         49.2 ns     14594670 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:70         75.1 ns         75.1 ns      8158347 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100/match_pct:100            52.6 ns         52.6 ns     13183547 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000/match_pct:100           26.2 ns         26.2 ns     26934428 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:10000/match_pct:100          23.5 ns         23.5 ns     29633971 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:100000/match_pct:100         23.4 ns         23.4 ns     29714138 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, false>/depth:1000000/match_pct:100        23.5 ns         23.5 ns     29669634 wide_range varied_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100/match_pct:0               62.1 ns         62.1 ns     11639036 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000/match_pct:0              58.3 ns         58.3 ns     12607824 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:10000/match_pct:0             53.0 ns         53.0 ns     13308660 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100000/match_pct:0            63.8 ns         63.8 ns     10426241 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:0           88.8 ns         88.8 ns      7461424 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100/match_pct:30              51.4 ns         51.3 ns     13314272 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000/match_pct:30             46.8 ns         46.8 ns     15044275 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:10000/match_pct:30            38.2 ns         38.2 ns     17903525 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100000/match_pct:30           50.0 ns         50.0 ns     10000000 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:30          70.1 ns         70.1 ns      9867656 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100/match_pct:70              36.2 ns         36.1 ns     19394292 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000/match_pct:70             29.5 ns         29.5 ns     23591371 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:10000/match_pct:70            29.8 ns         29.8 ns     23291167 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100000/match_pct:70           46.3 ns         46.3 ns     15036944 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:70          78.2 ns         78.2 ns     10000000 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100/match_pct:100             65.3 ns         65.3 ns     10635371 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000/match_pct:100            19.1 ns         19.1 ns     36742094 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:10000/match_pct:100           14.6 ns         14.6 ns     48288058 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:100000/match_pct:100          14.4 ns         14.4 ns     48708437 wide_range fixed_qty
BM_PlaceOrder_MixedTraffic_AsksResting<optimized::OrderBook, true, true>/depth:1000000/match_pct:100         14.7 ns         14.7 ns     47360601 wide_range fixed_qty
BM_CancelBids<naive::OrderBook>/depth:100                                                                     314 ns          314 ns      2230340
BM_CancelBids<naive::OrderBook>/depth:1000                                                                   4359 ns         4355 ns       160398
BM_CancelBids<naive::OrderBook>/depth:10000                                                                 58462 ns        58439 ns         9354
BM_CancelBids<naive::OrderBook>/depth:100000                                                               218950 ns       218869 ns         3334
BM_CancelBids<naive::OrderBook>/depth:1000000                                                             4621754 ns      4618414 ns          117
BM_CancelBids<optimized::OrderBook>/depth:100                                                                24.1 ns         24.1 ns     29403012
BM_CancelBids<optimized::OrderBook>/depth:1000                                                               28.5 ns         28.5 ns     24820881
BM_CancelBids<optimized::OrderBook>/depth:10000                                                              40.7 ns         40.7 ns     17211172
BM_CancelBids<optimized::OrderBook>/depth:100000                                                              129 ns          129 ns      5774894
BM_CancelBids<optimized::OrderBook>/depth:1000000                                                             259 ns          259 ns      3210843
BM_CancelAsks<naive::OrderBook>/depth:100                                                                     311 ns          311 ns      2245899
BM_CancelAsks<naive::OrderBook>/depth:1000                                                                   4319 ns         4319 ns       162600
BM_CancelAsks<naive::OrderBook>/depth:10000                                                                 59119 ns        59117 ns         9519
BM_CancelAsks<naive::OrderBook>/depth:100000                                                               216597 ns       216541 ns         3290
BM_CancelAsks<naive::OrderBook>/depth:1000000                                                             4073370 ns      4072777 ns          184
BM_CancelAsks<optimized::OrderBook>/depth:100                                                                23.9 ns         23.9 ns     29155258
BM_CancelAsks<optimized::OrderBook>/depth:1000                                                               28.7 ns         28.6 ns     24899899
BM_CancelAsks<optimized::OrderBook>/depth:10000                                                              40.8 ns         40.8 ns     17147547
BM_CancelAsks<optimized::OrderBook>/depth:100000                                                              121 ns          121 ns      6200882
BM_CancelAsks<optimized::OrderBook>/depth:1000000                                                             261 ns          261 ns      3257814
</details>

**Insertion (`NoMatch`, narrow range):** flat ~15–25ns for the optimized
version across all depths from 100 to 1,000,000; the naive version stays in
the same rough range (55–80ns) but is consistently 2–4x slower.

**Insertion (`NoMatch`, wide range):** the naive version degrades noticeably
at high depth (up to ~480ns) due to `std::map`'s O(log n) cost growing with
distinct key count; the optimized version stays far flatter (25–110ns),
though it also shows some growth at the largest depths from occupancy-bitmap
overhead across a sparser, wider range.

**Cancellation:** the most dramatic result. The naive version degrades from
~310ns at depth 100 to over **4.6 milliseconds** at depth 1,000,000 — the
expected consequence of O(n) deque-middle removal. The optimized version
stays essentially flat, ~24ns to ~260ns across the same range — a
**>10,000x** improvement at the largest depth tested.

**`AlwaysMatch` / 100% match rate — the one case where naive wins:** when
every incoming order crosses and fully depletes the book, the naive version
is faster (down to ~10–20ns) than the optimized version (up to ~240ns at
high depth). This is a real, structural finding, not noise — see the
tradeoffs section below.

## Design decisions & tradeoffs

A few decisions worth calling out explicitly, since the reasoning behind them
matters as much as the numbers:

- **Why the naive version wins on `AlwaysMatch`.** `std::map` maintains a
  cached leftmost/rightmost pointer as a side effect of every insert/erase,
  so `begin()`/`rbegin()` — the naive version's best-price lookup — is
  genuinely O(1) with zero incremental cost. The optimized version's cached
  best-price pointer is *also* O(1) most of the time, but when the current
  best price level fully empties, it has to actively search the occupancy
  bitmap for the next-best level — real work the naive version's tree never
  has to do, because it was paying that cost incrementally all along. Under
  a workload that's 100% matching with no cancellations, this search
  triggers on nearly every operation, and the optimized design's other
  advantages (O(1) insert, O(1) cancel) aren't in play to compensate. Under
  more realistic mixed traffic — partial match rates, real cancellation load
  — the optimized version's advantages dominate by a wide margin. This is a
  genuine tradeoff, not a bug: the optimized design trades a small, bounded,
  occasional cost on best-price re-search for large, consistent wins
  everywhere else.

- **Why `clear()` was deliberately left unoptimized.** The optimized book's
  `clear()` resets its full backing structures (O(pool capacity +
  price-range size)), rather than tracking touched slots for a
  proportional-cost reset. This is intentional: `clear()` isn't a realistic
  hot-path operation for a real exchange (a book is built up once and runs
  continuously, not repeatedly cleared), so optimizing it — which would
  require extra memory bookkeeping — wasn't worth the added complexity for a
  function that exists mainly to serve the benchmark harness's reset
  cadence between measurement cycles. The benchmark harness works around
  this itself (via targeted `cancelOrder`/`releaseOrder` calls rather than
  full `clear()`), so this decision doesn't affect the validity of any
  reported number.

- **Why prices are integer ticks, not floating point.** Floating-point
  rounding error is a classic source of bugs in trading systems — two
  logically equal prices can compare unequal. Prices are stored as
  fixed-point integers (cents) throughout.

- **Indices, not pointers, for all cross-references.** Both the order pool
  and the intrusive per-price-level linked list reference orders by pool
  index rather than raw pointer, specifically because the pool's backing
  `std::vector` can reallocate on growth — pointers into it would be
  silently invalidated, while indices remain valid across a resize.

## Known limitations & future work

- **No multi-symbol support.** Each `OrderBook` models a single instrument;
  a real system would route orders to one book per symbol via a separate
  gateway layer, with `id` assigned globally (already designed for this —
  see `Order::id`'s global atomic counter, kept deliberately separate from
  the book-local `sequence` field).
- **No client/server layer.** The engine is in-process only. A natural next
  step is wrapping it in a TCP-based order-entry service, with a
  single-threaded matching core fed by a queue from multiple network I/O
  threads.
- **Best-price re-search could be made O(1) unconditionally** by maintaining
  a separate doubly-linked list of *occupied* price levels (in addition to
  the per-level order chains), updated in O(1) on every level transition
  between empty and non-empty. This would close the `AlwaysMatch` gap
  entirely, at the cost of extra bookkeeping on every insert/cancel — not
  implemented here, but a clear, identified next step.
