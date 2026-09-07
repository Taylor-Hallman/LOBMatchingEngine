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

Each benchmark below is its own collapsible table, grouped by function.
"Variant" reflects the labels from the key above.

<details>
<summary><code>BM_PlaceOrder_NoMatch_Bids</code></summary>

| Implementation | Variant | Depth | Time |
|---|---|---:|---:|
| naive | narrow_range | 100 | 66.8 ns |
| naive | narrow_range | 1,000 | 78.8 ns |
| naive | narrow_range | 10,000 | 64.4 ns |
| naive | narrow_range | 100,000 | 53.8 ns |
| naive | narrow_range | 1,000,000 | 62.3 ns |
| naive | wide_range | 100 | 64.8 ns |
| naive | wide_range | 1,000 | 85.6 ns |
| naive | wide_range | 10,000 | 151.0 ns |
| naive | wide_range | 100,000 | 477.0 ns |
| naive | wide_range | 1,000,000 | 438.0 ns |
| optimized | narrow_range | 100 | 16.6 ns |
| optimized | narrow_range | 1,000 | 15.7 ns |
| optimized | narrow_range | 10,000 | 18.8 ns |
| optimized | narrow_range | 100,000 | 20.8 ns |
| optimized | narrow_range | 1,000,000 | 25.6 ns |
| optimized | wide_range | 100 | 57.2 ns |
| optimized | wide_range | 1,000 | 49.7 ns |
| optimized | wide_range | 10,000 | 42.2 ns |
| optimized | wide_range | 100,000 | 59.9 ns |
| optimized | wide_range | 1,000,000 | 112.0 ns |

</details>

<details>
<summary><code>BM_PlaceOrder_NoMatch_Asks</code></summary>

| Implementation | Variant | Depth | Time |
|---|---|---:|---:|
| naive | narrow_range | 100 | 63.1 ns |
| naive | narrow_range | 1,000 | 85.1 ns |
| naive | narrow_range | 10,000 | 64.3 ns |
| naive | narrow_range | 100,000 | 52.5 ns |
| naive | narrow_range | 1,000,000 | 56.5 ns |
| naive | wide_range | 100 | 64.1 ns |
| naive | wide_range | 1,000 | 85.1 ns |
| naive | wide_range | 10,000 | 153.0 ns |
| naive | wide_range | 100,000 | 428.0 ns |
| naive | wide_range | 1,000,000 | 413.0 ns |
| optimized | narrow_range | 100 | 16.3 ns |
| optimized | narrow_range | 1,000 | 15.4 ns |
| optimized | narrow_range | 10,000 | 18.4 ns |
| optimized | narrow_range | 100,000 | 20.9 ns |
| optimized | narrow_range | 1,000,000 | 25.9 ns |
| optimized | wide_range | 100 | 57.2 ns |
| optimized | wide_range | 1,000 | 48.8 ns |
| optimized | wide_range | 10,000 | 42.6 ns |
| optimized | wide_range | 100,000 | 59.6 ns |
| optimized | wide_range | 1,000,000 | 110.0 ns |

</details>

<details>
<summary><code>BM_PlaceOrder_AlwaysMatch_BidsResting</code></summary>

| Implementation | Variant | Depth | Time |
|---|---|---:|---:|
| naive | varied_qty | 100 | 37.8 ns |
| naive | varied_qty | 1,000 | 37.1 ns |
| naive | varied_qty | 10,000 | 29.4 ns |
| naive | varied_qty | 100,000 | 16.5 ns |
| naive | varied_qty | 1,000,000 | 25.5 ns |
| naive | fixed_qty | 100 | 29.8 ns |
| naive | fixed_qty | 1,000 | 26.9 ns |
| naive | fixed_qty | 10,000 | 21.6 ns |
| naive | fixed_qty | 100,000 | 10.1 ns |
| naive | fixed_qty | 1,000,000 | 19.8 ns |
| optimized | varied_qty | 100 | 37.7 ns |
| optimized | varied_qty | 1,000 | 30.8 ns |
| optimized | varied_qty | 10,000 | 34.2 ns |
| optimized | varied_qty | 100,000 | 65.0 ns |
| optimized | varied_qty | 1,000,000 | 239.0 ns |
| optimized | fixed_qty | 100 | 30.6 ns |
| optimized | fixed_qty | 1,000 | 22.7 ns |
| optimized | fixed_qty | 10,000 | 27.3 ns |
| optimized | fixed_qty | 100,000 | 56.9 ns |
| optimized | fixed_qty | 1,000,000 | 219.0 ns |

</details>

<details>
<summary><code>BM_PlaceOrder_AlwaysMatch_AsksResting</code></summary>

| Implementation | Variant | Depth | Time |
|---|---|---:|---:|
| naive | varied_qty | 100 | 38.2 ns |
| naive | varied_qty | 1,000 | 36.9 ns |
| naive | varied_qty | 10,000 | 29.8 ns |
| naive | varied_qty | 100,000 | 16.6 ns |
| naive | varied_qty | 1,000,000 | 25.6 ns |
| naive | fixed_qty | 100 | 29.7 ns |
| naive | fixed_qty | 1,000 | 28.5 ns |
| naive | fixed_qty | 10,000 | 21.7 ns |
| naive | fixed_qty | 100,000 | 9.7 ns |
| naive | fixed_qty | 1,000,000 | 19.8 ns |
| optimized | varied_qty | 100 | 59.8 ns |
| optimized | varied_qty | 1,000 | 32.5 ns |
| optimized | varied_qty | 10,000 | 34.1 ns |
| optimized | varied_qty | 100,000 | 64.3 ns |
| optimized | varied_qty | 1,000,000 | 238.0 ns |
| optimized | fixed_qty | 100 | 74.6 ns |
| optimized | fixed_qty | 1,000 | 29.1 ns |
| optimized | fixed_qty | 10,000 | 27.5 ns |
| optimized | fixed_qty | 100,000 | 61.5 ns |
| optimized | fixed_qty | 1,000,000 | 225.0 ns |

</details>

<details>
<summary><code>BM_PlaceOrder_MixedTraffic_BidsResting</code></summary>

| Implementation | Variant | Depth | Match % | Time |
|---|---|---:|---:|---:|
| naive | narrow_range varied_qty | 100 | 0% | 61.4 ns |
| naive | narrow_range varied_qty | 1,000 | 0% | 79.8 ns |
| naive | narrow_range varied_qty | 10,000 | 0% | 102.0 ns |
| naive | narrow_range varied_qty | 100,000 | 0% | 168.0 ns |
| naive | narrow_range varied_qty | 1,000,000 | 0% | 175.0 ns |
| naive | narrow_range varied_qty | 100 | 30% | 48.7 ns |
| naive | narrow_range varied_qty | 1,000 | 30% | 59.4 ns |
| naive | narrow_range varied_qty | 10,000 | 30% | 73.0 ns |
| naive | narrow_range varied_qty | 100,000 | 30% | 105.0 ns |
| naive | narrow_range varied_qty | 1,000,000 | 30% | 105.0 ns |
| naive | narrow_range varied_qty | 100 | 70% | 27.1 ns |
| naive | narrow_range varied_qty | 1,000 | 70% | 31.5 ns |
| naive | narrow_range varied_qty | 10,000 | 70% | 35.4 ns |
| naive | narrow_range varied_qty | 100,000 | 70% | 42.2 ns |
| naive | narrow_range varied_qty | 1,000,000 | 70% | 41.1 ns |
| naive | narrow_range varied_qty | 100 | 100% | 13.2 ns |
| naive | narrow_range varied_qty | 1,000 | 100% | 11.6 ns |
| naive | narrow_range varied_qty | 10,000 | 100% | 10.8 ns |
| naive | narrow_range varied_qty | 100,000 | 100% | 10.4 ns |
| naive | narrow_range varied_qty | 1,000,000 | 100% | 10.8 ns |
| naive | narrow_range fixed_qty | 100 | 0% | 60.4 ns |
| naive | narrow_range fixed_qty | 1,000 | 0% | 79.9 ns |
| naive | narrow_range fixed_qty | 10,000 | 0% | 101.0 ns |
| naive | narrow_range fixed_qty | 100,000 | 0% | 164.0 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 0% | 175.0 ns |
| naive | narrow_range fixed_qty | 100 | 30% | 44.0 ns |
| naive | narrow_range fixed_qty | 1,000 | 30% | 57.4 ns |
| naive | narrow_range fixed_qty | 10,000 | 30% | 71.8 ns |
| naive | narrow_range fixed_qty | 100,000 | 30% | 105.0 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 30% | 102.0 ns |
| naive | narrow_range fixed_qty | 100 | 70% | 22.5 ns |
| naive | narrow_range fixed_qty | 1,000 | 70% | 27.2 ns |
| naive | narrow_range fixed_qty | 10,000 | 70% | 31.1 ns |
| naive | narrow_range fixed_qty | 100,000 | 70% | 38.0 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 70% | 36.3 ns |
| naive | narrow_range fixed_qty | 100 | 100% | 5.7 ns |
| naive | narrow_range fixed_qty | 1,000 | 100% | 4.5 ns |
| naive | narrow_range fixed_qty | 10,000 | 100% | 4.2 ns |
| naive | narrow_range fixed_qty | 100,000 | 100% | 4.2 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 100% | 6.1 ns |
| naive | wide_range varied_qty | 100 | 0% | 61.3 ns |
| naive | wide_range varied_qty | 1,000 | 0% | 79.6 ns |
| naive | wide_range varied_qty | 10,000 | 0% | 103.0 ns |
| naive | wide_range varied_qty | 100,000 | 0% | 213.0 ns |
| naive | wide_range varied_qty | 1,000,000 | 0% | 609.0 ns |
| naive | wide_range varied_qty | 100 | 30% | 47.1 ns |
| naive | wide_range varied_qty | 1,000 | 30% | 59.4 ns |
| naive | wide_range varied_qty | 10,000 | 30% | 74.1 ns |
| naive | wide_range varied_qty | 100,000 | 30% | 137.0 ns |
| naive | wide_range varied_qty | 1,000,000 | 30% | 314.0 ns |
| naive | wide_range varied_qty | 100 | 70% | 27.6 ns |
| naive | wide_range varied_qty | 1,000 | 70% | 31.6 ns |
| naive | wide_range varied_qty | 10,000 | 70% | 36.0 ns |
| naive | wide_range varied_qty | 100,000 | 70% | 52.0 ns |
| naive | wide_range varied_qty | 1,000,000 | 70% | 106.0 ns |
| naive | wide_range varied_qty | 100 | 100% | 13.0 ns |
| naive | wide_range varied_qty | 1,000 | 100% | 11.6 ns |
| naive | wide_range varied_qty | 10,000 | 100% | 10.9 ns |
| naive | wide_range varied_qty | 100,000 | 100% | 10.5 ns |
| naive | wide_range varied_qty | 1,000,000 | 100% | 10.9 ns |
| naive | wide_range fixed_qty | 100 | 0% | 59.1 ns |
| naive | wide_range fixed_qty | 1,000 | 0% | 80.2 ns |
| naive | wide_range fixed_qty | 10,000 | 0% | 103.0 ns |
| naive | wide_range fixed_qty | 100,000 | 0% | 221.0 ns |
| naive | wide_range fixed_qty | 1,000,000 | 0% | 569.0 ns |
| naive | wide_range fixed_qty | 100 | 30% | 45.4 ns |
| naive | wide_range fixed_qty | 1,000 | 30% | 57.6 ns |
| naive | wide_range fixed_qty | 10,000 | 30% | 72.3 ns |
| naive | wide_range fixed_qty | 100,000 | 30% | 136.0 ns |
| naive | wide_range fixed_qty | 1,000,000 | 30% | 307.0 ns |
| naive | wide_range fixed_qty | 100 | 70% | 23.1 ns |
| naive | wide_range fixed_qty | 1,000 | 70% | 27.2 ns |
| naive | wide_range fixed_qty | 10,000 | 70% | 31.7 ns |
| naive | wide_range fixed_qty | 100,000 | 70% | 47.0 ns |
| naive | wide_range fixed_qty | 1,000,000 | 70% | 100.0 ns |
| naive | wide_range fixed_qty | 100 | 100% | 5.8 ns |
| naive | wide_range fixed_qty | 1,000 | 100% | 4.6 ns |
| naive | wide_range fixed_qty | 10,000 | 100% | 5.1 ns |
| naive | wide_range fixed_qty | 100,000 | 100% | 5.1 ns |
| naive | wide_range fixed_qty | 1,000,000 | 100% | 6.2 ns |
| optimized | narrow_range varied_qty | 100 | 0% | 19.3 ns |
| optimized | narrow_range varied_qty | 1,000 | 0% | 15.7 ns |
| optimized | narrow_range varied_qty | 10,000 | 0% | 17.3 ns |
| optimized | narrow_range varied_qty | 100,000 | 0% | 29.8 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 0% | 53.6 ns |
| optimized | narrow_range varied_qty | 100 | 30% | 23.5 ns |
| optimized | narrow_range varied_qty | 1,000 | 30% | 20.9 ns |
| optimized | narrow_range varied_qty | 10,000 | 30% | 22.8 ns |
| optimized | narrow_range varied_qty | 100,000 | 30% | 32.0 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 30% | 51.1 ns |
| optimized | narrow_range varied_qty | 100 | 70% | 30.3 ns |
| optimized | narrow_range varied_qty | 1,000 | 70% | 26.9 ns |
| optimized | narrow_range varied_qty | 10,000 | 70% | 28.3 ns |
| optimized | narrow_range varied_qty | 100,000 | 70% | 40.0 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 70% | 77.4 ns |
| optimized | narrow_range varied_qty | 100 | 100% | 30.6 ns |
| optimized | narrow_range varied_qty | 1,000 | 100% | 23.8 ns |
| optimized | narrow_range varied_qty | 10,000 | 100% | 23.1 ns |
| optimized | narrow_range varied_qty | 100,000 | 100% | 23.0 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 100% | 23.1 ns |
| optimized | narrow_range fixed_qty | 100 | 0% | 18.6 ns |
| optimized | narrow_range fixed_qty | 1,000 | 0% | 15.7 ns |
| optimized | narrow_range fixed_qty | 10,000 | 0% | 17.3 ns |
| optimized | narrow_range fixed_qty | 100,000 | 0% | 29.8 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 0% | 53.8 ns |
| optimized | narrow_range fixed_qty | 100 | 30% | 21.6 ns |
| optimized | narrow_range fixed_qty | 1,000 | 30% | 18.6 ns |
| optimized | narrow_range fixed_qty | 10,000 | 30% | 20.8 ns |
| optimized | narrow_range fixed_qty | 100,000 | 30% | 29.2 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 30% | 48.4 ns |
| optimized | narrow_range fixed_qty | 100 | 70% | 25.5 ns |
| optimized | narrow_range fixed_qty | 1,000 | 70% | 20.4 ns |
| optimized | narrow_range fixed_qty | 10,000 | 70% | 22.4 ns |
| optimized | narrow_range fixed_qty | 100,000 | 70% | 35.4 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 70% | 76.2 ns |
| optimized | narrow_range fixed_qty | 100 | 100% | 27.5 ns |
| optimized | narrow_range fixed_qty | 1,000 | 100% | 15.1 ns |
| optimized | narrow_range fixed_qty | 10,000 | 100% | 14.4 ns |
| optimized | narrow_range fixed_qty | 100,000 | 100% | 14.6 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 100% | 14.9 ns |
| optimized | wide_range varied_qty | 100 | 0% | 60.0 ns |
| optimized | wide_range varied_qty | 1,000 | 0% | 54.3 ns |
| optimized | wide_range varied_qty | 10,000 | 0% | 49.2 ns |
| optimized | wide_range varied_qty | 100,000 | 0% | 61.1 ns |
| optimized | wide_range varied_qty | 1,000,000 | 0% | 83.5 ns |
| optimized | wide_range varied_qty | 100 | 30% | 51.6 ns |
| optimized | wide_range varied_qty | 1,000 | 30% | 45.9 ns |
| optimized | wide_range varied_qty | 10,000 | 30% | 36.1 ns |
| optimized | wide_range varied_qty | 100,000 | 30% | 48.4 ns |
| optimized | wide_range varied_qty | 1,000,000 | 30% | 69.4 ns |
| optimized | wide_range varied_qty | 100 | 70% | 40.8 ns |
| optimized | wide_range varied_qty | 1,000 | 70% | 35.5 ns |
| optimized | wide_range varied_qty | 10,000 | 70% | 33.6 ns |
| optimized | wide_range varied_qty | 100,000 | 70% | 47.1 ns |
| optimized | wide_range varied_qty | 1,000,000 | 70% | 74.4 ns |
| optimized | wide_range varied_qty | 100 | 100% | 52.9 ns |
| optimized | wide_range varied_qty | 1,000 | 100% | 26.2 ns |
| optimized | wide_range varied_qty | 10,000 | 100% | 23.8 ns |
| optimized | wide_range varied_qty | 100,000 | 100% | 23.5 ns |
| optimized | wide_range varied_qty | 1,000,000 | 100% | 23.5 ns |
| optimized | wide_range fixed_qty | 100 | 0% | 60.1 ns |
| optimized | wide_range fixed_qty | 1,000 | 0% | 54.0 ns |
| optimized | wide_range fixed_qty | 10,000 | 0% | 48.5 ns |
| optimized | wide_range fixed_qty | 100,000 | 0% | 61.5 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 0% | 85.7 ns |
| optimized | wide_range fixed_qty | 100 | 30% | 51.2 ns |
| optimized | wide_range fixed_qty | 1,000 | 30% | 44.8 ns |
| optimized | wide_range fixed_qty | 10,000 | 30% | 33.2 ns |
| optimized | wide_range fixed_qty | 100,000 | 30% | 46.1 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 30% | 67.8 ns |
| optimized | wide_range fixed_qty | 100 | 70% | 36.5 ns |
| optimized | wide_range fixed_qty | 1,000 | 70% | 29.2 ns |
| optimized | wide_range fixed_qty | 10,000 | 70% | 26.7 ns |
| optimized | wide_range fixed_qty | 100,000 | 70% | 40.8 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 70% | 76.8 ns |
| optimized | wide_range fixed_qty | 100 | 100% | 66.5 ns |
| optimized | wide_range fixed_qty | 1,000 | 100% | 19.0 ns |
| optimized | wide_range fixed_qty | 10,000 | 100% | 14.7 ns |
| optimized | wide_range fixed_qty | 100,000 | 100% | 14.5 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 100% | 14.8 ns |

</details>

<details>
<summary><code>BM_PlaceOrder_MixedTraffic_AsksResting</code></summary>

| Implementation | Variant | Depth | Match % | Time |
|---|---|---:|---:|---:|
| naive | narrow_range varied_qty | 100 | 0% | 62.4 ns |
| naive | narrow_range varied_qty | 1,000 | 0% | 79.3 ns |
| naive | narrow_range varied_qty | 10,000 | 0% | 101.0 ns |
| naive | narrow_range varied_qty | 100,000 | 0% | 166.0 ns |
| naive | narrow_range varied_qty | 1,000,000 | 0% | 174.0 ns |
| naive | narrow_range varied_qty | 100 | 30% | 46.4 ns |
| naive | narrow_range varied_qty | 1,000 | 30% | 58.9 ns |
| naive | narrow_range varied_qty | 10,000 | 30% | 72.3 ns |
| naive | narrow_range varied_qty | 100,000 | 30% | 105.0 ns |
| naive | narrow_range varied_qty | 1,000,000 | 30% | 104.0 ns |
| naive | narrow_range varied_qty | 100 | 70% | 26.5 ns |
| naive | narrow_range varied_qty | 1,000 | 70% | 31.3 ns |
| naive | narrow_range varied_qty | 10,000 | 70% | 35.1 ns |
| naive | narrow_range varied_qty | 100,000 | 70% | 42.3 ns |
| naive | narrow_range varied_qty | 1,000,000 | 70% | 41.1 ns |
| naive | narrow_range varied_qty | 100 | 100% | 13.3 ns |
| naive | narrow_range varied_qty | 1,000 | 100% | 11.8 ns |
| naive | narrow_range varied_qty | 10,000 | 100% | 10.9 ns |
| naive | narrow_range varied_qty | 100,000 | 100% | 10.7 ns |
| naive | narrow_range varied_qty | 1,000,000 | 100% | 11.1 ns |
| naive | narrow_range fixed_qty | 100 | 0% | 61.6 ns |
| naive | narrow_range fixed_qty | 1,000 | 0% | 79.5 ns |
| naive | narrow_range fixed_qty | 10,000 | 0% | 102.0 ns |
| naive | narrow_range fixed_qty | 100,000 | 0% | 166.0 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 0% | 166.0 ns |
| naive | narrow_range fixed_qty | 100 | 30% | 43.4 ns |
| naive | narrow_range fixed_qty | 1,000 | 30% | 57.1 ns |
| naive | narrow_range fixed_qty | 10,000 | 30% | 70.5 ns |
| naive | narrow_range fixed_qty | 100,000 | 30% | 103.0 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 30% | 99.9 ns |
| naive | narrow_range fixed_qty | 100 | 70% | 22.0 ns |
| naive | narrow_range fixed_qty | 1,000 | 70% | 27.0 ns |
| naive | narrow_range fixed_qty | 10,000 | 70% | 30.9 ns |
| naive | narrow_range fixed_qty | 100,000 | 70% | 38.5 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 70% | 35.8 ns |
| naive | narrow_range fixed_qty | 100 | 100% | 5.9 ns |
| naive | narrow_range fixed_qty | 1,000 | 100% | 4.4 ns |
| naive | narrow_range fixed_qty | 10,000 | 100% | 4.2 ns |
| naive | narrow_range fixed_qty | 100,000 | 100% | 4.4 ns |
| naive | narrow_range fixed_qty | 1,000,000 | 100% | 6.1 ns |
| naive | wide_range varied_qty | 100 | 0% | 61.6 ns |
| naive | wide_range varied_qty | 1,000 | 0% | 79.8 ns |
| naive | wide_range varied_qty | 10,000 | 0% | 103.0 ns |
| naive | wide_range varied_qty | 100,000 | 0% | 212.0 ns |
| naive | wide_range varied_qty | 1,000,000 | 0% | 590.0 ns |
| naive | wide_range varied_qty | 100 | 30% | 45.7 ns |
| naive | wide_range varied_qty | 1,000 | 30% | 59.0 ns |
| naive | wide_range varied_qty | 10,000 | 30% | 74.0 ns |
| naive | wide_range varied_qty | 100,000 | 30% | 139.0 ns |
| naive | wide_range varied_qty | 1,000,000 | 30% | 315.0 ns |
| naive | wide_range varied_qty | 100 | 70% | 26.1 ns |
| naive | wide_range varied_qty | 1,000 | 70% | 31.4 ns |
| naive | wide_range varied_qty | 10,000 | 70% | 35.7 ns |
| naive | wide_range varied_qty | 100,000 | 70% | 51.9 ns |
| naive | wide_range varied_qty | 1,000,000 | 70% | 105.0 ns |
| naive | wide_range varied_qty | 100 | 100% | 13.3 ns |
| naive | wide_range varied_qty | 1,000 | 100% | 11.8 ns |
| naive | wide_range varied_qty | 10,000 | 100% | 10.9 ns |
| naive | wide_range varied_qty | 100,000 | 100% | 10.7 ns |
| naive | wide_range varied_qty | 1,000,000 | 100% | 11.2 ns |
| naive | wide_range fixed_qty | 100 | 0% | 61.6 ns |
| naive | wide_range fixed_qty | 1,000 | 0% | 79.7 ns |
| naive | wide_range fixed_qty | 10,000 | 0% | 104.0 ns |
| naive | wide_range fixed_qty | 100,000 | 0% | 216.0 ns |
| naive | wide_range fixed_qty | 1,000,000 | 0% | 605.0 ns |
| naive | wide_range fixed_qty | 100 | 30% | 45.3 ns |
| naive | wide_range fixed_qty | 1,000 | 30% | 57.2 ns |
| naive | wide_range fixed_qty | 10,000 | 30% | 72.0 ns |
| naive | wide_range fixed_qty | 100,000 | 30% | 136.0 ns |
| naive | wide_range fixed_qty | 1,000,000 | 30% | 317.0 ns |
| naive | wide_range fixed_qty | 100 | 70% | 22.0 ns |
| naive | wide_range fixed_qty | 1,000 | 70% | 27.0 ns |
| naive | wide_range fixed_qty | 10,000 | 70% | 31.4 ns |
| naive | wide_range fixed_qty | 100,000 | 70% | 46.8 ns |
| naive | wide_range fixed_qty | 1,000,000 | 70% | 98.2 ns |
| naive | wide_range fixed_qty | 100 | 100% | 5.8 ns |
| naive | wide_range fixed_qty | 1,000 | 100% | 4.4 ns |
| naive | wide_range fixed_qty | 10,000 | 100% | 4.2 ns |
| naive | wide_range fixed_qty | 100,000 | 100% | 4.3 ns |
| naive | wide_range fixed_qty | 1,000,000 | 100% | 6.1 ns |
| optimized | narrow_range varied_qty | 100 | 0% | 20.1 ns |
| optimized | narrow_range varied_qty | 1,000 | 0% | 16.4 ns |
| optimized | narrow_range varied_qty | 10,000 | 0% | 17.7 ns |
| optimized | narrow_range varied_qty | 100,000 | 0% | 31.4 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 0% | 54.1 ns |
| optimized | narrow_range varied_qty | 100 | 30% | 23.7 ns |
| optimized | narrow_range varied_qty | 1,000 | 30% | 21.1 ns |
| optimized | narrow_range varied_qty | 10,000 | 30% | 22.8 ns |
| optimized | narrow_range varied_qty | 100,000 | 30% | 32.2 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 30% | 52.3 ns |
| optimized | narrow_range varied_qty | 100 | 70% | 30.9 ns |
| optimized | narrow_range varied_qty | 1,000 | 70% | 27.2 ns |
| optimized | narrow_range varied_qty | 10,000 | 70% | 28.3 ns |
| optimized | narrow_range varied_qty | 100,000 | 70% | 42.1 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 70% | 76.4 ns |
| optimized | narrow_range varied_qty | 100 | 100% | 51.6 ns |
| optimized | narrow_range varied_qty | 1,000 | 100% | 26.0 ns |
| optimized | narrow_range varied_qty | 10,000 | 100% | 23.6 ns |
| optimized | narrow_range varied_qty | 100,000 | 100% | 23.5 ns |
| optimized | narrow_range varied_qty | 1,000,000 | 100% | 23.5 ns |
| optimized | narrow_range fixed_qty | 100 | 0% | 19.9 ns |
| optimized | narrow_range fixed_qty | 1,000 | 0% | 16.4 ns |
| optimized | narrow_range fixed_qty | 10,000 | 0% | 17.7 ns |
| optimized | narrow_range fixed_qty | 100,000 | 0% | 31.4 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 0% | 55.4 ns |
| optimized | narrow_range fixed_qty | 100 | 30% | 21.5 ns |
| optimized | narrow_range fixed_qty | 1,000 | 30% | 18.6 ns |
| optimized | narrow_range fixed_qty | 10,000 | 30% | 20.5 ns |
| optimized | narrow_range fixed_qty | 100,000 | 30% | 29.3 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 30% | 48.8 ns |
| optimized | narrow_range fixed_qty | 100 | 70% | 25.9 ns |
| optimized | narrow_range fixed_qty | 1,000 | 70% | 20.0 ns |
| optimized | narrow_range fixed_qty | 10,000 | 70% | 22.1 ns |
| optimized | narrow_range fixed_qty | 100,000 | 70% | 34.6 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 70% | 75.8 ns |
| optimized | narrow_range fixed_qty | 100 | 100% | 66.2 ns |
| optimized | narrow_range fixed_qty | 1,000 | 100% | 18.9 ns |
| optimized | narrow_range fixed_qty | 10,000 | 100% | 14.5 ns |
| optimized | narrow_range fixed_qty | 100,000 | 100% | 14.2 ns |
| optimized | narrow_range fixed_qty | 1,000,000 | 100% | 14.6 ns |
| optimized | wide_range varied_qty | 100 | 0% | 60.3 ns |
| optimized | wide_range varied_qty | 1,000 | 0% | 54.5 ns |
| optimized | wide_range varied_qty | 10,000 | 0% | 49.7 ns |
| optimized | wide_range varied_qty | 100,000 | 0% | 61.0 ns |
| optimized | wide_range varied_qty | 1,000,000 | 0% | 84.0 ns |
| optimized | wide_range varied_qty | 100 | 30% | 51.8 ns |
| optimized | wide_range varied_qty | 1,000 | 30% | 46.1 ns |
| optimized | wide_range varied_qty | 10,000 | 30% | 36.3 ns |
| optimized | wide_range varied_qty | 100,000 | 30% | 48.2 ns |
| optimized | wide_range varied_qty | 1,000,000 | 30% | 70.1 ns |
| optimized | wide_range varied_qty | 100 | 70% | 41.2 ns |
| optimized | wide_range varied_qty | 1,000 | 70% | 35.8 ns |
| optimized | wide_range varied_qty | 10,000 | 70% | 33.7 ns |
| optimized | wide_range varied_qty | 100,000 | 70% | 49.2 ns |
| optimized | wide_range varied_qty | 1,000,000 | 70% | 75.1 ns |
| optimized | wide_range varied_qty | 100 | 100% | 52.6 ns |
| optimized | wide_range varied_qty | 1,000 | 100% | 26.2 ns |
| optimized | wide_range varied_qty | 10,000 | 100% | 23.5 ns |
| optimized | wide_range varied_qty | 100,000 | 100% | 23.4 ns |
| optimized | wide_range varied_qty | 1,000,000 | 100% | 23.5 ns |
| optimized | wide_range fixed_qty | 100 | 0% | 62.1 ns |
| optimized | wide_range fixed_qty | 1,000 | 0% | 58.3 ns |
| optimized | wide_range fixed_qty | 10,000 | 0% | 53.0 ns |
| optimized | wide_range fixed_qty | 100,000 | 0% | 63.8 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 0% | 88.8 ns |
| optimized | wide_range fixed_qty | 100 | 30% | 51.4 ns |
| optimized | wide_range fixed_qty | 1,000 | 30% | 46.8 ns |
| optimized | wide_range fixed_qty | 10,000 | 30% | 38.2 ns |
| optimized | wide_range fixed_qty | 100,000 | 30% | 50.0 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 30% | 70.1 ns |
| optimized | wide_range fixed_qty | 100 | 70% | 36.2 ns |
| optimized | wide_range fixed_qty | 1,000 | 70% | 29.5 ns |
| optimized | wide_range fixed_qty | 10,000 | 70% | 29.8 ns |
| optimized | wide_range fixed_qty | 100,000 | 70% | 46.3 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 70% | 78.2 ns |
| optimized | wide_range fixed_qty | 100 | 100% | 65.3 ns |
| optimized | wide_range fixed_qty | 1,000 | 100% | 19.1 ns |
| optimized | wide_range fixed_qty | 10,000 | 100% | 14.6 ns |
| optimized | wide_range fixed_qty | 100,000 | 100% | 14.4 ns |
| optimized | wide_range fixed_qty | 1,000,000 | 100% | 14.7 ns |

</details>

<details>
<summary><code>BM_CancelBids</code></summary>

| Implementation | Variant | Depth | Time |
|---|---|---:|---:|
| naive | — | 100 | 314.0 ns |
| naive | — | 1,000 | 4.36 µs |
| naive | — | 10,000 | 58.46 µs |
| naive | — | 100,000 | 218.95 µs |
| naive | — | 1,000,000 | 4.62 ms |
| optimized | — | 100 | 24.1 ns |
| optimized | — | 1,000 | 28.5 ns |
| optimized | — | 10,000 | 40.7 ns |
| optimized | — | 100,000 | 129.0 ns |
| optimized | — | 1,000,000 | 259.0 ns |

</details>

<details>
<summary><code>BM_CancelAsks</code></summary>

| Implementation | Variant | Depth | Time |
|---|---|---:|---:|
| naive | — | 100 | 311.0 ns |
| naive | — | 1,000 | 4.32 µs |
| naive | — | 10,000 | 59.12 µs |
| naive | — | 100,000 | 216.60 µs |
| naive | — | 1,000,000 | 4.07 ms |
| optimized | — | 100 | 23.9 ns |
| optimized | — | 1,000 | 28.7 ns |
| optimized | — | 10,000 | 40.8 ns |
| optimized | — | 100,000 | 121.0 ns |
| optimized | — | 1,000,000 | 261.0 ns |

</details>

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
